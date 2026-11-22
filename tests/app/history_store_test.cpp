#include "app/CredentialBackend.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>

import wt.history_store;
import wt.connectors;

namespace {
class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString& key, const QByteArray& bytes) override {
        ++writes; values[key] = bytes; return {};
    }
    std::expected<std::optional<QByteArray>,QString> get(const QString& key) override {
        ++reads;
        if (values.contains(key)) return std::optional<QByteArray>{values[key]};
        return std::optional<QByteArray>{};
    }
    std::expected<void,QString> remove(const QString& key) override { values.remove(key); return {}; }
    QHash<QString,QByteArray> values;
    int reads = 0;
    int writes = 0;
};
}

TEST_CASE("History store opt out never accesses credentials and missing keys preserve existing files") {
    Credentials credentials;
    wealthtracker::app::HistoryStore store;
    store.initialize({}, &credentials, [](const auto&) {});
    CHECK_FALSE(store.ready());
    CHECK(store.error().isEmpty());
    CHECK(credentials.reads == 0);
    CHECK(credentials.writes == 0);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("existing.db");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("existing bytes");
    file.close();
    store.initialize(path, &credentials, [](const auto&) {});
    CHECK_FALSE(store.ready());
    CHECK_FALSE(store.error().isEmpty());
    CHECK(credentials.writes == 0);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == QByteArrayLiteral("existing bytes"));
}

TEST_CASE("History store owns atomic captures and wallet configuration across restart") {
    Credentials credentials;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("history.db");
    {
        wealthtracker::app::HistoryStore store;
        store.initialize(path, &credentials, [](const auto&) {});
        REQUIRE(store.ready());
        REQUIRE(store.saveWallet("0x1111111111111111111111111111111111111111", "Wallet"));
        wealthtracker::connectors::NormalizedBatch batch;
        batch.accounts.push_back({"account", "Account", "current", "RUB", {}, false, {}});
        CHECK_FALSE(store.appendBank("bank", "Bank", batch));
        CHECK(store.captures().empty());
        batch.balances.push_back({"account",QDateTime::currentDateTimeUtc(),{12345,"RUB"},{}});
        REQUIRE(store.appendBank("bank", "Bank", batch));
        REQUIRE(store.captures().size() == 1);
        CHECK(store.captures().front().balances.front().amount_minor == 12345);
        CHECK(store.error().isEmpty());
        REQUIRE(store.removeWallet("0x1111111111111111111111111111111111111111"));
        CHECK(store.wallets().empty());
        CHECK(store.captures().size() == 1);
    }
    wealthtracker::app::HistoryStore restored;
    restored.initialize(path, &credentials, [](const auto&) {});
    REQUIRE(restored.ready());
    CHECK(restored.wallets().empty());
    REQUIRE(restored.captures().size() == 1);
    CHECK(restored.captures().front().balances.front().amount_minor == 12345);
    CHECK(credentials.writes == 1);
}
