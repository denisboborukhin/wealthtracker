module;

#include <chrono>
#include <optional>
#include <string>
#include <vector>

export module wt.core.models;
export import wt.core.decimal;
export import wt.core.money;

export namespace wealthtracker::core {

using AccountId = std::string;
using CategoryId = std::string;
using ConnectionId = std::string;
using InstrumentId = std::string;
using TransactionId = std::string;
using LocalDate = std::chrono::sys_days;

enum class AccountType {
    checking,
    savings,
    cash,
    credit_card,
    brokerage,
    deposit,
    loan,
    other,
};

struct Account {
    AccountId id;
    std::string institution_id;
    std::optional<ConnectionId> connection_id;
    std::string name;
    AccountType type{AccountType::other};
    Currency currency{};
    bool closed{false};
    bool operator==(const Account&) const = default;
};

struct BalanceSnapshot {
    AccountId account_id;
    LocalDate date;
    Money balance;
    bool operator==(const BalanceSnapshot&) const = default;
};

enum class TransactionKind {
    regular,
    transfer,
    credit_card_repayment,
    investment_purchase,
    investment_sale,
    fee,
    tax,
};

struct TransactionSplit {
    CategoryId category_id;
    Money amount;
    std::optional<std::string> note;
    bool operator==(const TransactionSplit&) const = default;
};

struct Transaction {
    TransactionId id;
    AccountId account_id;
    LocalDate posting_date;
    std::optional<std::chrono::sys_seconds> occurred_at;
    Money amount;
    std::string payee;
    std::string description;
    std::optional<std::string> bank_category;
    std::optional<std::string> source_transaction_id;
    std::optional<CategoryId> category_id;
    std::vector<TransactionSplit> splits;
    std::vector<std::string> tags;
    TransactionKind kind{TransactionKind::regular};
    std::optional<std::string> transfer_pair_id;
    bool operator==(const Transaction&) const = default;
};

enum class InvestmentEventType {
    buy,
    sell,
    dividend,
    coupon,
    fee,
    tax,
    deposit,
    withdrawal,
};

struct Instrument {
    InstrumentId id;
    std::string name;
    std::string ticker;
    std::string asset_class;
    Currency currency{};
    bool operator==(const Instrument&) const = default;
};

struct InvestmentEvent {
    std::string id;
    AccountId account_id;
    InstrumentId instrument_id;
    LocalDate date;
    InvestmentEventType type{InvestmentEventType::buy};
    std::optional<Decimal> quantity;
    Money cash_amount;
    bool operator==(const InvestmentEvent&) const = default;
};

struct PositionSnapshot {
    AccountId account_id;
    InstrumentId instrument_id;
    LocalDate date;
    Decimal quantity;
    Decimal price_minor_units;
    Currency currency{};
    bool operator==(const PositionSnapshot&) const = default;
};

struct Quote {
    InstrumentId instrument_id;
    LocalDate date;
    Decimal price_minor_units;
    Currency currency{};
    bool operator==(const Quote&) const = default;
};

struct FxRate {
    Currency from;
    Currency to;
    LocalDate date;
    Decimal rate;
    bool operator==(const FxRate&) const = default;
};

}  // namespace wealthtracker::core
