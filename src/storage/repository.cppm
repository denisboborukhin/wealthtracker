module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module wt.storage.repository;

export import wt.storage.database;

export namespace wealthtracker::storage {

struct AccountRecord {
    std::string id;
    std::string institution_id;
    std::string name;
    std::string account_type;
    std::string currency;
    bool is_closed{};
};

struct LedgerTransactionRecord {
    std::string id;
    std::string account_id;
    std::string posted_on;
    std::optional<std::string> occurred_at;
    std::int64_t amount_minor{};
    std::string currency;
    std::string description;
    std::optional<std::string> category_id;
    std::optional<std::string> merchant_name;
    bool excluded_from_analytics{};
};

struct TransactionOverlayDraft {
    std::optional<std::string> category_id;
    std::optional<std::string> merchant_name;
    std::optional<std::string> note;
    bool excluded_from_analytics{};
    std::string updated_at;
};

class LedgerRepository final {
public:
    explicit LedgerRepository(Database& database) noexcept;

    [[nodiscard]] std::expected<std::vector<AccountRecord>, Error> accounts(
        bool include_closed = false);
    [[nodiscard]] std::expected<std::vector<LedgerTransactionRecord>, Error> transactions(
        std::string_view account_id,
        std::string_view date_from,
        std::string_view date_to,
        std::int64_t limit = 1'000);
    [[nodiscard]] std::expected<void, Error> set_transaction_overlay(
        std::string_view transaction_id,
        const TransactionOverlayDraft& overlay);

private:
    Database& database_;
};

}  // namespace wealthtracker::storage
