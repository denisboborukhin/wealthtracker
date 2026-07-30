#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QHash>

#include <expected>
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

}  // namespace

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
