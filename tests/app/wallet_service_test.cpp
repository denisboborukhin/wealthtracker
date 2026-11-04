#include "app/CredentialBackend.hpp"
#include "app/AppController.hpp"
#include "../support/QtTestSupport.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <memory>

import wt.wallet_service;
import wt.history_store;

namespace {
class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString& key, const QByteArray& value) override {
        values[key] = value; return {};
    }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        if (values.contains(key)) return std::optional<QByteArray>{values[key]};
        return std::optional<QByteArray>{};
    }
    std::expected<void, QString> remove(const QString& key) override { values.remove(key); return {}; }
    QHash<QString, QByteArray> values;
};

class PendingReply final : public QNetworkReply {
public:
    PendingReply(const QNetworkRequest& request, int& aborts, QObject* parent)
        : QNetworkReply(parent), aborts_(aborts) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }
    void abort() override { ++aborts_; }
protected:
    qint64 readData(char*, qint64) override { return 0; }
private:
    int& aborts_;
};

class PendingNetwork final : public QNetworkAccessManager {
public:
    int requests = 0;
    int aborts = 0;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        return new PendingReply(request, aborts, this);
    }
};
}

TEST_CASE("Wallet service cancels owned work and discards its pending wallet queue") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    Credentials credentials;
    wealthtracker::app::HistoryStore history;
    history.initialize(directory.filePath("history.db"), &credentials, [](const auto&) {});
    REQUIRE(history.ready());
    REQUIRE(history.saveWallet("0x1111111111111111111111111111111111111111", "First"));
    REQUIRE(history.saveWallet("0x2222222222222222222222222222222222222222", "Second"));
    PendingNetwork network;
    int notifications = 0;
    int historyNotifications = 0;
    {
        wealthtracker::app::WalletService service(network, history);
        service.setNotifications([&] { ++notifications; }, [&] { ++historyNotifications; });
        REQUIRE(service.refresh());
        REQUIRE(service.running());
        REQUIRE(network.requests == 1);
        CHECK_FALSE(service.refresh());
        service.stop();
        CHECK_FALSE(service.running());
        CHECK(network.aborts == 1);
        QCoreApplication::processEvents();
        CHECK(network.requests == 1);
        CHECK(historyNotifications == 0);
        CHECK(history.captures().empty());
        REQUIRE(service.refresh());
        CHECK(network.requests == 2);
    }
    CHECK(network.aborts == 2);
    CHECK(notifications == 3); // start, stop, restart; destruction is quiet.
    QCoreApplication::processEvents();
    CHECK(network.requests == 2);
    CHECK(historyNotifications == 0);
}

TEST_CASE("Wallet service presents saved valuations without fabricating token details") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    Credentials credentials;
    wealthtracker::app::HistoryStore history;
    history.initialize(directory.filePath("history.db"), &credentials, [](const auto&) {});
    REQUIRE(history.ready());
    const auto address = QStringLiteral("0x1111111111111111111111111111111111111111");
    REQUIRE(history.saveWallet(address, "Wallet"));
    REQUIRE(history.append("ethereum:" + address.toStdString(), "EVM · Wallet",
        "2026-11-04T12:00:00.000Z", {{address.toStdString(),"Wallet", "USDT",12345}}));
    PendingNetwork network;
    wealthtracker::app::WalletService service(network, history);
    const auto rows = service.rows();
    REQUIRE(rows.size() == 1);
    const auto row = rows.front().toMap();
    CHECK(row["value"].toString() == "123.45 USDT");
    CHECK(row["holdings"].toList().isEmpty());
    CHECK_FALSE(row["lastSync"].toString().isEmpty());
    CHECK(network.requests == 0);
    REQUIRE(service.remove(address));
    CHECK(service.rows().isEmpty());
    CHECK(history.captures().size() == 1);
}

TEST_CASE("Wallet add rechecks privacy after notifying saved configuration") {
    wealthtracker::test::SettingsScope settings("WalletServiceTests", "PrivacyReentry");
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    auto network = std::make_unique<PendingNetwork>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(std::make_unique<Credentials>(),
        std::move(network), nullptr, directory.filePath("history.db"));
    QObject::connect(&controller, &wealthtracker::app::AppController::cryptoWalletsChanged,
        &controller, [&] { controller.lock(); });
    REQUIRE(controller.addCryptoWallet("Wallet", "0x1111111111111111111111111111111111111111"));
    CHECK_FALSE(controller.cryptoSyncRunning());
    CHECK(observed->requests == 0);
    CHECK(controller.cryptoWallets().size() == 1);
}
