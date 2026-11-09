#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

import wt.storage.database;
import wt.storage.migrations;

namespace {

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        std::random_device entropy;
        path_ = std::filesystem::temp_directory_path() /
                ("wealthtracker-storage-test-" + std::to_string(suffix) + "-" +
                 std::to_string(entropy()) + "-" + std::to_string(entropy()) + ".sqlite");
    }

    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::expected<wealthtracker::storage::Database, wealthtracker::storage::Error> open_test_database(
    const std::filesystem::path& path) {
    constexpr std::array<std::byte, 32> key{};
    return wealthtracker::storage::Database::open(
        path, key, wealthtracker::storage::OpenOptions{.create_if_missing = true, .require_cipher = false});
}

}  // namespace

TEST_CASE("default migrations are idempotent", "[storage][migrations]") {
    TemporaryDatabase temporary;
    auto database = open_test_database(temporary.path());
    REQUIRE(database.has_value());

    wealthtracker::storage::MigrationRunner runner(*database);
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());

    auto version = runner.current_version();
    REQUIRE(version.has_value());
    REQUIRE(*version == 2);
}

TEST_CASE("newer database schemas fail closed", "[storage][migrations]") {
    TemporaryDatabase temporary;
    auto database = open_test_database(temporary.path());
    REQUIRE(database.has_value());
    wealthtracker::storage::MigrationRunner runner(*database);
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());
    REQUIRE(database->execute(
        "INSERT INTO schema_migrations(version, name) VALUES(999, 'future');").has_value());

    auto result = runner.apply(wealthtracker::storage::default_migrations());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == wealthtracker::storage::ErrorCode::migration_error);
}

TEST_CASE("SQLCipher database rejects a wrong key and hides schema text", "[storage][crypto]") {
    TemporaryDatabase temporary;
    std::array<std::byte, 32> key{};
    key.fill(std::byte{0x31});
    {
        auto database = wealthtracker::storage::Database::open(temporary.path(), key);
        REQUIRE(database);
        wealthtracker::storage::MigrationRunner runner(*database);
        REQUIRE(runner.apply(wealthtracker::storage::default_migrations()));
    }

    std::ifstream encrypted(temporary.path(), std::ios::binary);
    REQUIRE(encrypted);
    const std::string bytes{
        std::istreambuf_iterator<char>{encrypted}, std::istreambuf_iterator<char>{}};
    CHECK_FALSE(bytes.contains("ledger_transactions"));

    std::array<std::byte, 32> wrong_key{};
    wrong_key.fill(std::byte{0x32});
    const auto reopened = wealthtracker::storage::Database::open(
        temporary.path(), wrong_key,
        wealthtracker::storage::OpenOptions{.create_if_missing = false});
    REQUIRE_FALSE(reopened);
}
