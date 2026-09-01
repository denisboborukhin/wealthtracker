#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <string>
#include <vector>

import wt.core.categorization;

using namespace std::chrono_literals;
using namespace wealthtracker::core;

namespace {

LocalDate day(const int year, const unsigned month, const unsigned day_of_month) {
    return std::chrono::sys_days{
        std::chrono::year{year} / std::chrono::month{month} /
        std::chrono::day{day_of_month}};
}

Transaction transaction(
    std::string id,
    std::string account,
    const LocalDate date,
    const std::int64_t amount,
    std::string payee = {},
    std::string description = {}) {
    Transaction result;
    result.id = std::move(id);
    result.account_id = std::move(account);
    result.posting_date = date;
    result.amount = Money::from_minor(amount, Currency::rub());
    result.payee = std::move(payee);
    result.description = std::move(description);
    return result;
}

}  // namespace

TEST_CASE("categorization follows stable precedence") {
    CategorizerConfig config;
    config.rules = {
        CategorizationRule{
            .id = "lower",
            .category_id = "groceries",
            .priority = 10,
            .payee_contains = "market"},
        CategorizationRule{
            .id = "higher",
            .category_id = "health",
            .priority = 20,
            .payee_contains = "MARKET"}};
    config.remembered_payees.emplace("local market", "remembered");
    config.bank_category_mapping.emplace("supermarkets", "bank");
    const Categorizer categorizer{std::move(config)};

    auto purchase =
        transaction("t1", "card", day(2026, 7, 1), -1'000, "Local Market");
    purchase.bank_category = "Supermarkets";
    const CategoryDecision rule_decision = categorizer.categorize(purchase);
    CHECK(rule_decision.category_id == "health");
    CHECK(rule_decision.source == CategoryDecisionSource::user_rule);
    CHECK(rule_decision.rule_id == "higher");

    purchase.category_id = "manual";
    const CategoryDecision existing_decision = categorizer.categorize(purchase);
    CHECK(existing_decision.category_id == "manual");
    CHECK(existing_decision.source == CategoryDecisionSource::existing);
}

TEST_CASE("categorization falls through mappings and transaction classifiers") {
    CategorizerConfig config;
    config.remembered_payees.emplace("Coffee Shop", "cafes");
    config.bank_category_mapping.emplace("Transport", "transport");
    const Categorizer categorizer{std::move(config)};

    auto remembered =
        transaction("t1", "card", day(2026, 7, 1), -500, "  COFFEE   SHOP ");
    CHECK(categorizer.categorize(remembered).category_id == "cafes");

    auto bank = transaction("t2", "card", day(2026, 7, 1), -500);
    bank.bank_category = "transport";
    CHECK(categorizer.categorize(bank).category_id == "transport");

    auto transfer = transaction("t3", "card", day(2026, 7, 1), -500);
    transfer.kind = TransactionKind::transfer;
    CHECK(categorizer.categorize(transfer).source ==
          CategoryDecisionSource::transfer_classifier);

    auto unknown = transaction("t4", "card", day(2026, 7, 1), -500);
    CHECK_FALSE(categorizer.categorize(unknown).category_id);
}

TEST_CASE("categorization normalizes Russian letter case") {
    CategorizerConfig config;
    config.remembered_payees.emplace("Пятёрочка", "groceries");
    config.bank_category_mapping.emplace("ТРАНСПОРТ", "transport");
    const Categorizer categorizer{std::move(config)};

    const auto purchase = transaction(
        "t1", "card", day(2026, 7, 1), -500, "ПЯТЁРОЧКА");
    CHECK(categorizer.categorize(purchase).category_id == "groceries");

    auto ride = transaction("t2", "card", day(2026, 7, 1), -500);
    ride.bank_category = "транспорт";
    CHECK(categorizer.categorize(ride).category_id == "transport");
}

TEST_CASE("transfer detection requires equal opposite amounts across accounts") {
    const std::vector transactions{
        transaction(
            "out",
            "checking",
            day(2026, 7, 1),
            -25'000,
            {},
            "Перевод между счетами"),
        transaction("in", "savings", day(2026, 7, 1), 25'000),
        transaction("purchase", "card", day(2026, 7, 1), -25'000),
        transaction("late", "savings", day(2026, 7, 10), 25'000)};

    const auto candidates = find_transfer_candidates(transactions);
    REQUIRE(candidates.size() == 2);
    CHECK(candidates.front().outgoing_transaction_id == "out");
    CHECK(candidates.front().incoming_transaction_id == "in");
    CHECK(candidates.front().confidence == Catch::Approx(1.0));
    CHECK(candidates.back().outgoing_transaction_id == "purchase");
    CHECK(candidates.back().confidence == Catch::Approx(0.9));
}

TEST_CASE("already paired transactions are not proposed again") {
    auto outgoing = transaction("out", "checking", day(2026, 7, 1), -1'000);
    outgoing.transfer_pair_id = "pair";
    const std::vector transactions{
        outgoing,
        transaction("in", "savings", day(2026, 7, 1), 1'000)};
    CHECK(find_transfer_candidates(transactions).empty());
}
