module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module wt.core.analytics;
export import wt.core.models;

export namespace wealthtracker::core {

enum class AnalyticsError {
    currency_mismatch,
    arithmetic_overflow,
    invalid_period,
    invalid_position,
    invalid_split,
};

struct CategoryAmount {
    CategoryId category_id;
    Money amount;
    bool operator==(const CategoryAmount&) const = default;
};

struct CashflowSummary {
    Money income;
    Money spending;
    Money net;
    std::optional<Decimal> savings_rate;
    std::vector<CategoryAmount> spending_by_category;
};

[[nodiscard]] std::expected<CashflowSummary, AnalyticsError> summarize_cashflow(
    std::span<const Transaction> transactions,
    Currency reporting_currency,
    LocalDate from_inclusive,
    LocalDate to_exclusive);

struct NetWorthEntry {
    AccountId account_id;
    Money reporting_value;
};

[[nodiscard]] std::expected<Money, AnalyticsError> calculate_net_worth(
    std::span<const NetWorthEntry> balances,
    Currency reporting_currency);

struct PositionValuationInput {
    InstrumentId instrument_id;
    std::string asset_class;
    Decimal quantity;
    Decimal market_price_minor_units;
    Money cost_basis;
    Money realized_pnl;
};

struct PositionValuation {
    InstrumentId instrument_id;
    std::string asset_class;
    Money market_value;
    Money cost_basis;
    Money unrealized_pnl;
    Money realized_pnl;
    Decimal allocation;
};

struct PortfolioSummary {
    Money market_value;
    Money cost_basis;
    Money unrealized_pnl;
    Money realized_pnl;
    std::vector<PositionValuation> positions;
};

[[nodiscard]] std::expected<PortfolioSummary, AnalyticsError> value_portfolio(
    std::span<const PositionValuationInput> positions,
    Currency reporting_currency);

struct DatedCashFlow {
    LocalDate date;
    Money amount;
};

enum class XirrError {
    insufficient_cash_flows,
    currency_mismatch,
    no_sign_change,
    no_solution,
};

[[nodiscard]] std::expected<double, XirrError> calculate_xirr(
    std::span<const DatedCashFlow> cash_flows,
    double tolerance = 1e-9,
    std::size_t maximum_iterations = 256);

}  // namespace wealthtracker::core

