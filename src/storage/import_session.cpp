module;
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module wt.storage.import_session;

namespace wealthtracker::storage {
namespace {

std::expected<void, Error> bind_optional(
    Statement& statement,
    int index,
    const std::optional<std::string>& value) {
    if (value) {
        return statement.bind(index, *value);
    }
    return statement.bind_null(index);
}

std::expected<void, Error> expect_done(Statement& statement) {
    auto result = statement.step();
    if (!result) {
        return std::unexpected(result.error());
    }
    if (*result != StepResult::done) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "write statement unexpectedly returned a row"});
    }
    return {};
}

}  // namespace

ImportSession::ImportSession(
    Database& database,
    SqlTransaction transaction,
    std::string batch_id) noexcept
    : database_(&database),
      transaction_(std::move(transaction)),
      batch_id_(std::move(batch_id)) {}

std::expected<ImportSession, Error> ImportSession::begin(
    Database& database,
    const ImportBatchDraft& batch) {
    if (batch.id.empty() || batch.connector_id.empty() || batch.source_name.empty() ||
        batch.source_sha256.empty() || batch.started_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "import batch fields must not be empty"});
    }

    auto transaction = SqlTransaction::begin(database);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto statement = database.prepare(R"sql(
        INSERT INTO import_batches(
            id, connector_id, source_name, source_sha256, status, started_at)
        VALUES(?1, ?2, ?3, ?4, 'running', ?5);
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, batch.id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(2, batch.connector_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(3, batch.source_name); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(4, batch.source_sha256); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = statement->bind(5, batch.started_at); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = expect_done(*statement); !result) {
        return std::unexpected(result.error());
    }
    return ImportSession(database, std::move(*transaction), batch.id);
}

std::expected<void, Error> ImportSession::upsert_institution(
    const InstitutionDraft& institution) {
    if (!active()) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import session is not active"});
    }
    if (institution.id.empty() || institution.name.empty() ||
        institution.country_code.size() != 2 || institution.created_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid institution fields"});
    }
    auto statement = database_->prepare(R"sql(
        INSERT INTO institutions(id, name, country_code, created_at)
        VALUES(?1, ?2, ?3, ?4)
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            country_code = excluded.country_code;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, institution.id); !result) {
        return result;
    }
    if (auto result = statement->bind(2, institution.name); !result) {
        return result;
    }
    if (auto result = statement->bind(3, institution.country_code); !result) {
        return result;
    }
    if (auto result = statement->bind(4, institution.created_at); !result) {
        return result;
    }
    return expect_done(*statement);
}

std::expected<void, Error> ImportSession::upsert_account(const AccountDraft& account) {
    if (!active()) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import session is not active"});
    }
    if (account.id.empty() || account.institution_id.empty() || account.name.empty() ||
        account.account_type.empty() || account.currency.size() != 3 ||
        account.created_at.empty()) {
        return std::unexpected(Error{ErrorCode::invalid_argument, 0, "invalid account fields"});
    }
    auto statement = database_->prepare(R"sql(
        INSERT INTO accounts(
            id, connection_id, institution_id, external_id, name, account_type,
            currency, opened_at, created_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
        ON CONFLICT(id) DO UPDATE SET
            connection_id = excluded.connection_id,
            institution_id = excluded.institution_id,
            external_id = excluded.external_id,
            name = excluded.name,
            account_type = excluded.account_type,
            currency = excluded.currency,
            opened_at = excluded.opened_at,
            is_closed = 0;
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, account.id); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 2, account.connection_id); !result) {
        return result;
    }
    if (auto result = statement->bind(3, account.institution_id); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 4, account.external_id); !result) {
        return result;
    }
    if (auto result = statement->bind(5, account.name); !result) {
        return result;
    }
    if (auto result = statement->bind(6, account.account_type); !result) {
        return result;
    }
    if (auto result = statement->bind(7, account.currency); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 8, account.opened_at); !result) {
        return result;
    }
    if (auto result = statement->bind(9, account.created_at); !result) {
        return result;
    }
    return expect_done(*statement);
}

