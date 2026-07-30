#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <vector>

import wt.core.analytics;

using namespace wealthtracker::core;

namespace {

LocalDate day(const int year, const unsigned month, const unsigned day_of_month) {
    return std::chrono::sys_days{
        std::chrono::year{year} / std::chrono::month{month} /
        std::chrono::day{day_of_month}};
}

Transaction transaction(
    std::string id,
    const LocalDate date,
    const std::int64_t amount,
    std::optional<CategoryId> category = std::nullopt,
    const TransactionKind kind = TransactionKind::regular) {
    Transaction result;
    result.id = std::move(id);
    result.account_id = "account";
    result.posting_date = date;
    result.amount = Money::from_minor(amount, Currency::rub());
    result.category_id = std::move(category);
    result.kind = kind;
    return result;
}

}  // namespace

TEST_CASE("cashflow reports income spending savings and categories") {
    const std::vector transactions{
        transaction("salary", day(2026, 7, 1), 100'000, "salary"),
        transaction("food", day(2026, 7, 2), -25'000, "groceries"),
        transaction("rent", day(2026, 7, 3), -35'000, "housing"),
        transaction(
            "transfer",
            day(2026, 7, 4),
            -10'000,
            "other",
            TransactionKind::transfer),
        transaction("outside", day(2026, 8, 1), -50'000, "other")};

    const auto summary = summarize_cashflow(
        transactions,
        Currency::rub(),
        day(2026, 7, 1),
        day(2026, 8, 1));
    REQUIRE(summary);
    CHECK(summary->income.minor_units() == 100'000);
    CHECK(summary->spending.minor_units() == 60'000);
    CHECK(summary->net.minor_units() == 40'000);
    REQUIRE(summary->savings_rate);
    CHECK(summary->savings_rate->to_string() == "0.4");
    REQUIRE(summary->spending_by_category.size() == 2);
    CHECK(summary->spending_by_category[0].category_id == "groceries");
    CHECK(summary->spending_by_category[0].amount.minor_units() == 25'000);
    CHECK(summary->spending_by_category[1].category_id == "housing");
}

TEST_CASE("cashflow uses split categories without double counting the total") {
    auto purchase = transaction("split", day(2026, 7, 2), -10'000);
    purchase.splits = {
        TransactionSplit{
            .category_id = "food",
            .amount = Money::from_minor(-6'000, Currency::rub())},
        TransactionSplit{
            .category_id = "household",
            .amount = Money::from_minor(-4'000, Currency::rub())}};

    const std::vector transactions{purchase};
    const auto summary = summarize_cashflow(
        transactions,
        Currency::rub(),
        day(2026, 7, 1),
        day(2026, 8, 1));
    REQUIRE(summary);
    CHECK(summary->spending.minor_units() == 10'000);
    REQUIRE(summary->spending_by_category.size() == 2);
    CHECK(summary->spending_by_category[0].amount.minor_units() == 6'000);
    CHECK(summary->spending_by_category[1].amount.minor_units() == 4'000);
}

TEST_CASE("cashflow rejects splits that do not reconcile to the transaction") {
    auto purchase = transaction("split", day(2026, 7, 2), -10'000);
    purchase.splits = {
        TransactionSplit{
            .category_id = "food",
            .amount = Money::from_minor(-9'000, Currency::rub())}};
    const std::vector transactions{purchase};

    const auto summary = summarize_cashflow(
        transactions,
        Currency::rub(),
        day(2026, 7, 1),
        day(2026, 8, 1));
    REQUIRE_FALSE(summary);
    CHECK(summary.error() == AnalyticsError::invalid_split);
}

TEST_CASE("net worth includes liabilities and rejects mixed currencies") {
    const std::vector balances{
        NetWorthEntry{"checking", Money::from_minor(150'000, Currency::rub())},
        NetWorthEntry{"credit", Money::from_minor(-30'000, Currency::rub())}};
    REQUIRE(calculate_net_worth(balances, Currency::rub()));
    CHECK(calculate_net_worth(balances, Currency::rub())->minor_units() == 120'000);

    const std::vector mixed{
        NetWorthEntry{"usd", *Money::from_minor(100, "USD")}};
    CHECK_FALSE(calculate_net_worth(mixed, Currency::rub()));
}

TEST_CASE("portfolio valuation reports exact values pnl and allocation") {
    const std::vector positions{
        PositionValuationInput{
            .instrument_id = "bond",
            .asset_class = "bonds",
            .quantity = *Decimal::parse("2.5"),
            .market_price_minor_units = *Decimal::parse("123.45"),
            .cost_basis = Money::from_minor(250, Currency::rub()),
            .realized_pnl = Money::from_minor(10, Currency::rub())},
        PositionValuationInput{
            .instrument_id = "cash-fund",
            .asset_class = "cash",
            .quantity = Decimal::from_integer(1),
            .market_price_minor_units = Decimal::from_integer(309),
            .cost_basis = Money::from_minor(300, Currency::rub()),
            .realized_pnl = Money::from_minor(0, Currency::rub())}};

    const auto summary = value_portfolio(positions, Currency::rub());
    REQUIRE(summary);
    CHECK(summary->market_value.minor_units() == 618);
    CHECK(summary->cost_basis.minor_units() == 550);
    CHECK(summary->unrealized_pnl.minor_units() == 68);
    CHECK(summary->realized_pnl.minor_units() == 10);
    REQUIRE(summary->positions.size() == 2);
    CHECK(summary->positions[0].allocation.to_string() == "0.5");
    CHECK(summary->positions[1].allocation.to_string() == "0.5");
}

TEST_CASE("XIRR calculates an annualized return for irregular cash flows") {
    const std::vector flows{
        DatedCashFlow{
            day(2025, 1, 1),
            Money::from_minor(-100'000, Currency::rub())},
        DatedCashFlow{
            day(2026, 1, 1),
            Money::from_minor(110'000, Currency::rub())}};

    const auto result = calculate_xirr(flows);
    REQUIRE(result);
    CHECK(*result == Catch::Approx(0.1).margin(1e-7));
}

TEST_CASE("XIRR rejects flows without both signs") {
    const std::vector flows{
        DatedCashFlow{day(2025, 1, 1), Money::from_minor(100, Currency::rub())},
        DatedCashFlow{day(2026, 1, 1), Money::from_minor(110, Currency::rub())}};
    REQUIRE_FALSE(calculate_xirr(flows));
    CHECK(calculate_xirr(flows).error() == XirrError::no_sign_change);
}
