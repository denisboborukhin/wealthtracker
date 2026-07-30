module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

export module wt.storage.import_session;

export import wt.storage.database;
export import wt.storage.transaction;

export namespace wealthtracker::storage {

struct ImportBatchDraft {
    std::string id;
    std::string connector_id;
    std::string source_name;
    std::string source_sha256;
    std::string started_at;
};

struct SourceRecordDraft {
    std::string id;
    std::optional<std::string> external_source_id;
    std::int64_t row_number{};
    std::string canonical_hash;
    std::string raw_payload_json;
    std::string created_at;
};

struct InstitutionDraft {
    std::string id;
    std::string name;
    std::string country_code{"RU"};
    std::string created_at;
};

struct AccountDraft {
    std::string id;
    std::optional<std::string> connection_id;
    std::string institution_id;
    std::optional<std::string> external_id;
    std::string name;
    std::string account_type;
    std::string currency;
    std::optional<std::string> opened_at;
    std::string created_at;
};

struct LedgerTransactionDraft {
    std::string id;
    std::string account_id;
    std::string source_record_id;
    std::optional<std::string> external_id;
    std::string posted_on;
    std::optional<std::string> occurred_at;
    std::int64_t amount_minor{};
    std::string currency;
    std::string description;
    std::optional<std::string> bank_category;
    std::string transaction_kind{"regular"};
    std::string canonical_hash;
    std::string created_at;
};

enum class ImportInsertStatus {
    inserted,
    exact_duplicate,
    needs_review,
};

struct ImportInsertResult {
    ImportInsertStatus status{ImportInsertStatus::inserted};
    std::optional<std::string> possible_existing_id;
};

class ImportSession final {
public:
    ~ImportSession() = default;

    ImportSession(const ImportSession&) = delete;
    ImportSession& operator=(const ImportSession&) = delete;
    ImportSession(ImportSession&&) noexcept = default;
    ImportSession& operator=(ImportSession&&) noexcept = default;

    [[nodiscard]] static std::expected<ImportSession, Error> begin(
        Database& database,
        const ImportBatchDraft& batch);

    [[nodiscard]] std::expected<void, Error> upsert_institution(
        const InstitutionDraft& institution);
    [[nodiscard]] std::expected<void, Error> upsert_account(const AccountDraft& account);
    [[nodiscard]] std::expected<void, Error> add_source_record(const SourceRecordDraft& record);
    [[nodiscard]] std::expected<ImportInsertResult, Error> add_transaction(
        const LedgerTransactionDraft& transaction);
    [[nodiscard]] std::expected<void, Error> commit(
        std::string_view committed_at,
        std::int64_t warning_count);
    [[nodiscard]] std::expected<void, Error> rollback();

    [[nodiscard]] std::string_view batch_id() const noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    ImportSession(
        Database& database,
        SqlTransaction transaction,
        std::string batch_id) noexcept;

    Database* database_{};
    SqlTransaction transaction_;
    std::string batch_id_;
};

}  // namespace wealthtracker::storage
