import wt.core.decimal;
import wt.core.money;

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace wealthtracker::core;

TEST_CASE("currency parsing normalizes ISO codes") {
    const auto rub = Currency::parse("rub");
    REQUIRE(rub);
    CHECK(rub->code() == "RUB");
    CHECK_FALSE(Currency::parse("RU"));
    CHECK_FALSE(Currency::parse("R1B"));
}

TEST_CASE("money arithmetic is currency-safe and checked") {
    const Money first = *Money::from_minor(125, "RUB");
    const Money second = *Money::from_minor(75, "rub");

    REQUIRE(first.add(second));
    CHECK(first.add(second)->minor_units() == 200);
    CHECK_FALSE(first.add(*Money::from_minor(1, "USD")));

    const Money maximum =
        Money::from_minor(std::numeric_limits<std::int64_t>::max(), Currency::rub());
    CHECK_FALSE(maximum.add(Money::from_minor(1, Currency::rub())));
}

TEST_CASE("decimal parsing is exact and canonical") {
    const auto value = Decimal::parse("-0012.3400");
    REQUIRE(value);
    CHECK(value->to_string() == "-12.34");
    CHECK(value->scale() == 2);
    CHECK(*value == *Decimal::parse("-12.340"));

    CHECK_FALSE(Decimal::parse(""));
    CHECK_FALSE(Decimal::parse("1,25"));
    CHECK_FALSE(Decimal::parse("1.2.3"));
}

TEST_CASE("decimal operations preserve precision and round halves away from zero") {
    const Decimal one = Decimal::from_integer(1);
    const Decimal eight = Decimal::from_integer(8);
    REQUIRE(one.divide(eight, 4));
    CHECK(one.divide(eight, 4)->to_string() == "0.125");

    const auto quantity = Decimal::parse("2.5");
    const auto price = Decimal::parse("123.45");
    REQUIRE(quantity);
    REQUIRE(price);
    REQUIRE(quantity->multiply(*price));
    CHECK(quantity->multiply(*price)->to_string() == "308.625");
    CHECK(quantity->multiply(*price)->rescale(0)->to_string() == "309");

    const auto negative_half = Decimal::parse("-2.5");
    REQUIRE(negative_half);
    CHECK(negative_half->rescale(0)->to_string() == "-3");
}

TEST_CASE("money multiplication uses exact decimal rounding") {
    const Money amount = Money::from_minor(101, Currency::rub());
    const auto factor = Decimal::parse("1.5");
    REQUIRE(factor);
    REQUIRE(amount.multiplied_by(*factor));
    CHECK(amount.multiplied_by(*factor)->minor_units() == 152);
}
