module;

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module wt.storage.balance_history;

export import wt.storage.database;

export namespace wealthtracker::storage {

struct HistoryBalance {
    std::string account_id;
    std::string name;
    std::string currency;
    std::int64_t amount_minor{};
};

struct BalanceCapture {
    std::int64_t id{};
    std::string connection_id;
    std::string institution;
    std::string captured_at;
    std::vector<HistoryBalance> balances;
};

struct CryptoWallet {
    std::string address;
    std::string name;
};

class BalanceHistoryRepository final {
public:
    explicit BalanceHistoryRepository(Database& database) noexcept;

    [[nodiscard]] std::expected<void, Error> initialize();
    [[nodiscard]] std::expected<std::int64_t, Error> append(
        std::string_view connection_id,
        std::string_view institution,
        std::string_view captured_at,
        std::span<const HistoryBalance> balances);
    [[nodiscard]] std::expected<std::vector<BalanceCapture>, Error> captures();
    [[nodiscard]] std::expected<std::vector<CryptoWallet>, Error> wallets();
    [[nodiscard]] std::expected<void, Error> save_wallet(
        std::string_view address, std::string_view name);
    [[nodiscard]] std::expected<void, Error> remove_wallet(std::string_view address);

private:
    Database& database_;
};

}  // namespace wealthtracker::storage