namespace wealthtracker::core {
namespace {

bool excluded_from_cashflow(const TransactionKind kind) {
    return kind == TransactionKind::transfer ||
           kind == TransactionKind::credit_card_repayment ||
           kind == TransactionKind::investment_purchase ||
           kind == TransactionKind::investment_sale;
}

AnalyticsError map_money_error(const MoneyError error) {
    return error == MoneyError::currency_mismatch ? AnalyticsError::currency_mismatch
                                                  : AnalyticsError::arithmetic_overflow;
}

std::expected<Money, AnalyticsError> add_money(const Money& left, const Money& right) {
    auto result = left.add(right);
    return result ? std::expected<Money, AnalyticsError>{*result}
                  : std::unexpected(map_money_error(result.error()));
}

std::expected<Money, AnalyticsError> subtract_money(const Money& left, const Money& right) {
    auto result = left.subtract(right);
    return result ? std::expected<Money, AnalyticsError>{*result}
                  : std::unexpected(map_money_error(result.error()));
}

std::expected<Money, AnalyticsError> decimal_minor_units_to_money(
    const Decimal& value,
    const Currency& currency) {
    auto minor_units = value.to_int64_rounded();
    if (!minor_units) {
        return std::unexpected(AnalyticsError::arithmetic_overflow);
    }
    return Money::from_minor(*minor_units, currency);
}

long double net_present_value(
    const std::span<const DatedCashFlow> cash_flows,
    const LocalDate first_date,
    const long double rate) {
    long double value = 0.0L;
    for (const auto& cash_flow : cash_flows) {
        const auto days = (cash_flow.date - first_date).count();
        const long double years = static_cast<long double>(days) / 365.0L;
        value += static_cast<long double>(cash_flow.amount.minor_units()) /
                 std::pow(1.0L + rate, years);
    }
    return value;
}

}  // namespace

std::expected<CashflowSummary, AnalyticsError> summarize_cashflow(
    const std::span<const Transaction> transactions,
    const Currency reporting_currency,
    const LocalDate from_inclusive,
    const LocalDate to_exclusive) {
    if (to_exclusive <= from_inclusive) {
        return std::unexpected(AnalyticsError::invalid_period);
    }
    CashflowSummary summary{
        .income = Money::from_minor(0, reporting_currency),
        .spending = Money::from_minor(0, reporting_currency),
        .net = Money::from_minor(0, reporting_currency)};
    std::map<CategoryId, Money> category_totals;

    for (const auto& transaction : transactions) {
        if (transaction.posting_date < from_inclusive ||
            transaction.posting_date >= to_exclusive ||
            excluded_from_cashflow(transaction.kind)) {
            continue;
        }
        if (transaction.amount.currency() != reporting_currency) {
            return std::unexpected(AnalyticsError::currency_mismatch);
        }
        if (transaction.amount.is_positive()) {
            auto updated = add_money(summary.income, transaction.amount);
            if (!updated) {
                return std::unexpected(updated.error());
            }
            summary.income = *updated;
        } else if (transaction.amount.is_negative()) {
            auto positive_amount = transaction.amount.negated();
            if (!positive_amount) {
                return std::unexpected(AnalyticsError::arithmetic_overflow);
            }
            auto updated = add_money(summary.spending, *positive_amount);
            if (!updated) {
                return std::unexpected(updated.error());
            }
            summary.spending = *updated;

            if (!transaction.splits.empty()) {
                Money split_total = Money::from_minor(0, reporting_currency);
                for (const auto& split : transaction.splits) {
                    if (split.amount.currency() != reporting_currency) {
                        return std::unexpected(AnalyticsError::currency_mismatch);
                    }
                    auto split_spending =
                        split.amount.is_negative() ? split.amount.negated()
                                                   : std::expected<Money, MoneyError>{split.amount};
                    if (!split_spending) {
                        return std::unexpected(AnalyticsError::arithmetic_overflow);
                    }
                    auto updated_split_total = add_money(split_total, *split_spending);
                    if (!updated_split_total) {
                        return std::unexpected(updated_split_total.error());
                    }
                    split_total = *updated_split_total;
                    auto [entry, inserted] = category_totals.try_emplace(
                        split.category_id,
                        Money::from_minor(0, reporting_currency));
                    auto category_value = add_money(entry->second, *split_spending);
                    if (!category_value) {
                        return std::unexpected(category_value.error());
                    }
                    entry->second = *category_value;
                }
                if (split_total != *positive_amount) {
                    return std::unexpected(AnalyticsError::invalid_split);
                }
            } else {
                auto [entry, inserted] = category_totals.try_emplace(
                    transaction.category_id.value_or("uncategorized"),
                    Money::from_minor(0, reporting_currency));
                auto category_value = add_money(entry->second, *positive_amount);
                if (!category_value) {
                    return std::unexpected(category_value.error());
                }
                entry->second = *category_value;
            }
        }
    }

    auto net = subtract_money(summary.income, summary.spending);
    if (!net) {
        return std::unexpected(net.error());
    }
    summary.net = *net;
    if (summary.income.is_positive()) {
        auto rate = Decimal::from_integer(summary.net.minor_units())
                        .divide(Decimal::from_integer(summary.income.minor_units()), 6);
        if (!rate) {
            return std::unexpected(AnalyticsError::arithmetic_overflow);
        }
        summary.savings_rate = *rate;
    }
    for (auto& [category, amount] : category_totals) {
        summary.spending_by_category.push_back(
            CategoryAmount{.category_id = std::move(category), .amount = amount});
    }
    return summary;
}

std::expected<Money, AnalyticsError> calculate_net_worth(
    const std::span<const NetWorthEntry> balances,
    const Currency reporting_currency) {
    Money total = Money::from_minor(0, reporting_currency);
    for (const auto& balance : balances) {
        if (balance.reporting_value.currency() != reporting_currency) {
            return std::unexpected(AnalyticsError::currency_mismatch);
        }
        auto updated = add_money(total, balance.reporting_value);
        if (!updated) {
            return std::unexpected(updated.error());
        }
        total = *updated;
    }
    return total;
}

std::expected<PortfolioSummary, AnalyticsError> value_portfolio(
    const std::span<const PositionValuationInput> positions,
    const Currency reporting_currency) {
    PortfolioSummary result{
        .market_value = Money::from_minor(0, reporting_currency),
        .cost_basis = Money::from_minor(0, reporting_currency),
        .unrealized_pnl = Money::from_minor(0, reporting_currency),
        .realized_pnl = Money::from_minor(0, reporting_currency)};
    const Decimal zero = Decimal::from_integer(0);

    for (const auto& position : positions) {
        if (position.cost_basis.currency() != reporting_currency ||
            position.realized_pnl.currency() != reporting_currency) {
            return std::unexpected(AnalyticsError::currency_mismatch);
        }
        if (position.quantity < zero || position.market_price_minor_units < zero ||
            position.cost_basis.is_negative()) {
            return std::unexpected(AnalyticsError::invalid_position);
        }
        auto raw_value = position.quantity.multiply(position.market_price_minor_units);
        if (!raw_value) {
            return std::unexpected(AnalyticsError::arithmetic_overflow);
        }
        auto market_value = decimal_minor_units_to_money(*raw_value, reporting_currency);
        if (!market_value) {
            return std::unexpected(market_value.error());
        }
        auto unrealized = subtract_money(*market_value, position.cost_basis);
        if (!unrealized) {
            return std::unexpected(unrealized.error());
        }

        const std::array<std::pair<Money*, const Money*>, 4> aggregates{{
            {&result.market_value, &*market_value},
            {&result.cost_basis, &position.cost_basis},
            {&result.unrealized_pnl, &*unrealized},
            {&result.realized_pnl, &position.realized_pnl},
        }};
        for (const auto [aggregate, addition] : aggregates) {
            auto updated = add_money(*aggregate, *addition);
            if (!updated) {
                return std::unexpected(updated.error());
            }
            *aggregate = *updated;
        }

        result.positions.push_back(PositionValuation{
            .instrument_id = position.instrument_id,
            .asset_class = position.asset_class,
            .market_value = *market_value,
            .cost_basis = position.cost_basis,
            .unrealized_pnl = *unrealized,
            .realized_pnl = position.realized_pnl,
            .allocation = Decimal::from_integer(0)});
    }
    if (result.market_value.is_positive()) {
        for (auto& position : result.positions) {
            auto allocation =
                Decimal::from_integer(position.market_value.minor_units())
                    .divide(Decimal::from_integer(result.market_value.minor_units()), 6);
            if (!allocation) {
                return std::unexpected(AnalyticsError::arithmetic_overflow);
            }
            position.allocation = *allocation;
        }
    }
    return result;
}

std::expected<double, XirrError> calculate_xirr(
    const std::span<const DatedCashFlow> cash_flows,
    const double tolerance,
    const std::size_t maximum_iterations) {
    if (cash_flows.size() < 2) {
        return std::unexpected(XirrError::insufficient_cash_flows);
    }
    const Currency currency = cash_flows.front().amount.currency();
    bool has_positive = false;
    bool has_negative = false;
    LocalDate first_date = cash_flows.front().date;
    for (const auto& cash_flow : cash_flows) {
        if (cash_flow.amount.currency() != currency) {
            return std::unexpected(XirrError::currency_mismatch);
        }
        has_positive = has_positive || cash_flow.amount.is_positive();
        has_negative = has_negative || cash_flow.amount.is_negative();
        first_date = std::min(first_date, cash_flow.date);
    }
    if (!has_positive || !has_negative) {
        return std::unexpected(XirrError::no_sign_change);
    }

    constexpr std::array<long double, 25> candidate_rates{
        -0.999999L, -0.99L, -0.95L, -0.9L, -0.75L, -0.5L, -0.25L,
        0.0L, 0.1L, 0.25L, 0.5L, 1.0L, 2.0L, 5.0L, 10.0L, 25.0L,
        50.0L, 100.0L, 250.0L, 500.0L, 1'000.0L, 10'000.0L,
        100'000.0L, 500'000.0L, 1'000'000.0L};
    long double lower = candidate_rates.front();
    long double lower_value = net_present_value(cash_flows, first_date, lower);
    std::optional<std::pair<long double, long double>> bracket;
    for (std::size_t index = 1; index < candidate_rates.size(); ++index) {
        const long double upper = candidate_rates[index];
        const long double upper_value = net_present_value(cash_flows, first_date, upper);
        if (std::isfinite(lower_value) && std::isfinite(upper_value) &&
            ((lower_value <= 0.0L && upper_value >= 0.0L) ||
             (lower_value >= 0.0L && upper_value <= 0.0L))) {
            bracket = std::pair{lower, upper};
            break;
        }
        lower = upper;
        lower_value = upper_value;
    }
    if (!bracket) {
        return std::unexpected(XirrError::no_solution);
    }

    lower = bracket->first;
    long double upper = bracket->second;
    lower_value = net_present_value(cash_flows, first_date, lower);
    for (std::size_t iteration = 0; iteration < maximum_iterations; ++iteration) {
        const long double midpoint = std::midpoint(lower, upper);
        const long double midpoint_value = net_present_value(cash_flows, first_date, midpoint);
        if (std::abs(midpoint_value) <= tolerance ||
            std::abs(upper - lower) <= tolerance) {
            return static_cast<double>(midpoint);
        }
        if ((lower_value < 0.0L && midpoint_value > 0.0L) ||
            (lower_value > 0.0L && midpoint_value < 0.0L)) {
            upper = midpoint;
        } else {
            lower = midpoint;
            lower_value = midpoint_value;
        }
    }
    return std::unexpected(XirrError::no_solution);
}

}  // namespace wealthtracker::core
