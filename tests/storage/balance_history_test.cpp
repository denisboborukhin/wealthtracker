#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import wt.storage.balance_history;

namespace {

class TemporaryHistory final {
public:
    TemporaryHistory() {
        std::random_device entropy;
        path = std::filesystem::temp_directory_path() /
               ("wealthtracker-history-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(entropy()) + ".sqlite");
        key.fill(std::byte{0x42});
    }

    ~TemporaryHistory() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(path.string() + "-wal", ignored);
        std::filesystem::remove(path.string() + "-shm", ignored);
    }

    std::filesystem::path path;
    std::array<std::byte, 32> key{};
};

using wealthtracker::storage::BalanceHistoryRepository;
using wealthtracker::storage::Database;
using wealthtracker::storage::HistoryBalance;

constexpr std::string_view captured_at = "2026-10-21T12:00:00.123Z";
constexpr std::string_view wallet_address = "0x0123456789abcdef0123456789abcdef01234567";

void create_v1_history(Database& database) {
    REQUIRE(database.execute(R"sql(
        CREATE TABLE captures (
            id INTEGER PRIMARY KEY,
            connection_id TEXT NOT NULL CHECK(length(connection_id) > 0),
            institution TEXT NOT NULL CHECK(length(institution) > 0),
            captured_at TEXT NOT NULL CHECK(length(captured_at) > 0)
        );
        CREATE INDEX captures_date ON captures(captured_at, id);
        CREATE TABLE balances (
            capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
            account_id TEXT NOT NULL CHECK(length(account_id) > 0),
            name TEXT NOT NULL,
            currency TEXT NOT NULL CHECK(currency GLOB '[A-Z][A-Z][A-Z]'),
            amount_minor INTEGER NOT NULL,
            PRIMARY KEY(capture_id, account_id, currency)
        );
        INSERT INTO captures VALUES(7, 'bank', 'Bank', '2026-10-21T12:00:00.123Z');
        INSERT INTO captures VALUES(8, 'bank', 'Bank', '2026-10-21T13:00:00.123Z');
        INSERT INTO balances VALUES(7, 'a', 'Original', 'RUB', 9223372036854775807);
        INSERT INTO balances VALUES(7, 'b', 'Debt', 'USD', -9223372036854775808);
        PRAGMA user_version = 1;
    )sql"));
}

}  // namespace

TEST_CASE("encrypted balance history survives reopening with exact amounts", "[storage][history]") {
    TemporaryHistory temporary;
    const std::vector<HistoryBalance> balances{
        {"account-high", "Private balance history account", "RUB",
         std::numeric_limits<std::int64_t>::max()},
        {"account-low", "Foreign account", "USD", std::numeric_limits<std::int64_t>::min()},
    };
    {
        auto database = Database::open(temporary.path, temporary.key);
        REQUIRE(database);
        BalanceHistoryRepository repository(*database);
        REQUIRE(repository.initialize());
        REQUIRE(repository.initialize());
        REQUIRE(repository.append("bank", "Bank", captured_at, balances));
    }
    std::ifstream encrypted(temporary.path, std::ios::binary);
    REQUIRE(encrypted);
    const std::string bytes{
        std::istreambuf_iterator<char>{encrypted}, std::istreambuf_iterator<char>{}};
    CHECK_FALSE(bytes.starts_with("SQLite format 3"));
    CHECK_FALSE(bytes.contains("Private balance history account"));
    encrypted.close();

    auto database = Database::open(temporary.path, temporary.key,
                                  {.create_if_missing = false});
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    const auto captures = repository.captures();
    REQUIRE(captures);
    REQUIRE(captures->size() == 1);
    CHECK(captures->front().connection_id == "bank");
    CHECK(captures->front().institution == "Bank");
    CHECK(captures->front().captured_at == captured_at);
    REQUIRE(captures->front().balances.size() == 2);
    CHECK(captures->front().balances[0].amount_minor == std::numeric_limits<std::int64_t>::max());
    CHECK(captures->front().balances[1].amount_minor == std::numeric_limits<std::int64_t>::min());
    CHECK(captures->front().balances[1].currency == "USD");
}

TEST_CASE("balance history rejects wrong keys and preserves existing captures", "[storage][history]") {
    TemporaryHistory temporary;
    {
        auto database = Database::open(temporary.path, temporary.key);
        REQUIRE(database);
        BalanceHistoryRepository repository(*database);
        REQUIRE(repository.initialize());
        REQUIRE(repository.append("bank", "Bank", captured_at, {}));
    }
    auto wrong_key = temporary.key;
    wrong_key[0] = std::byte{0x43};
    REQUIRE_FALSE(Database::open(temporary.path, wrong_key, {.create_if_missing = false}));
    auto database = Database::open(temporary.path, temporary.key, {.create_if_missing = false});
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    const auto captures = repository.captures();
    REQUIRE(captures);
    REQUIRE(captures->size() == 1);
}

TEST_CASE("duplicate history accounts roll back the entire capture", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    const std::vector<HistoryBalance> duplicate{
        {"account", "Account", "RUB", 10}, {"account", "Duplicate", "RUB", 20}};
    REQUIRE_FALSE(repository.append("bank", "Bank", captured_at, duplicate));
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM captures;") == 0);
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM balances;") == 0);
    REQUIRE(repository.append("bank", "Bank", captured_at, {}));
}

TEST_CASE("history isolates connectors and keeps same-time and empty captures", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    const std::vector<HistoryBalance> balances{
        {"same-account", "Account", "RUB", 100},
        {"same-account", "Account", "USD", 200},
    };
    const auto first = repository.append("bank-a", "Bank A", captured_at, balances);
    const auto second = repository.append("bank-b", "Bank B", captured_at, balances);
    const auto empty = repository.append("bank-a", "Bank A", captured_at, {});
    const auto earlier = repository.append("bank-b", "Bank B", "2026-10-20T12:00:00.000Z", {});
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(empty);
    REQUIRE(earlier);
    CHECK(*first < *second);
    CHECK(*second < *empty);
    const auto captures = repository.captures();
    REQUIRE(captures);
    REQUIRE(captures->size() == 4);
    CHECK((*captures)[0].id == *earlier);
    CHECK((*captures)[1].id == *first);
    CHECK((*captures)[2].id == *second);
    CHECK((*captures)[3].id == *empty);
    CHECK((*captures)[1].connection_id == "bank-a");
    CHECK((*captures)[2].connection_id == "bank-b");
    CHECK((*captures)[1].balances.size() == 2);
    CHECK((*captures)[2].balances.size() == 2);
    CHECK((*captures)[3].balances.empty());
}

TEST_CASE("balance history validates identifiers and currencies before saving", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    REQUIRE_FALSE(repository.append("", "Bank", captured_at, {}));
    REQUIRE_FALSE(repository.append("bank", "", captured_at, {}));
    REQUIRE_FALSE(repository.append("bank", "Bank", "", {}));
    for (const auto& invalid : std::vector<HistoryBalance>{
             {"", "Account", "RUB", 1}, {"account", "Account", "rub", 1},
             {"account", "Account", "RU", 1}, {"account", "Account", "RUBX", 1},
             {"account", "Account", "R1B", 1}}) {
        REQUIRE_FALSE(repository.append("bank", "Bank", captured_at,
                                        std::span<const HistoryBalance>(&invalid, 1)));
    }
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM captures;") == 0);
}

TEST_CASE("balance history rejects a newer schema without rewriting it", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    REQUIRE(database->execute("PRAGMA user_version = 3;"));
    const auto result = repository.initialize();
    REQUIRE_FALSE(result);
    CHECK(result.error().code == wealthtracker::storage::ErrorCode::migration_error);
    CHECK(database->scalar_int64("PRAGMA user_version;") == 3);
}

TEST_CASE("v1 encrypted history migrates without changing captures", "[storage][history]") {
    TemporaryHistory temporary;
    {
        auto database = Database::open(temporary.path, temporary.key);
        REQUIRE(database);
        create_v1_history(*database);
    }
    {
        auto database = Database::open(temporary.path, temporary.key, {.create_if_missing = false});
        REQUIRE(database);
        BalanceHistoryRepository repository(*database);
        REQUIRE(repository.initialize());
        CHECK(database->scalar_int64("PRAGMA user_version;") == 2);
        const auto captures = repository.captures();
        REQUIRE(captures);
        REQUIRE(captures->size() == 2);
        CHECK(captures->front().id == 7);
        CHECK(captures->front().connection_id == "bank");
        CHECK(captures->front().institution == "Bank");
        CHECK(captures->front().captured_at == captured_at);
        REQUIRE(captures->front().balances.size() == 2);
        CHECK(captures->front().balances[0].name == "Original");
        CHECK(captures->front().balances[0].amount_minor == std::numeric_limits<std::int64_t>::max());
        CHECK(captures->front().balances[1].amount_minor == std::numeric_limits<std::int64_t>::min());
        CHECK(captures->back().id == 8);
        CHECK(captures->back().balances.empty());
        const std::vector<HistoryBalance> crypto{{"portfolio", "Wallet", "USDT", 12345}};
        REQUIRE(repository.append("ethereum", "Ethereum", "2026-10-22T00:00:00.000Z", crypto));
        REQUIRE(repository.save_wallet(wallet_address, "Public wallet"));
    }
    auto database = Database::open(temporary.path, temporary.key, {.create_if_missing = false});
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    const auto captures = repository.captures();
    REQUIRE(captures);
    REQUIRE(captures->size() == 3);
    REQUIRE(captures->back().balances.size() == 1);
    CHECK(captures->back().balances.front().currency == "USDT");
    CHECK(captures->back().balances.front().amount_minor == 12345);
    const auto wallets = repository.wallets();
    REQUIRE(wallets);
    REQUIRE(wallets->size() == 1);
    CHECK(wallets->front().address == wallet_address);
    CHECK(wallets->front().name == "Public wallet");
}

TEST_CASE("failed history migration rolls back schema and balances", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    create_v1_history(*database);
    REQUIRE(database->execute("CREATE TABLE crypto_wallets (unrelated TEXT);"));
    BalanceHistoryRepository repository(*database);
    REQUIRE_FALSE(repository.initialize());
    CHECK(database->scalar_int64("PRAGMA user_version;") == 1);
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM balances;") == 2);
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM captures;") == 2);
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM sqlite_master WHERE name='balances_v2';") == 0);
    REQUIRE_FALSE(database->execute("INSERT INTO balances VALUES(8, 'crypto', 'Wallet', 'USDT', 10);"));
}

TEST_CASE("wallet configuration updates and removal preserve captured history", "[storage][history]") {
    TemporaryHistory temporary;
    {
        auto database = Database::open(temporary.path, temporary.key);
        REQUIRE(database);
        BalanceHistoryRepository repository(*database);
        REQUIRE(repository.initialize());
        REQUIRE(repository.save_wallet(wallet_address, "First"));
        REQUIRE(repository.save_wallet(wallet_address, "Renamed"));
        REQUIRE(repository.save_wallet(wallet_address, "Renamed"));
        const std::vector<HistoryBalance> balances{{"portfolio", "Renamed", "USDT", 9876}};
        REQUIRE(repository.append(wallet_address, "Ethereum", captured_at, balances));
    }
    auto database = Database::open(temporary.path, temporary.key, {.create_if_missing = false});
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    const auto wallets = repository.wallets();
    REQUIRE(wallets);
    REQUIRE(wallets->size() == 1);
    CHECK(wallets->front().name == "Renamed");
    REQUIRE(repository.remove_wallet(wallet_address));
    REQUIRE(repository.remove_wallet(wallet_address));
    const auto removed = repository.wallets();
    REQUIRE(removed);
    CHECK(removed->empty());
    const auto captures = repository.captures();
    REQUIRE(captures);
    REQUIRE(captures->size() == 1);
    CHECK(captures->front().balances.front().amount_minor == 9876);
}

TEST_CASE("wallet configuration rejects malformed identifiers and names", "[storage][history]") {
    TemporaryHistory temporary;
    auto database = Database::open(temporary.path, temporary.key);
    REQUIRE(database);
    BalanceHistoryRepository repository(*database);
    REQUIRE(repository.initialize());
    for (const auto address : {std::string_view{}, std::string_view{"0x123"},
             std::string_view{"0X0123456789abcdef0123456789abcdef01234567"},
             std::string_view{"0x0123456789ABCDEF0123456789abcdef01234567"},
             std::string_view{"0xg123456789abcdef0123456789abcdef01234567"},
             std::string_view{"0x0123456789abcdef0123456789abcdef012345678"}}) {
        CHECK_FALSE(repository.save_wallet(address, "Wallet"));
        CHECK_FALSE(repository.remove_wallet(address));
    }
    CHECK_FALSE(repository.save_wallet(wallet_address, ""));
    CHECK_FALSE(repository.save_wallet(wallet_address, std::string(81, 'a')));
    CHECK_FALSE(repository.save_wallet(wallet_address, std::string_view{"ab\0cd", 5}));
    REQUIRE(repository.save_wallet(wallet_address, std::string(80, 'a')));
    CHECK(database->scalar_int64("SELECT COUNT(*) FROM crypto_wallets;") == 1);
}
