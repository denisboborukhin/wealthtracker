module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module wt.storage.repository;

namespace wealthtracker::storage {
namespace {

std::expected<void, Error> bind_optional(
    Statement& statement,
    int index,
    const std::optional<std::string>& value) {
    return value ? statement.bind(index, *value) : statement.bind_null(index);
}

}  // namespace

LedgerRepository::LedgerRepository(Database& database) noexcept : database_(database) {}

std::expected<std::vector<AccountRecord>, Error> LedgerRepository::accounts(bool include_closed) {
    auto statement = database_.prepare(R"sql(
        SELECT id, institution_id, name, account_type, currency, is_closed
        FROM accounts
        WHERE ?1 = 1 OR is_closed = 0
        ORDER BY name COLLATE NOCASE, id;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, std::int64_t{include_closed ? 1 : 0}); !result) {
        return std::unexpected(result.error());
    }
    std::vector<AccountRecord> result;
    while (true) {
        auto step = statement->step();
        if (!step) {
            return std::unexpected(step.error());
        }
        if (*step == StepResult::done) {
            break;
        }
        result.push_back(AccountRecord{
            statement->column_text(0),
            statement->column_text(1),
            statement->column_text(2),
            statement->column_text(3),
            statement->column_text(4),
            statement->column_int64(5) != 0,
        });
    }
    return result;
}

std::expected<std::vector<LedgerTransactionRecord>, Error> LedgerRepository::transactions(
    std::string_view account_id,
    std::string_view date_from,
    std::string_view date_to,
    std::int64_t limit) {
    if (account_id.empty() || date_from.empty() || date_to.empty() || date_from > date_to ||
        limit <= 0 || limit > 10'000) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid transaction query"});
    }
    auto statement = database_.prepare(R"sql(
        SELECT
            t.id, t.account_id, t.posted_on, t.occurred_at, t.amount_minor,
            t.currency, t.description, o.category_id, o.merchant_name,
            COALESCE(o.excluded_from_analytics, 0)
        FROM ledger_transactions AS t
        LEFT JOIN transaction_overlays AS o ON o.transaction_id = t.id
        WHERE t.account_id = ?1 AND t.posted_on BETWEEN ?2 AND ?3
        ORDER BY t.posted_on DESC, t.id
        LIMIT ?4;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, account_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(2, date_from); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(3, date_to); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(4, limit); !result) {
        return std::unexpected(result.error());
    }

    std::vector<LedgerTransactionRecord> result;
    while (true) {
        auto step = statement->step();
        if (!step) {
            return std::unexpected(step.error());
        }
        if (*step == StepResult::done) {
            break;
        }
        LedgerTransactionRecord record{
            .id = statement->column_text(0),
            .account_id = statement->column_text(1),
            .posted_on = statement->column_text(2),
            .occurred_at = std::nullopt,
            .amount_minor = statement->column_int64(4),
            .currency = statement->column_text(5),
            .description = statement->column_text(6),
            .category_id = std::nullopt,
            .merchant_name = std::nullopt,
            .excluded_from_analytics = statement->column_int64(9) != 0,
        };
        if (!statement->column_is_null(3)) {
            record.occurred_at = statement->column_text(3);
        }
        if (!statement->column_is_null(7)) {
            record.category_id = statement->column_text(7);
        }
        if (!statement->column_is_null(8)) {
            record.merchant_name = statement->column_text(8);
        }
        result.push_back(std::move(record));
    }
    return result;
}

std::expected<void, Error> LedgerRepository::set_transaction_overlay(
    std::string_view transaction_id,
    const TransactionOverlayDraft& overlay) {
    if (transaction_id.empty() || overlay.updated_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid transaction overlay"});
    }
    auto statement = database_.prepare(R"sql(
        INSERT INTO transaction_overlays(
            transaction_id, category_id, merchant_name, note,
            excluded_from_analytics, updated_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6)
        ON CONFLICT(transaction_id) DO UPDATE SET
            category_id = excluded.category_id,
            merchant_name = excluded.merchant_name,
            note = excluded.note,
            excluded_from_analytics = excluded.excluded_from_analytics,
            updated_at = excluded.updated_at;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, transaction_id); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 2, overlay.category_id); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 3, overlay.merchant_name); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 4, overlay.note); !result) {
        return result;
    }
    if (auto result = statement->bind(
            5, std::int64_t{overlay.excluded_from_analytics ? 1 : 0});
        !result) {
        return result;
    }
    if (auto result = statement->bind(6, overlay.updated_at); !result) {
        return result;
    }
    auto step = statement->step();
    if (!step) {
        return std::unexpected(step.error());
    }
    return {};
}

}  // namespace wealthtracker::storage