std::expected<void, Error> ImportSession::add_source_record(const SourceRecordDraft& record) {
    if (!active()) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import session is not active"});
    }
    if (record.id.empty() || record.row_number < 0 || record.canonical_hash.empty() ||
        record.raw_payload_json.empty() || record.created_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid source record fields"});
    }
    auto statement = database_->prepare(R"sql(
        INSERT INTO source_records(
            id, import_batch_id, external_source_id, row_number, canonical_hash,
            raw_payload_json, created_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, record.id); !result) {
        return result;
    }
    if (auto result = statement->bind(2, batch_id_); !result) {
        return result;
    }
    if (auto result = bind_optional(*statement, 3, record.external_source_id); !result) {
        return result;
    }
    if (auto result = statement->bind(4, record.row_number); !result) {
        return result;
    }
    if (auto result = statement->bind(5, record.canonical_hash); !result) {
        return result;
    }
    if (auto result = statement->bind(6, record.raw_payload_json); !result) {
        return result;
    }
    if (auto result = statement->bind(7, record.created_at); !result) {
        return result;
    }
    return expect_done(*statement);
}

std::expected<ImportInsertResult, Error> ImportSession::add_transaction(
    const LedgerTransactionDraft& transaction) {
    if (!active()) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import session is not active"});
    }
    if (transaction.id.empty() || transaction.account_id.empty() ||
        transaction.source_record_id.empty() || transaction.posted_on.empty() ||
        transaction.currency.size() != 3 || transaction.description.empty() ||
        transaction.transaction_kind.empty() || transaction.canonical_hash.empty() ||
        transaction.created_at.empty()) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid transaction fields"});
    }

    if (transaction.external_id) {
        auto exact = database_->prepare(R"sql(
            SELECT id, canonical_hash FROM ledger_transactions
            WHERE account_id = ?1 AND external_id = ?2
            LIMIT 1;
        )sql");
        if (!exact) {
            return std::unexpected(exact.error());
        }
        if (auto result = exact->bind(1, transaction.account_id); !result) {
            return std::unexpected(result.error());
        }
        if (auto result = exact->bind(2, *transaction.external_id); !result) {
            return std::unexpected(result.error());
        }
        auto step = exact->step();
        if (!step) {
            return std::unexpected(step.error());
        }
        if (*step == StepResult::row) {
            const auto existing_id = exact->column_text(0);
            if (exact->column_text(1) == transaction.canonical_hash) {
                return ImportInsertResult{
                    ImportInsertStatus::exact_duplicate, existing_id};
            }

            auto update = database_->prepare(R"sql(
                UPDATE ledger_transactions
                SET source_record_id = ?1,
                    posted_on = ?2,
                    occurred_at = ?3,
                    amount_minor = ?4,
                    currency = ?5,
                    description = ?6,
                    bank_category = ?7,
                    transaction_kind = ?8,
                    canonical_hash = ?9
                WHERE id = ?10;
            )sql");
            if (!update) {
                return std::unexpected(update.error());
            }
            if (auto result = update->bind(1, transaction.source_record_id); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(2, transaction.posted_on); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = bind_optional(*update, 3, transaction.occurred_at); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(4, transaction.amount_minor); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(5, transaction.currency); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(6, transaction.description); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = bind_optional(*update, 7, transaction.bank_category); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(8, transaction.transaction_kind); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(9, transaction.canonical_hash); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = update->bind(10, existing_id); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = expect_done(*update); !result) {
                return std::unexpected(result.error());
            }
            if (database_->changes() != 1) {
                return std::unexpected(Error{
                    ErrorCode::invalid_state, 0, "existing transaction was not updated"});
            }
            return ImportInsertResult{ImportInsertStatus::updated, existing_id};
        }
    }

    std::optional<std::string> possible_duplicate;
    auto fingerprint = database_->prepare(R"sql(
        SELECT id FROM ledger_transactions
        WHERE account_id = ?1
          AND posted_on = ?2
          AND amount_minor = ?3
          AND currency = ?4
          AND canonical_hash = ?5
        LIMIT 1;
    )sql");
    if (!fingerprint) {
        return std::unexpected(fingerprint.error());
    }
    if (auto result = fingerprint->bind(1, transaction.account_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = fingerprint->bind(2, transaction.posted_on); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = fingerprint->bind(3, transaction.amount_minor); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = fingerprint->bind(4, transaction.currency); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = fingerprint->bind(5, transaction.canonical_hash); !result) {
        return std::unexpected(result.error());
    }
    auto fingerprint_step = fingerprint->step();
    if (!fingerprint_step) {
        return std::unexpected(fingerprint_step.error());
    }
    if (*fingerprint_step == StepResult::row) {
        possible_duplicate = fingerprint->column_text(0);
    }

    auto insert = database_->prepare(R"sql(
        INSERT INTO ledger_transactions(
            id, account_id, source_record_id, external_id, posted_on, occurred_at,
            amount_minor, currency, description, bank_category, transaction_kind,
            canonical_hash, created_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);
    )sql");
    if (!insert) {
        return std::unexpected(insert.error());
    }
    if (auto result = insert->bind(1, transaction.id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(2, transaction.account_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(3, transaction.source_record_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = bind_optional(*insert, 4, transaction.external_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(5, transaction.posted_on); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = bind_optional(*insert, 6, transaction.occurred_at); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(7, transaction.amount_minor); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(8, transaction.currency); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(9, transaction.description); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = bind_optional(*insert, 10, transaction.bank_category); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(11, transaction.transaction_kind); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(12, transaction.canonical_hash); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = insert->bind(13, transaction.created_at); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = expect_done(*insert); !result) {
        return std::unexpected(result.error());
    }

    if (!possible_duplicate) {
        return ImportInsertResult{ImportInsertStatus::inserted, std::nullopt};
    }

    auto review = database_->prepare(R"sql(
        INSERT INTO import_review_queue(
            import_batch_id, source_record_id, possible_transaction_id, reason)
        VALUES(?1, ?2, ?3, 'canonical_fingerprint_match');
    )sql");
    if (!review) {
        return std::unexpected(review.error());
    }
    if (auto result = review->bind(1, batch_id_); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = review->bind(2, transaction.source_record_id); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = review->bind(3, *possible_duplicate); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = expect_done(*review); !result) {
        return std::unexpected(result.error());
    }
    return ImportInsertResult{ImportInsertStatus::needs_review, std::move(possible_duplicate)};
}

std::expected<void, Error> ImportSession::commit(
    std::string_view committed_at,
    std::int64_t warning_count) {
    if (!active()) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import session is not active"});
    }
    if (committed_at.empty() || warning_count < 0) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, 0, "invalid import commit fields"});
    }
    auto statement = database_->prepare(R"sql(
        UPDATE import_batches
        SET status = 'committed', committed_at = ?1, warning_count = ?2
        WHERE id = ?3 AND status = 'running';
    )sql");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto result = statement->bind(1, committed_at); !result) {
        return result;
    }
    if (auto result = statement->bind(2, warning_count); !result) {
        return result;
    }
    if (auto result = statement->bind(3, batch_id_); !result) {
        return result;
    }
    if (auto result = expect_done(*statement); !result) {
        return result;
    }
    if (database_->changes() != 1) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "import batch was not in running state"});
    }
    return transaction_.commit();
}

std::expected<void, Error> ImportSession::rollback() {
    return transaction_.rollback();
}

std::string_view ImportSession::batch_id() const noexcept {
    return batch_id_;
}

bool ImportSession::active() const noexcept {
    return database_ != nullptr && transaction_.active();
}

}  // namespace wealthtracker::storage
