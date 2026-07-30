#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>
#include <optional>

namespace {

class MemoryCredentialBackend final
    : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }

    std::expected<void, QString> put(const QString& key,
                                     const QByteArray& secret) override {
        values.insert(key, secret);
        return {};
    }

    std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) override {
        const auto value = values.constFind(key);
        if (value == values.cend()) {
            return std::optional<QByteArray>{};
        }
        return std::optional<QByteArray>{*value};
    }

    std::expected<void, QString> remove(const QString& key) override {
        values.remove(key);
        return {};
    }

    QHash<QString, QByteArray> values;
};

bool connectionState(QAbstractItemModel* model, const QString& connectorId) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, ConnectionModel::ConnectedRole).toBool();
        }
    }
    return false;
}

bool waitUntil(const std::function<bool()>& predicate,
               const int timeoutMilliseconds = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    }
    return predicate();
}

class ZenSettingsCleanup final {
public:
    ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
    ~ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
};

}  // namespace

TEST_CASE("new installations start with empty data and optional demo mode") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(controller.categories()->rowCount() == 0);
    CHECK(controller.holdings()->rowCount() == 0);
    CHECK(controller.netWorth() == QStringLiteral("0 ₽"));

    controller.loadDemoData();
    CHECK(controller.demoMode());
    CHECK(controller.accounts()->rowCount() > 0);

    controller.clearDemoData();
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
}

TEST_CASE("official bank tokens have independent credential state") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    REQUIRE(controller.saveBankToken(QStringLiteral("alfa-api"),
                                     QStringLiteral("alfa-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("tbank-business"),
                                     QStringLiteral("tbank-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("modulbank"),
                                     QStringLiteral("sandboxtoken")));

    CHECK(stored->values.value(QStringLiteral("alfa-b2c-readonly")) ==
          QByteArrayLiteral("alfa-token-valid"));
    CHECK(stored->values.value(QStringLiteral("tbank-business-readonly")) ==
          QByteArrayLiteral("tbank-token-valid"));
    CHECK(stored->values.value(QStringLiteral("modulbank-readonly")) ==
          QByteArrayLiteral("sandboxtoken"));
    CHECK_FALSE(controller.syncStatus().contains(
        QStringLiteral("sandboxtoken")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("tbank-business")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));

    controller.removeBankToken(QStringLiteral("alfa-api"));
    CHECK_FALSE(
        stored->values.contains(QStringLiteral("alfa-b2c-readonly")));
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));
}

TEST_CASE("application runs a selected local bundle through the embedded host") {
    if (QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty()) {
        SKIP("Node.js is not installed");
    }
    const ZenSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);
    const auto fixture = QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR
        "/tests/fixtures/zen_plugin_fixture.cjs");

    REQUIRE(controller.inspectZenPluginBundle(
        QUrl::fromLocalFile(fixture)));
    CHECK(controller.zenPluginId()
          == QStringLiteral("zen_plugin_fixture"));
    REQUIRE(controller.startZenPlugin(
        QStringLiteral("Fixture Bank"),
        {{QStringLiteral("login"), QStringLiteral("fixture")},
         {QStringLiteral("password"),
          QStringLiteral("session-only-secret")},
         {QStringLiteral("startDate"),
          QStringLiteral("2026-07-01T00:00:00.000Z")}}));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginPrompt().isEmpty();
    }));
    REQUIRE(controller.answerZenPluginPrompt(
        QStringLiteral("123456")));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginRunning();
    }));
    CHECK(controller.accounts()->rowCount() == 1);
    CHECK(controller.transactions()->rowCount() == 1);
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.syncStatus().contains(
        QStringLiteral("Fixture Bank")));

    controller.removeZenPlugin();
}

TEST_CASE("bank connection state rejects malformed and unknown tokens") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("modulbank"), QStringLiteral("short")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("alfa-api"), QStringLiteral("token with spaces")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("unknown-bank"), QStringLiteral("valid-token")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("modulbank-readonly")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("alfa-b2c-readonly")));
}
