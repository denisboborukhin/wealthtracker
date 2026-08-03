#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

import wt.tbank_tls;

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

QVariant connectionValue(QAbstractItemModel* model, const QString& connectorId,
                         const int role) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, role);
        }
    }
    return {};
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

TEST_CASE("T-Bank is bundled while other retail banks remain adapter slots") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    const auto tbankAvailable = connectionValue(
        controller.bankConnections(), QStringLiteral("tbank-retail"),
        wealthtracker::app::ConnectionModel::AvailableRole).toBool();
    CHECK(tbankAvailable
          == !QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty());
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("tbank-retail")));
    CHECK_FALSE(controller.tbankSessionAvailable());
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("tbank.ru")));
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::StatusRole)
              .toString()
              .contains(QStringLiteral("вход")));
    controller.describeBankConnection(QStringLiteral("tbank-retail"));
    CHECK(controller.connectionMessage().contains(QStringLiteral("psid")));
    CHECK(controller.connectionMessage().contains(
        QStringLiteral("не передаются приложению")));

    CHECK(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://www.tbank.ru/auth/login/"))));
    CHECK(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://id.tbank.ru/auth/step?cid=test"))));
    CHECK(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://id.tinkoff.ru/auth/getuaid?k=test"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://www.tinkoff.ru/"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://evil.tinkoff.ru/"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://tbank.ru.evil.example/"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("http://id.tinkoff.ru/auth/"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://user@id.tinkoff.ru/auth/"))));
    CHECK_FALSE(controller.isAllowedTBankLoginUrl(
        QUrl(QStringLiteral("https://id.tinkoff.ru:8443/auth/"))));
    CHECK(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://mddc.tbank.ru/collect"))));
    CHECK(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://mddc.tinkoff.ru/collect"))));
    CHECK(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://api-statist.tinkoff.ru/events"))));
    CHECK(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://api-statist.dev-tcsgroup.io/events"))));
    CHECK_FALSE(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://mddc.tbank.ru.evil.example/"))));
    CHECK_FALSE(controller.isBlockedTBankLoginSubresource(
        QUrl(QStringLiteral("https://api-statist.tinkoff.ru.evil.example/"))));

    for (const auto& connectorId :
         {QStringLiteral("alfa-retail"), QStringLiteral("sber-retail"),
          QStringLiteral("vtb-retail")}) {
        CHECK_FALSE(connectionValue(controller.bankConnections(), connectorId,
                                    wealthtracker::app::ConnectionModel::
                                        AvailableRole)
                        .toBool());
        CHECK(connectionValue(controller.bankConnections(), connectorId,
                              wealthtracker::app::ConnectionModel::MethodRole)
                  .toString()
                  .contains(QStringLiteral("Телефон + код")));
        CHECK(connectionValue(controller.bankConnections(), connectorId,
                              wealthtracker::app::ConnectionModel::StatusRole)
                  .toString()
                  .contains(QStringLiteral("адаптер")));
    }
}

TEST_CASE("T-Bank runtime bundle is embedded in the application") {
    QFile bundle(QStringLiteral(
        ":/org/wealthtracker/runtime/bundles/tbank-retail/tbank-retail.cjs"));
    QFile preferences(QStringLiteral(
        ":/org/wealthtracker/runtime/bundles/tbank-retail/preferences.xml"));
    REQUIRE(bundle.open(QIODevice::ReadOnly));
    REQUIRE(preferences.open(QIODevice::ReadOnly));
    const auto source = bundle.readAll();
    CHECK(source.contains(QByteArrayLiteral("accounts_light_ib")));
    CHECK(source.contains(QByteArrayLiteral("apiGet('operations'")));
    CHECK(preferences.readAll().contains(QByteArrayLiteral("startDate")));
}

TEST_CASE("T-Bank login UI bounds loading and exposes navigation failures") {
    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral(
        "App.isAllowedTBankLoginUrl(request.url)")));
    CHECK(source.contains(QByteArrayLiteral(
        "WebEngineView.LoadFailedStatus")));
    CHECK(source.contains(QByteArrayLiteral("interval: 25000")));
    CHECK(source.contains(QByteArrayLiteral("tbankPageError")));
    CHECK(source.contains(QByteArrayLiteral("retryTBankPage")));
    CHECK(source.contains(QByteArrayLiteral("onCertificateError")));
    CHECK(source.contains(QByteArrayLiteral(
        "App.canTrustTBankCertificate(error)")));
    CHECK(source.contains(QByteArrayLiteral("error.acceptCertificate()")));
    CHECK(source.contains(QByteArrayLiteral(
        "App.reportTBankCertificateError")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("error.description")));
    CHECK(source.contains(QByteArrayLiteral("App.tbankSessionAvailable")));
    CHECK(source.contains(QByteArrayLiteral("Проверить сессию")));
}

TEST_CASE("T-Bank Russian-CA trust requires exact host error and root pin") {
    using wealthtracker::app::TBankCertificateEvidence;
    using wealthtracker::app::is_trusted_tbank_certificate;
    const std::string root{
        "d26d2d0231b7c39f92cc738512ba54103519e4405d68b5bd703e9788ca8ecf31"};
    const std::string leaf{
        "015438de596351e39658aadc152a5109aafc884820caafed7d0165ac962aaea1"};
    const std::string intermediate{
        "2155785036c900dbb5f1bb2a1569c80c55595bd6bf94867a29bbddbc7d88a3f2"};
    const std::vector<std::string> completeChain{leaf, intermediate, root};

    CHECK(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru", -202, true, completeChain}));
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru.evil.example", -202, true, completeChain}));
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-statist.tinkoff.ru", -202, true, completeChain}));
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru", -200, true, completeChain}));
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru", -202, false, completeChain}));
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru", -202, true,
        std::span<const std::string>{completeChain.data(), 2}}));
    const std::vector<std::string> misplacedRoot{root, intermediate};
    CHECK_FALSE(is_trusted_tbank_certificate(TBankCertificateEvidence{
        "api-common.tinkoff.ru", -202, true, misplacedRoot}));
}

TEST_CASE("T-Bank login filters known renderer diagnostic noise") {
    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto pageSource = page.readAll();
    CHECK(pageSource.contains(QByteArrayLiteral(
        "onJavaScriptConsoleMessage")));
    CHECK(pageSource.contains(QByteArrayLiteral(
        "t-static.ruimagessolid/favicon.png")));
    CHECK(pageSource.contains(QByteArrayLiteral("GeolocationCollector:")));
    CHECK(pageSource.contains(QByteArrayLiteral(
        "Failed to create WebGPU Context Provider")));
    CHECK_FALSE(pageSource.contains(QByteArrayLiteral("console.warn(message)")));

    QFile mainSourceFile(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/main.cpp"));
    REQUIRE(mainSourceFile.open(QIODevice::ReadOnly));
    const auto mainSource = mainSourceFile.readAll();
    const auto privacyPolicy = mainSource.indexOf(QByteArrayLiteral(
        "--force-webrtc-ip-handling-policy=disable_non_proxied_udp"));
    const auto webEngineInitialization = mainSource.indexOf(
        QByteArrayLiteral("QtWebEngineQuick::initialize()"));
    REQUIRE(privacyPolicy >= 0);
    REQUIRE(webEngineInitialization >= 0);
    CHECK(privacyPolicy < webEngineInitialization);
}

TEST_CASE("T-Bank TLS diagnostics expose no URL secrets") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);
    const auto message = controller.reportTBankCertificateError(
        QUrl(QStringLiteral(
            "https://user:password@id.tbank.ru/private/login"
            "?psid=session-secret#otp")),
        -202, true);

    CHECK(message.contains(QStringLiteral("id.tbank.ru")));
    CHECK(message.contains(QStringLiteral("не является доверенным")));
    CHECK_FALSE(message.contains(QStringLiteral("user")));
    CHECK_FALSE(message.contains(QStringLiteral("password")));
    CHECK_FALSE(message.contains(QStringLiteral("private")));
    CHECK_FALSE(message.contains(QStringLiteral("session-secret")));
    CHECK_FALSE(message.contains(QStringLiteral("otp")));

    const auto deviceCollectorMessage =
        controller.reportTBankCertificateError(
            QUrl(QStringLiteral("https://mddc.tbank.ru/collect?token=secret")),
            -202, false);
    CHECK(deviceCollectorMessage.isEmpty());
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
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);
    const auto baselineCredentialCount = stored->values.size();
    REQUIRE(stored->values.contains(QStringLiteral("database-key")));
    const auto fixture = QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR
        "/tests/fixtures/zen_plugin_fixture.cjs");

    REQUIRE(controller.inspectZenPluginBundle(
        QUrl::fromLocalFile(fixture)));
    CHECK(controller.zenPluginId()
          == QStringLiteral("zen_plugin_fixture"));
    controller.setZenPluginAutoUpdate(true);
    CHECK(controller.zenPluginAutoUpdate());
    CHECK(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));
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
    CHECK(stored->values.size() == baselineCredentialCount + 1);
    CHECK(controller.zenPluginLastSync() !=
          QStringLiteral("Нет успешного обновления"));
    CHECK_FALSE(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));

    const auto savedPreferences =
        QSettings{}.value(QStringLiteral("zenPlugin/preferences"))
            .toByteArray();
    CHECK(savedPreferences.contains("fixture"));
    CHECK(savedPreferences.contains("startDate"));
    CHECK_FALSE(savedPreferences.contains("session-only-secret"));
    CHECK_FALSE(savedPreferences.contains("password"));
    CHECK_FALSE(controller.synchronizeZenPlugin());
    CHECK(controller.syncStatus().contains(QStringLiteral("Пароль")));

    controller.removeZenPlugin();
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(stored->values.size() == baselineCredentialCount);
    CHECK(stored->values.contains(QStringLiteral("database-key")));
    CHECK(controller.zenPluginId().isEmpty());
    CHECK_FALSE(controller.zenPluginAutoUpdate());
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
