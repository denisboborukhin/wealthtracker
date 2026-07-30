module;
#include <array>
#include <string>

module wt.storage.migrations;

import wt.storage.transaction;

namespace wealthtracker::storage {
namespace {

constexpr std::string_view schema_v1 = R"sql(
CREATE TABLE institutions (
    id TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    country_code TEXT NOT NULL DEFAULT 'RU',
    created_at TEXT NOT NULL
);

CREATE TABLE connections (
    id TEXT PRIMARY KEY NOT NULL,
    institution_id TEXT NOT NULL REFERENCES institutions(id),
    connector_id TEXT NOT NULL,
    display_name TEXT NOT NULL,
    capabilities_json TEXT NOT NULL DEFAULT '[]',
    sync_cursor TEXT,
    created_at TEXT NOT NULL,
    last_synced_at TEXT,
    UNIQUE(connector_id, id)
);

CREATE TABLE accounts (
    id TEXT PRIMARY KEY NOT NULL,
    connection_id TEXT REFERENCES connections(id) ON DELETE SET NULL,
    institution_id TEXT NOT NULL REFERENCES institutions(id),
    external_id TEXT,
    name TEXT NOT NULL,
    account_type TEXT NOT NULL,
    currency TEXT NOT NULL CHECK(length(currency) = 3),
    is_closed INTEGER NOT NULL DEFAULT 0 CHECK(is_closed IN (0, 1)),
    opened_at TEXT,
    created_at TEXT NOT NULL,
    UNIQUE(connection_id, external_id)
);

CREATE TABLE import_batches (
    id TEXT PRIMARY KEY NOT NULL,
    connector_id TEXT NOT NULL,
    source_name TEXT NOT NULL,
    source_sha256 TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('running', 'committed', 'failed')),
    started_at TEXT NOT NULL,
    committed_at TEXT,
    warning_count INTEGER NOT NULL DEFAULT 0 CHECK(warning_count >= 0),
    UNIQUE(connector_id, source_sha256)
);

CREATE TABLE source_records (
    id TEXT PRIMARY KEY NOT NULL,
    import_batch_id TEXT NOT NULL REFERENCES import_batches(id) ON DELETE CASCADE,
    external_source_id TEXT,
    row_number INTEGER NOT NULL CHECK(row_number >= 0),
    canonical_hash TEXT NOT NULL,
    raw_payload_json TEXT NOT NULL,
    created_at TEXT NOT NULL,
    UNIQUE(import_batch_id, row_number)
);

CREATE TABLE balance_snapshots (
    id TEXT PRIMARY KEY NOT NULL,
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    source_record_id TEXT REFERENCES source_records(id),
    captured_at TEXT NOT NULL,
    amount_minor INTEGER NOT NULL,
    currency TEXT NOT NULL CHECK(length(currency) = 3),
    UNIQUE(account_id, captured_at)
);

CREATE TABLE ledger_transactions (
    id TEXT PRIMARY KEY NOT NULL,
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    source_record_id TEXT NOT NULL REFERENCES source_records(id),
    external_id TEXT,
    posted_on TEXT NOT NULL,
    occurred_at TEXT,
    amount_minor INTEGER NOT NULL,
    currency TEXT NOT NULL CHECK(length(currency) = 3),
    description TEXT NOT NULL,
    bank_category TEXT,
    transaction_kind TEXT NOT NULL DEFAULT 'regular',
    canonical_hash TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE UNIQUE INDEX ledger_transactions_external_id
ON ledger_transactions(account_id, external_id)
WHERE external_id IS NOT NULL;

CREATE INDEX ledger_transactions_account_date
ON ledger_transactions(account_id, posted_on DESC);

CREATE INDEX ledger_transactions_fingerprint
ON ledger_transactions(account_id, posted_on, amount_minor, currency, canonical_hash);

CREATE TABLE import_review_queue (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    import_batch_id TEXT NOT NULL REFERENCES import_batches(id) ON DELETE CASCADE,
    source_record_id TEXT NOT NULL REFERENCES source_records(id) ON DELETE CASCADE,
    possible_transaction_id TEXT NOT NULL REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    reason TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'pending'
        CHECK(status IN ('pending', 'keep_both', 'duplicate')),
    UNIQUE(source_record_id, possible_transaction_id)
);

CREATE TABLE categories (
    id TEXT PRIMARY KEY NOT NULL,
    parent_id TEXT REFERENCES categories(id) ON DELETE SET NULL,
    name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK(kind IN ('income', 'expense', 'transfer', 'investment')),
    color TEXT,
    is_system INTEGER NOT NULL DEFAULT 0 CHECK(is_system IN (0, 1))
);

CREATE TABLE transaction_overlays (
    transaction_id TEXT PRIMARY KEY NOT NULL REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    category_id TEXT REFERENCES categories(id) ON DELETE SET NULL,
    merchant_name TEXT,
    note TEXT,
    excluded_from_analytics INTEGER NOT NULL DEFAULT 0
        CHECK(excluded_from_analytics IN (0, 1)),
    updated_at TEXT NOT NULL
);

CREATE TABLE transaction_splits (
    id TEXT PRIMARY KEY NOT NULL,
    transaction_id TEXT NOT NULL REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    category_id TEXT REFERENCES categories(id) ON DELETE SET NULL,
    amount_minor INTEGER NOT NULL,
    note TEXT
);

CREATE TABLE transfer_pairs (
    id TEXT PRIMARY KEY NOT NULL,
    outgoing_transaction_id TEXT NOT NULL UNIQUE
        REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    incoming_transaction_id TEXT NOT NULL UNIQUE
        REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    status TEXT NOT NULL CHECK(status IN ('suggested', 'confirmed', 'rejected')),
    CHECK(outgoing_transaction_id <> incoming_transaction_id)
);

CREATE TABLE categorization_rules (
    id TEXT PRIMARY KEY NOT NULL,
    priority INTEGER NOT NULL,
    matcher_json TEXT NOT NULL,
    category_id TEXT NOT NULL REFERENCES categories(id),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at TEXT NOT NULL
);

CREATE TABLE tags (
    id TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL UNIQUE,
    color TEXT
);

CREATE TABLE transaction_tags (
    transaction_id TEXT NOT NULL REFERENCES ledger_transactions(id) ON DELETE CASCADE,
    tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY(transaction_id, tag_id)
);

CREATE TABLE instruments (
    id TEXT PRIMARY KEY NOT NULL,
    external_id TEXT NOT NULL UNIQUE,
    ticker TEXT,
    name TEXT NOT NULL,
    instrument_type TEXT NOT NULL,
    currency TEXT NOT NULL CHECK(length(currency) = 3)
);

CREATE TABLE investment_events (
    id TEXT PRIMARY KEY NOT NULL,
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    instrument_id TEXT REFERENCES instruments(id),
    source_record_id TEXT REFERENCES source_records(id),
    external_id TEXT,
    event_type TEXT NOT NULL,
    occurred_at TEXT NOT NULL,
    quantity_mantissa TEXT,
    quantity_scale INTEGER,
    amount_minor INTEGER,
    currency TEXT CHECK(currency IS NULL OR length(currency) = 3),
    UNIQUE(account_id, external_id)
);

CREATE TABLE position_snapshots (
    id TEXT PRIMARY KEY NOT NULL,
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    instrument_id TEXT NOT NULL REFERENCES instruments(id),
    captured_at TEXT NOT NULL,
    quantity_mantissa TEXT NOT NULL,
    quantity_scale INTEGER NOT NULL,
    UNIQUE(account_id, instrument_id, captured_at)
);

CREATE TABLE quotes (
    instrument_id TEXT NOT NULL REFERENCES instruments(id) ON DELETE CASCADE,
    quoted_at TEXT NOT NULL,
    price_mantissa TEXT NOT NULL,
    price_scale INTEGER NOT NULL,
    currency TEXT NOT NULL CHECK(length(currency) = 3),
    PRIMARY KEY(instrument_id, quoted_at)
);

CREATE TABLE fx_rates (
    base_currency TEXT NOT NULL CHECK(length(base_currency) = 3),
    quote_currency TEXT NOT NULL CHECK(length(quote_currency) = 3),
    rate_date TEXT NOT NULL,
    rate_mantissa TEXT NOT NULL,
    rate_scale INTEGER NOT NULL,
    source TEXT NOT NULL,
    PRIMARY KEY(base_currency, quote_currency, rate_date)
);

CREATE TABLE settings (
    key TEXT PRIMARY KEY NOT NULL,
    value_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
)sql";

constexpr std::string_view schema_v2 = R"sql(
CREATE INDEX investment_events_account_date
ON investment_events(account_id, occurred_at DESC);

CREATE INDEX balance_snapshots_account_date
ON balance_snapshots(account_id, captured_at DESC);
)sql";

constexpr std::array migrations{
    Migration{1, "initial encrypted ledger schema", schema_v1},
    Migration{2, "analytics indexes", schema_v2},
};

}  // namespace

