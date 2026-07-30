#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

import wt.storage.database;
import wt.storage.import_session;
import wt.storage.migrations;
import wt.storage.repository;
import wt.storage.transaction;

namespace {

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("wealthtracker-storage-test-" + std::to_string(suffix) + ".sqlite");
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

wealthtracker::storage::ImportBatchDraft batch(std::string id, std::string hash) {
    return {
        .id = std::move(id),
        .connector_id = "test.csv",
        .source_name = "statement.csv",
        .source_sha256 = std::move(hash),
        .started_at = "2026-07-30T00:00:00Z",
    };
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

TEST_CASE("uncommitted import is rolled back atomically", "[storage][import]") {
    TemporaryDatabase temporary;
    auto database = open_test_database(temporary.path());
    REQUIRE(database.has_value());
    wealthtracker::storage::MigrationRunner runner(*database);
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());

    {
        auto session = wealthtracker::storage::ImportSession::begin(*database, batch("batch-1", "hash-1"));
        REQUIRE(session.has_value());
        REQUIRE(session->upsert_institution({
            .id = "bank",
            .name = "Test Bank",
            .country_code = "RU",
            .created_at = "2026-07-30T00:00:00Z",
        }).has_value());
    }

    auto count = database->scalar_int64("SELECT COUNT(*) FROM institutions;");
    REQUIRE(count.has_value());
    REQUIRE(*count == 0);
}

TEST_CASE("import detects source IDs exactly and fingerprints conservatively", "[storage][import]") {
    TemporaryDatabase temporary;
    auto database = open_test_database(temporary.path());
    REQUIRE(database.has_value());
    wealthtracker::storage::MigrationRunner runner(*database);
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());

    auto first = wealthtracker::storage::ImportSession::begin(*database, batch("batch-1", "hash-1"));
    REQUIRE(first.has_value());
    REQUIRE(first->upsert_institution({
        .id = "bank",
        .name = "Test Bank",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    REQUIRE(first->upsert_account({
        .id = "account",
        .institution_id = "bank",
        .name = "Card",
        .account_type = "card",
        .currency = "RUB",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    REQUIRE(first->add_source_record({
        .id = "source-1",
        .row_number = 1,
        .canonical_hash = "fingerprint",
        .raw_payload_json = R"({"row":1})",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    wealthtracker::storage::LedgerTransactionDraft original{
        .id = "transaction-1",
        .account_id = "account",
        .source_record_id = "source-1",
        .external_id = "bank-id-1",
        .posted_on = "2026-07-29",
        .amount_minor = -12345,
        .currency = "RUB",
        .description = "Purchase",
        .canonical_hash = "fingerprint",
        .created_at = "2026-07-30T00:00:00Z",
    };
    auto inserted = first->add_transaction(original);
    REQUIRE(inserted.has_value());
    REQUIRE(inserted->status == wealthtracker::storage::ImportInsertStatus::inserted);
    REQUIRE(first->commit("2026-07-30T00:01:00Z", 0).has_value());

    auto second = wealthtracker::storage::ImportSession::begin(*database, batch("batch-2", "hash-2"));
    REQUIRE(second.has_value());
    REQUIRE(second->add_source_record({
        .id = "source-2",
        .row_number = 1,
        .canonical_hash = "fingerprint",
        .raw_payload_json = R"({"row":1})",
        .created_at = "2026-07-30T00:02:00Z",
    }).has_value());
    original.id = "transaction-2";
    original.source_record_id = "source-2";
    auto duplicate = second->add_transaction(original);
    REQUIRE(duplicate.has_value());
    REQUIRE(duplicate->status == wealthtracker::storage::ImportInsertStatus::exact_duplicate);
    REQUIRE(duplicate->possible_existing_id == "transaction-1");
    REQUIRE(second->commit("2026-07-30T00:03:00Z", 0).has_value());

    auto third = wealthtracker::storage::ImportSession::begin(*database, batch("batch-3", "hash-3"));
    REQUIRE(third.has_value());
    REQUIRE(third->add_source_record({
        .id = "source-3",
        .row_number = 1,
        .canonical_hash = "fingerprint",
        .raw_payload_json = R"({"row":1})",
        .created_at = "2026-07-30T00:04:00Z",
    }).has_value());
    original.id = "transaction-3";
    original.source_record_id = "source-3";
    original.external_id = std::nullopt;
    auto ambiguous = third->add_transaction(original);
    REQUIRE(ambiguous.has_value());
    REQUIRE(ambiguous->status == wealthtracker::storage::ImportInsertStatus::needs_review);
}

TEST_CASE("ledger repository stores user edits as overlays", "[storage][repository]") {
    TemporaryDatabase temporary;
    auto database = open_test_database(temporary.path());
    REQUIRE(database.has_value());
    wealthtracker::storage::MigrationRunner runner(*database);
    REQUIRE(runner.apply(wealthtracker::storage::default_migrations()).has_value());

    auto session = wealthtracker::storage::ImportSession::begin(*database, batch("batch-1", "hash-1"));
    REQUIRE(session.has_value());
    REQUIRE(session->upsert_institution({
        .id = "bank", .name = "Bank", .created_at = "2026-07-30T00:00:00Z"}).has_value());
    REQUIRE(session->upsert_account({
        .id = "account",
        .institution_id = "bank",
        .name = "Card",
        .account_type = "card",
        .currency = "RUB",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    REQUIRE(session->add_source_record({
        .id = "source",
        .row_number = 1,
        .canonical_hash = "hash",
        .raw_payload_json = "{}",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    REQUIRE(session->add_transaction({
        .id = "transaction",
        .account_id = "account",
        .source_record_id = "source",
        .posted_on = "2026-07-30",
        .amount_minor = -100,
        .currency = "RUB",
        .description = "Coffee",
        .canonical_hash = "hash",
        .created_at = "2026-07-30T00:00:00Z",
    }).has_value());
    REQUIRE(session->commit("2026-07-30T00:01:00Z", 0).has_value());

    wealthtracker::storage::LedgerRepository repository(*database);
    REQUIRE(repository.set_transaction_overlay("transaction", {
        .merchant_name = "Coffee shop",
        .note = "Morning",
        .excluded_from_analytics = true,
        .updated_at = "2026-07-30T00:02:00Z",
    }).has_value());

    auto transactions =
        repository.transactions("account", "2026-07-01", "2026-07-31");
    REQUIRE(transactions.has_value());
    REQUIRE(transactions->size() == 1);
    CHECK(transactions->front().merchant_name == "Coffee shop");
    CHECK(transactions->front().excluded_from_analytics);
}
