module;

#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

module wt.storage.balance_history;

import wt.storage.transaction;

namespace wealthtracker::storage {

namespace {
bool valid_wallet_address(const std::string_view address) {
    return address.size() == 42 && address.starts_with("0x") &&
        std::all_of(address.begin() + 2, address.end(), [](const char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        });
}
}  // namespace

BalanceHistoryRepository::BalanceHistoryRepository(Database& database) noexcept
    : database_(database) {}

std::expected<void, Error> BalanceHistoryRepository::initialize() {
    auto transaction = SqlTransaction::begin(database_, TransactionMode::exclusive);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto version = database_.scalar_int64("PRAGMA user_version;");
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version < 0 || *version > 2) {
        return std::unexpected(Error{
            ErrorCode::migration_error, 0, "unsupported balance history schema version"});
    }
    if (*version == 0) {
        const auto result = database_.execute(R"sql(
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
            PRAGMA user_version = 1;
        )sql");
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    if (*version <= 1) {
        const auto result = database_.execute(R"sql(
            CREATE TABLE balances_v2 (
                capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
                account_id TEXT NOT NULL CHECK(length(account_id) > 0),
                name TEXT NOT NULL,
                currency TEXT NOT NULL CHECK(currency GLOB '[A-Z][A-Z][A-Z]' OR currency = 'USDT'),
                amount_minor INTEGER NOT NULL,
                PRIMARY KEY(capture_id, account_id, currency)
            );
            INSERT INTO balances_v2 SELECT capture_id, account_id, name, currency, amount_minor FROM balances;
            DROP TABLE balances;
            ALTER TABLE balances_v2 RENAME TO balances;
            CREATE TABLE crypto_wallets (
                address TEXT PRIMARY KEY NOT NULL CHECK(length(address) = 42
                    AND substr(address, 1, 2) = '0x'
                    AND substr(address, 3) NOT GLOB '*[^0-9a-f]*'),
                name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 80)
            );
            PRAGMA user_version = 2;
        )sql");
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return transaction->commit();
}

std::expected<std::int64_t, Error> BalanceHistoryRepository::append(
    std::string_view connection_id,
    std::string_view institution,
    std::string_view captured_at,
    std::span<const HistoryBalance> balances) {
    if (connection_id.empty() || institution.empty() || captured_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid balance capture metadata"});
    }
    for (const auto& balance : balances) {
        if (balance.account_id.empty() || (balance.currency != "USDT" && (balance.currency.size() != 3 ||
            !std::all_of(balance.currency.begin(), balance.currency.end(),
                         [](char value) { return value >= 'A' && value <= 'Z'; })))) {
            return std::unexpected(
                Error{ErrorCode::invalid_argument, 0, "invalid history balance"});
        }
    }
    auto transaction = SqlTransaction::begin(database_);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto capture = database_.prepare(
        "INSERT INTO captures(connection_id, institution, captured_at) VALUES(?1, ?2, ?3);");
    if (!capture) {
        return std::unexpected(capture.error());
    }
    if (auto result = capture->bind(1, connection_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = capture->bind(2, institution); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = capture->bind(3, captured_at); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = capture->step(); !result) {
        return std::unexpected(result.error());
    }
    const auto id = database_.last_insert_row_id();
    auto statement = database_.prepare(R"sql(
        INSERT INTO balances(capture_id, account_id, name, currency, amount_minor)
        VALUES(?1, ?2, ?3, ?4, ?5);
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    for (const auto& balance : balances) {
        if (auto result = statement->bind(1, id); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->bind(2, balance.account_id); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->bind(3, balance.name); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->bind(4, balance.currency); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->bind(5, balance.amount_minor); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->step(); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = statement->reset(); !result) {
            return std::unexpected(result.error());
        }
    }
    if (auto result = transaction->commit(); !result) {
        return std::unexpected(result.error());
    }
    return id;
}

std::expected<std::vector<BalanceCapture>, Error> BalanceHistoryRepository::captures() {
    auto statement = database_.prepare(R"sql(
        SELECT c.id, c.connection_id, c.institution, c.captured_at,
               b.account_id, b.name, b.currency, b.amount_minor
        FROM captures AS c
        LEFT JOIN balances AS b ON b.capture_id = c.id
        ORDER BY c.captured_at, c.id, b.account_id, b.currency;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    std::vector<BalanceCapture> result;
    while (true) {
        const auto step = statement->step();
        if (!step) {
            return std::unexpected(step.error());
        }
        if (*step == StepResult::done) {
            break;
        }
        const auto id = statement->column_int64(0);
        if (result.empty() || result.back().id != id) {
            result.push_back({id, statement->column_text(1), statement->column_text(2),
                              statement->column_text(3), {}});
        }
        if (!statement->column_is_null(4)) {
            result.back().balances.push_back({
                statement->column_text(4), statement->column_text(5),
                statement->column_text(6), statement->column_int64(7)});
        }
    }
    return result;
}

std::expected<std::vector<CryptoWallet>, Error> BalanceHistoryRepository::wallets() {
    auto statement = database_.prepare("SELECT address, name FROM crypto_wallets ORDER BY address;");
    if (!statement) return std::unexpected(statement.error());
    std::vector<CryptoWallet> result;
    while (true) {
        const auto step = statement->step();
        if (!step) return std::unexpected(step.error());
        if (*step == StepResult::done) break;
        result.push_back({statement->column_text(0), statement->column_text(1)});
    }
    return result;
}

std::expected<void, Error> BalanceHistoryRepository::save_wallet(
    const std::string_view address, const std::string_view name) {
    if (!valid_wallet_address(address) || name.empty() || name.size() > 80 ||
        name.find('\0') != std::string_view::npos) {
        return std::unexpected(Error{ErrorCode::invalid_argument, 0, "invalid crypto wallet"});
    }
    auto statement = database_.prepare(
        "INSERT INTO crypto_wallets(address, name) VALUES(?1, ?2) "
        "ON CONFLICT(address) DO UPDATE SET name = excluded.name;");
    if (!statement) return std::unexpected(statement.error());
    if (auto result = statement->bind(1, address); !result) return std::unexpected(result.error());
    if (auto result = statement->bind(2, name); !result) return std::unexpected(result.error());
    if (auto result = statement->step(); !result) return std::unexpected(result.error());
    return {};
}

std::expected<void, Error> BalanceHistoryRepository::remove_wallet(const std::string_view address) {
    if (!valid_wallet_address(address)) {
        return std::unexpected(Error{ErrorCode::invalid_argument, 0, "invalid crypto wallet address"});
    }
    auto statement = database_.prepare("DELETE FROM crypto_wallets WHERE address = ?1;");
    if (!statement) return std::unexpected(statement.error());
    if (auto result = statement->bind(1, address); !result) return std::unexpected(result.error());
    if (auto result = statement->step(); !result) return std::unexpected(result.error());
    return {};
}

}  // namespace wealthtracker::storage