MigrationRunner::MigrationRunner(Database& database) noexcept : database_(database) {}

std::expected<std::int64_t, Error> MigrationRunner::current_version() {
    auto create_result = database_.execute(R"sql(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
    )sql");
    if (!create_result) {
        return std::unexpected(create_result.error());
    }
    return database_.scalar_int64(
        "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;");
}

std::expected<void, Error> MigrationRunner::apply(std::span<const Migration> migrations_to_apply) {
    for (std::size_t index = 0; index < migrations_to_apply.size(); ++index) {
        const auto& migration = migrations_to_apply[index];
        if (migration.version <= 0 || migration.name.empty() || migration.sql.empty() ||
            (index > 0 && migration.version <= migrations_to_apply[index - 1].version)) {
            return std::unexpected(Error{
                ErrorCode::migration_error, 0, "migrations must be non-empty and strictly ordered"});
        }
    }

    auto version = current_version();
    if (!version) {
        return std::unexpected(version.error());
    }
    if (!migrations_to_apply.empty() &&
        *version > migrations_to_apply.back().version) {
        return std::unexpected(Error{
            ErrorCode::migration_error,
            0,
            "database schema is newer than this application supports"});
    }
    auto transaction = SqlTransaction::begin(database_, TransactionMode::exclusive);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }

    for (const auto& migration : migrations_to_apply) {
        if (migration.version <= *version) {
            continue;
        }
        if (auto result = database_.execute(migration.sql); !result) {
            auto error = result.error();
            error.code = ErrorCode::migration_error;
            error.message = "migration " + std::to_string(migration.version) + " (" +
                            std::string(migration.name) + ") failed: " + error.message;
            return std::unexpected(std::move(error));
        }

        auto statement = database_.prepare(
            "INSERT INTO schema_migrations(version, name) VALUES(?1, ?2);");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        if (auto result = statement->bind(1, migration.version); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->bind(2, migration.name); !result) {
            return std::unexpected(result.error());
        }
        auto insert_result = statement->step();
        if (!insert_result) {
            return std::unexpected(insert_result.error());
        }
    }
    return transaction->commit();
}

std::span<const Migration> default_migrations() noexcept {
    return migrations;
}

}  // namespace wealthtracker::storage
