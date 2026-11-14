#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QStringList>
#include <QDate>
#include <QDateTime>
#include <QVariant>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

import wt.storage.balance_history;
import wt.balance_history_view;

namespace {
using wealthtracker::storage::BalanceCapture;
using wealthtracker::storage::HistoryBalance;
using namespace wealthtracker::app;

BalanceCapture capture(const std::int64_t id, const std::string& connection,
                       const std::string& date, std::vector<HistoryBalance> balances) {
    return {id, connection, connection, date, std::move(balances)};
}

QVariantMap chart(const std::vector<BalanceCapture>& captures, const QString& account = {}) {
    return balance_history_chart(captures, account, QStringLiteral("RUB"));
}
}  // namespace

TEST_CASE("Balance history filters USDT wallets separately and respects local date boundaries") {
    const QDate day(2026, 10, 22);
    const auto midnight = day.startOfDay();
    const auto stamp = [](const QDateTime& time) { return time.toUTC().toString(Qt::ISODateWithMs).toStdString(); };
    const std::vector<BalanceCapture> captures{
        capture(1, "ethereum:a", stamp(midnight.addMSecs(-1)), {{"a", "Wallet A", "USDT", 10000}}),
        capture(2, "bank", stamp(midnight), {{"a", "Bank", "USD", 90000}}),
        capture(3, "ethereum:b", stamp(midnight), {{"b", "Wallet B", "USDT", 20000}}),
        capture(4, "ethereum:a", stamp(day.addDays(1).startOfDay().addMSecs(-1)), {{"a", "Wallet A", "USDT", 15000}}),
        capture(5, "ethereum:a", stamp(day.addDays(1).startOfDay()), {{"a", "Wallet A", "USDT", 99000}})};
    BalanceHistoryFilter filter{true, {"ethereum:a"}, day, day};
    const auto selected = balance_history_chart(captures, {}, "USDT", filter);
    CHECK(selected.value("error").toString().isEmpty());
    CHECK(selected.value("openingValue").toString() == "100.00 USDT");
    CHECK(selected.value("lastValue").toString() == "150.00 USDT");
    CHECK(selected.value("observationCount").toInt() == 1);
    CHECK(selected.value("baselineCarried").toBool());
    filter.restrictConnections = false;
    const auto combined = balance_history_chart(captures, {}, "USDT", filter);
    CHECK(combined.value("lastValue").toString() == "350.00 USDT");
    CHECK(balance_history_currencies(captures) == QStringList{"USD", "USDT"});
}

TEST_CASE("Balance history carries complete connector snapshots forward by currency") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T00:00:00Z", {{"1", "Same", "RUB", 100}, {"2", "USD", "USD", 9900}}),
        capture(2, "b", "2026-01-02T00:00:00Z", {{"1", "Same", "RUB", 200}}),
        capture(3, "a", "2026-01-05T00:00:00Z", {{"1", "Same", "RUB", 400}}),
        capture(4, "a", "2026-01-06T00:00:00Z", {}),
        capture(5, "b", "2026-01-07T00:00:00Z", {})};
    const auto result = chart(captures);
    CHECK(result.value("error").toString().isEmpty());
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 5);
    CHECK(points.at(0).toMap().value("label").toString() == "1.00 RUB");
    CHECK(points.at(1).toMap().value("label").toString() == "3.00 RUB");
    CHECK(points.at(2).toMap().value("label").toString() == "6.00 RUB");
    CHECK(points.at(3).toMap().value("label").toString() == "2.00 RUB");
    CHECK(points.at(4).toMap().value("label").toString() == "0.00 RUB");
    CHECK(points.at(1).toMap().value("x").toDouble() == Catch::Approx(1.0 / 6.0));
    CHECK(result.value("accountCount").toInt() == 0);
    CHECK(balance_history_currencies(captures) == QStringList{"RUB", "USD"});
    const auto accounts = balance_history_accounts(captures);
    REQUIRE(accounts.size() == 3);
    QString selected;
    for (const auto& account : accounts) {
        const auto option = account.toMap();
        if (option.value("label").toString() == "Same · a") selected = option.value("id").toString();
    }
    REQUIRE_FALSE(selected.isEmpty());
    const auto individual = chart(captures, selected);
    CHECK(individual.value("points").toList().size() == 2);
    CHECK(individual.value("lastValue").toString() == "4.00 RUB");
}

TEST_CASE("Balance history amounts stay exact beyond JavaScript integer precision") {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T00:00:00Z", {{"1", "Account", "RUB", minimum}}),
        capture(2, "a", "2026-01-02T00:00:00Z", {{"1", "Account", "RUB", maximum - 1}}),
        capture(3, "a", "2026-01-03T00:00:00Z", {{"1", "Account", "RUB", maximum}})};
    const auto result = chart(captures);
    CHECK(result.value("minimumLabel").toString() == "−92233720368547758.08 RUB");
    CHECK(result.value("maximumLabel").toString() == "92233720368547758.07 RUB");
    CHECK(result.value("points").toList().at(1).toMap().value("label").toString() == "92233720368547758.06 RUB");
    auto adjacent = captures;
    adjacent.erase(adjacent.begin());
    const auto points = chart(adjacent).value("points").toList();
    REQUIRE(points.size() == 2);
    CHECK(points.front().toMap().value("y").toDouble() == 0.0);
    CHECK(points.back().toMap().value("y").toDouble() == 1.0);
}

TEST_CASE("Balance history sorts events and coalesces simultaneous captures") {
    const std::vector<BalanceCapture> captures{
        capture(3, "a", "2026-01-02T00:00:00Z", {{"1", "A", "RUB", -50}}),
        capture(2, "b", "2026-01-01T00:00:00Z", {{"1", "B", "RUB", 200}}),
        capture(1, "a", "2026-01-01T00:00:00Z", {{"1", "A", "RUB", -100}})};
    const auto result = chart(captures);
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 2);
    CHECK(points.front().toMap().value("label").toString() == "1.00 RUB");
    CHECK(points.back().toMap().value("label").toString() == "1.50 RUB");
    CHECK(result.value("accountCount").toInt() == 2);
}

TEST_CASE("Balance history handles empty and single observation charts") {
    CHECK(chart({}).value("error").toString().isEmpty());
    CHECK(chart({}).value("points").toList().isEmpty());
    CHECK(chart({capture(1, "a", "2026-01-01T00:00:00Z", {})}).value("error").toString().isEmpty());
    const auto result = chart({capture(1, "a", "2026-01-01T00:00:00Z", {{"1", "A", "RUB", -1}})});
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 1);
    CHECK(points.front().toMap().value("x").toDouble() == 0.5);
    CHECK(points.front().toMap().value("y").toDouble() == 0.5);
    CHECK(result.value("lastValue").toString() == "−0.01 RUB");
}

TEST_CASE("Balance history does not invent balances before first observation") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T00:00:00Z", {}),
        capture(2, "a", "2026-01-02T00:00:00Z", {{"1", "A", "RUB", 100}})};
    CHECK(chart(captures).value("observationCount").toInt() == 1);
    CHECK_FALSE(balance_history_chart(captures, {}, "USD").value("error").toString().isEmpty());
}

TEST_CASE("Balance history rejects malformed captures and overflowing totals") {
    auto valid = capture(1, "a", "2026-01-01T00:00:00Z", {{"1", "A", "RUB", 100}});
    SECTION("Invalid timestamp") { valid.captured_at = "invalid"; }
    SECTION("Invalid currency") { valid.balances.front().currency = "?"; }
    SECTION("Duplicate account") { valid.balances.push_back(valid.balances.front()); }
    SECTION("Empty connector") { valid.connection_id.clear(); }
    SECTION("Positive overflow") {
        valid.balances.front().amount_minor = std::numeric_limits<std::int64_t>::max();
        valid.balances.push_back({"2", "B", "RUB", 1});
    }
    SECTION("Negative overflow") {
        valid.balances.front().amount_minor = std::numeric_limits<std::int64_t>::min();
        valid.balances.push_back({"2", "B", "RUB", -1});
    }
    const auto result = chart({valid});
    CHECK_FALSE(result.value("error").toString().isEmpty());
    CHECK(result.value("points").toList().isEmpty());
}

TEST_CASE("History filters reconstruct selected bank balances at the period boundary") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "A", "RUB", 10000}}),
        capture(2, "b", "2026-01-02T12:00:00Z", {{"1", "B", "RUB", 20000}}),
        capture(3, "c", "2026-01-03T12:00:00Z", {{"1", "C", "RUB", 90000}}),
        capture(4, "a", "2026-01-05T12:00:00Z", {{"1", "A", "RUB", 15000}}),
        capture(5, "a", "2026-01-07T12:00:00Z", {{"1", "A", "RUB", 99999}})};
    const BalanceHistoryFilter filter{true, {"a", "b"}, QDate(2026, 1, 4), QDate(2026, 1, 6)};
    const auto result = balance_history_chart(captures, {}, "RUB", filter);
    CHECK(result.value("error").toString().isEmpty());
    CHECK(result.value("openingValue").toString() == "300.00 RUB");
    CHECK(result.value("lastValue").toString() == "350.00 RUB");
    CHECK(result.value("changeValue").toString() == "50 RUB");
    CHECK(result.value("changePercent").toString() == "16.67 %");
    CHECK(result.value("accountCount").toInt() == 2);
    CHECK(result.value("observationCount").toInt() == 1);
    CHECK(result.value("baselineCarried").toBool());
    REQUIRE(result.value("points").toList().size() == 2);
    CHECK(result.value("points").toList().front().toMap().value("baseline").toBool());
    CHECK(balance_history_connections(captures).size() == 3);

    auto none = filter;
    none.connectionIds.clear();
    CHECK(balance_history_chart(captures, {}, "RUB", none).value("points").toList().isEmpty());
    none.connectionIds = {"missing"};
    CHECK(balance_history_chart(captures, {}, "RUB", none).value("points").toList().isEmpty());
    const auto account = balance_history_accounts(captures).front().toMap().value("id").toString();
    CHECK(balance_history_chart(captures, account, "RUB", none).value("points").toList().isEmpty());
}

TEST_CASE("History periods distinguish carried balances from new observations") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "A", "RUB", 100}}),
        capture(2, "a", "2026-01-08T12:00:00Z", {})};
    const auto carried = balance_history_chart(captures, {}, "RUB", {false, {}, QDate(2026, 1, 2), QDate(2026, 1, 7)});
    CHECK(carried.value("lastValue").toString() == "1.00 RUB");
    CHECK(carried.value("openingValue").toString() == "1.00 RUB");
    CHECK(carried.value("changeValue").toString() == "0 RUB");
    CHECK(carried.value("observationCount").toInt() == 0);
    CHECK(carried.value("baselineCarried").toBool());
    CHECK(balance_history_chart(captures, {}, "RUB", {false, {}, {}, QDate(2025, 12, 31)})
              .value("points").toList().isEmpty());
    const auto emptied = balance_history_chart(captures, {}, "RUB", {false, {}, QDate(2026, 1, 2), QDate(2026, 1, 8)});
    CHECK(emptied.value("lastValue").toString() == "0.00 RUB");
    CHECK(emptied.value("changePercent").toString() == "-100 %");
    CHECK_FALSE(balance_history_chart(captures, {}, "RUB", {false, {}, QDate(2026, 2, 1), QDate(2026, 1, 1)})
                    .value("error").toString().isEmpty());
}

TEST_CASE("History changes remain exact when endpoint subtraction exceeds int64") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "A", "RUB", std::numeric_limits<std::int64_t>::min()}}),
        capture(2, "a", "2026-01-02T12:00:00Z", {{"1", "A", "RUB", std::numeric_limits<std::int64_t>::max()}})};
    const auto result = chart(captures);
    CHECK(result.value("error").toString().isEmpty());
    CHECK(result.value("changeValue").toString() == "184467440737095516.15 RUB");
    CHECK(result.value("changePercent").toString().isEmpty());
}

TEST_CASE("History composition carries accounts forward and shows closure without invented earlier zeros") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"same", "A", "RUB", 100}}),
        capture(2, "b", "2026-01-02T12:00:00Z", {{"same", "B", "RUB", 300}}),
        capture(3, "a", "2026-01-03T12:00:00Z", {}),
        capture(4, "b", "2026-01-04T12:00:00Z", {})};
    const auto result = chart(captures);
    REQUIRE(result.value("compositionAvailable").toBool());
    const auto series = result.value("series").toList();
    REQUIRE(series.size() == 2);
    CHECK(series[0].toMap().value("id") != series[1].toMap().value("id"));
    const auto first = series[0].toMap().value("points").toList();
    const auto second = series[1].toMap().value("points").toList();
    REQUIRE(first.size() == 4);
    REQUIRE(second.size() == 3);
    CHECK(first[0].toMap().value("upper").toDouble() == 0.25);
    CHECK(first[1].toMap().value("upper").toDouble() == 0.25);
    CHECK(first[1].toMap().value("shareLabel").toString() == "25 %");
    CHECK(second[0].toMap().value("lower").toDouble() == 0.25);
    CHECK(second[0].toMap().value("upper").toDouble() == 1.0);
    CHECK(second[0].toMap().value("shareLabel").toString() == "75 %");
    CHECK(second[0].toMap().value("x") == first[1].toMap().value("x"));
    CHECK(second[0].toMap().value("timestamp") == first[1].toMap().value("timestamp"));
    CHECK(first[2].toMap().value("label").toString() == "0.00 RUB");
    CHECK(first[2].toMap().value("upper").toDouble() == 0.0);
    CHECK(second[1].toMap().value("lower").toDouble() == 0.0);
    CHECK(second[1].toMap().value("upper").toDouble() == 0.75);
    CHECK(second[2].toMap().value("shareLabel").toString() == "—");
    CHECK(second[2].toMap().value("upper").toDouble() == 0.0);
    CHECK(series[0].toMap().value("lastValue").toString() == "0.00 RUB");
    CHECK(result.value("compositionMinimumLabel").toString() == "0.00 RUB");
    CHECK(result.value("compositionMaximumLabel").toString() == "4.00 RUB");
}

TEST_CASE("History composition applies source currency and date filters to carried baselines") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "A", "RUB", 100}, {"2", "Dollar", "USD", 999}}),
        capture(2, "b", "2026-01-02T12:00:00Z", {{"1", "B", "RUB", 300}}),
        capture(3, "c", "2026-01-03T12:00:00Z", {{"1", "C", "RUB", 999}}),
        capture(4, "a", "2026-01-05T12:00:00Z", {{"1", "A", "RUB", 300}})};
    const auto result = balance_history_chart(captures, {}, "RUB",
        {true, {"a", "b"}, QDate(2026, 1, 4), QDate(2026, 1, 6)});
    REQUIRE(result.value("compositionAvailable").toBool());
    const auto series = result.value("series").toList();
    REQUIRE(series.size() == 2);
    const auto first = series[0].toMap().value("points").toList();
    const auto second = series[1].toMap().value("points").toList();
    REQUIRE(first.size() == 2);
    REQUIRE(second.size() == 2);
    CHECK(first[0].toMap().value("baseline").toBool());
    CHECK(second[0].toMap().value("baseline").toBool());
    CHECK_FALSE(first[1].toMap().value("baseline").toBool());
    CHECK(first[0].toMap().value("shareLabel").toString() == "25 %");
    CHECK(first[0].toMap().value("upper").toDouble() == Catch::Approx(1.0 / 6.0));
    CHECK(second[0].toMap().value("upper").toDouble() == Catch::Approx(2.0 / 3.0));
    CHECK(first[1].toMap().value("shareLabel").toString() == "50 %");
    CHECK(first[1].toMap().value("upper") == second[1].toMap().value("lower"));
    CHECK(second[1].toMap().value("upper").toDouble() == 1.0);
    CHECK(result.value("observationCount").toInt() == 1);
}

TEST_CASE("History composition nets accounts within connections and preserves exact large amounts") {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto large = chart({capture(1, "a", "2026-01-01T12:00:00Z",
        {{"1", "A", "RUB", maximum - 1}, {"2", "B", "RUB", 1}})});
    REQUIRE(large.value("compositionAvailable").toBool());
    const auto series = large.value("series").toList();
    REQUIRE(series.size() == 1);
    CHECK(series[0].toMap().value("lastValue").toString() == "92233720368547758.07 RUB");
    CHECK(series[0].toMap().value("id").toString() == "a");
    CHECK(large.value("accountCount").toInt() == 2);
    CHECK(large.value("connectionCount").toInt() == 1);
    const auto negative = chart({capture(1, "a", "2026-01-01T12:00:00Z",
        {{"1", "Asset", "RUB", 100}, {"2", "Debt", "RUB", -50}})});
    CHECK(negative.value("compositionAvailable").toBool());
    REQUIRE(negative.value("series").toList().size() == 1);
    CHECK(negative.value("compositionNote").toString().isEmpty());
    CHECK(negative.value("shareBasis").toString() == "total");
    CHECK(negative.value("lastValue").toString() == "0.50 RUB");
    const auto overflow = chart({capture(1, "a", "2026-01-01T12:00:00Z",
        {{"1", "A", "RUB", maximum}, {"2", "B", "RUB", 1}})});
    CHECK_FALSE(overflow.value("error").toString().isEmpty());
    CHECK_FALSE(overflow.value("compositionAvailable").toBool());
    CHECK(overflow.value("series").toList().isEmpty());
}

TEST_CASE("Connection composition stacks signed balances around zero with gross shares") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "Savings", "RUB", 100}, {"2", "Cash", "RUB", 200}}),
        capture(2, "b", "2026-01-02T12:00:00Z", {{"1", "Debt", "RUB", -100}})};
    const auto result = chart(captures);
    REQUIRE(result.value("compositionAvailable").toBool());
    CHECK(result.value("compositionMinimumLabel").toString() == "−1.00 RUB");
    CHECK(result.value("compositionMaximumLabel").toString() == "3.00 RUB");
    CHECK(result.value("compositionZeroY").toDouble() == 0.25);
    CHECK(result.value("connectionCount").toInt() == 2);
    CHECK(result.value("accountCount").toInt() == 3);
    CHECK(result.value("shareBasis").toString() == "absolute");
    CHECK_FALSE(result.value("compositionNote").toString().isEmpty());
    const auto series = result.value("series").toList();
    REQUIRE(series.size() == 2);
    CHECK(series[0].toMap().value("id").toString() == "a");
    CHECK(series[0].toMap().value("label").toString() == "a");
    const auto asset = series[0].toMap().value("points").toList().back().toMap();
    const auto debt = series[1].toMap().value("points").toList().back().toMap();
    CHECK(asset.value("lower").toDouble() == 0.25);
    CHECK(asset.value("upper").toDouble() == 1.0);
    CHECK(asset.value("shareLabel").toString() == "75 %");
    CHECK(asset.value("valueSign").toInt() == 1);
    CHECK(debt.value("lower").toDouble() == 0.0);
    CHECK(debt.value("upper").toDouble() == 0.25);
    CHECK(debt.value("label").toString() == "−1.00 RUB");
    CHECK(debt.value("shareLabel").toString() == "25 %");
    CHECK(debt.value("valueSign").toInt() == -1);
    CHECK(result.value("points").toList().back().toMap().value("compositionY").toDouble() == 0.75);
}

TEST_CASE("Connection composition handles debt only zero and sign changes") {
    const std::vector<BalanceCapture> captures{
        capture(1, "a", "2026-01-01T12:00:00Z", {{"1", "A", "RUB", -100}}),
        capture(2, "a", "2026-01-02T12:00:00Z", {{"1", "A", "RUB", 0}}),
        capture(3, "a", "2026-01-03T12:00:00Z", {{"1", "A", "RUB", 100}})};
    const auto result = chart(captures);
    CHECK(result.value("compositionZeroY").toDouble() == 0.5);
    const auto values = result.value("series").toList().front().toMap().value("points").toList();
    REQUIRE(values.size() == 3);
    CHECK(values[0].toMap().value("lower").toDouble() == 0.0);
    CHECK(values[0].toMap().value("upper").toDouble() == 0.5);
    CHECK(values[1].toMap().value("lower").toDouble() == 0.5);
    CHECK(values[1].toMap().value("upper").toDouble() == 0.5);
    CHECK(values[1].toMap().value("shareLabel").toString() == "—");
    CHECK(values[2].toMap().value("lower").toDouble() == 0.5);
    CHECK(values[2].toMap().value("upper").toDouble() == 1.0);
    const auto debt = chart({captures.front()});
    CHECK(debt.value("compositionZeroY").toDouble() == 1.0);
    CHECK(debt.value("compositionMaximumLabel").toString() == "0.00 RUB");
    const auto zero = chart({captures[1]});
    CHECK(zero.value("compositionAvailable").toBool());
    CHECK(zero.value("compositionZeroY").toDouble() == 0.0);
    CHECK(zero.value("compositionMinimumLabel").toString() == "0.00 RUB");
}

TEST_CASE("Connection composition gross balances exceed int64 without losing exact labels") {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const std::vector<BalanceCapture> captures{
        capture(1, "debt", "2026-01-01T12:00:00Z", {{"1", "Debt", "RUB", -maximum}}),
        capture(2, "assets", "2026-01-02T12:00:00Z", {{"1", "A", "RUB", maximum}, {"2", "B", "RUB", maximum}})};
    const auto result = chart(captures);
    REQUIRE(result.value("error").toString().isEmpty());
    CHECK(result.value("compositionMaximumLabel").toString() == "184467440737095516.14 RUB");
    CHECK(result.value("lastValue").toString() == "92233720368547758.07 RUB");
    const auto series = result.value("series").toList();
    REQUIRE(series.size() == 2);
    CHECK(series[0].toMap().value("lastValue").toString() == "184467440737095516.14 RUB");
    const auto assets = series[0].toMap().value("points").toList().back().toMap();
    CHECK(assets.value("shareLabel").toString() == "66.67 %");
    CHECK(assets.value("upper").toDouble() == 1.0);
    CHECK(result.value("compositionZeroY").toDouble() == Catch::Approx(1.0 / 3.0));
}

TEST_CASE("History account colors remain distinct and stable when sources are filtered") {
    const std::vector<BalanceCapture> captures{
        capture(1, "vtb", "2026-01-01T00:00:00Z", {{"1", "A", "RUB", 100}}),
        capture(2, "alfa", "2026-01-02T00:00:00Z", {{"1", "B", "RUB", 200}})};
    const auto all = chart(captures).value("series").toList();
    REQUIRE(all.size() == 2);
    CHECK(all[0].toMap().value("colorIndex") != all[1].toMap().value("colorIndex"));
    const auto selected = balance_history_chart(captures, {}, "RUB", {true, {"vtb"}, {}, {}}).value("series").toList();
    REQUIRE(selected.size() == 1);
    for (const auto& item : all) {
        const auto series = item.toMap();
        if (series.value("id") == selected[0].toMap().value("id"))
            CHECK(series.value("colorIndex") == selected[0].toMap().value("colorIndex"));
    }
}

TEST_CASE("Daily history carries complete source snapshots between updates and uses each days latest value") {
    const auto stamp = [](const QDate& day, const int hour = 12) {
        return day.startOfDay().addSecs(hour * 3600).toUTC().toString(Qt::ISODateWithMs).toStdString();
    };
    const std::vector<BalanceCapture> captures{
        capture(1, "a", stamp(QDate(2026, 1, 2), 10), {{"1", "A", "RUB", 100}}),
        capture(2, "a", stamp(QDate(2026, 1, 2), 18), {{"1", "A", "RUB", 200}}),
        capture(3, "b", stamp(QDate(2026, 1, 4)), {{"1", "B", "RUB", 600}}),
        capture(4, "a", stamp(QDate(2026, 1, 5)), {})};
    const auto result = balance_history_chart(captures, {}, "RUB",
        {false, {}, QDate(2026, 1, 1), QDate(2026, 1, 6), true});
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 5);
    CHECK(result.value("observationCount").toInt() == 4);
    CHECK(result.value("openingUnknown").toBool());
    CHECK(points[0].toMap().value("dateLabel").toString() == "02.01.2026");
    CHECK(points[0].toMap().value("label").toString() == "2.00 RUB");
    CHECK(points[1].toMap().value("label").toString() == "2.00 RUB");
    CHECK(points[1].toMap().value("carried").toBool());
    CHECK_FALSE(points[2].toMap().value("carried").toBool());
    CHECK(points[2].toMap().value("label").toString() == "8.00 RUB");
    CHECK(points[3].toMap().value("label").toString() == "6.00 RUB");
    CHECK(points[4].toMap().value("carried").toBool());
    CHECK(points[4].toMap().value("dateLabel").toString() == "06.01.2026");
    CHECK(points[4].toMap().value("compositionY").toDouble() == 0.75);
    const auto series = result.value("series").toList();
    REQUIRE(series.size() == 2);
    const auto a = series[0].toMap().value("points").toList();
    const auto b = series[1].toMap().value("points").toList();
    REQUIRE(a.size() == 5);
    REQUIRE(b.size() == 3);
    CHECK(a[2].toMap().value("shareLabel").toString() == "25 %");
    CHECK(b[0].toMap().value("shareLabel").toString() == "75 %");
    CHECK(a[3].toMap().value("label").toString() == "0.00 RUB");
    CHECK(b[2].toMap().value("shareLabel").toString() == "100 %");
    CHECK(b[2].toMap().value("dateLabel") == points[4].toMap().value("dateLabel"));
    CHECK(chart(captures).value("points").toList().size() == 4);
}

TEST_CASE("Daily history uses carried baselines and counts real same-time observations") {
    const auto stamp = [](const QDate& day) { return day.startOfDay().addSecs(3600).toUTC().toString(Qt::ISODateWithMs).toStdString(); };
    const std::vector<BalanceCapture> captures{
        capture(1, "a", stamp(QDate(2026, 1, 1)), {{"1", "A", "RUB", 100}}),
        capture(2, "b", stamp(QDate(2026, 1, 2)), {{"1", "B", "RUB", 300}}),
        capture(3, "a", stamp(QDate(2026, 1, 5)), {{"1", "A", "RUB", 200}}),
        capture(4, "b", stamp(QDate(2026, 1, 5)), {{"1", "B", "RUB", 600}}),
        capture(5, "c", stamp(QDate(2026, 1, 5)), {{"1", "Excluded", "RUB", 999}})};
    const auto result = balance_history_chart(captures, {}, "RUB",
        {true, {"a", "b"}, QDate(2026, 1, 3), QDate(2026, 1, 6), true});
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 4);
    CHECK(result.value("observationCount").toInt() == 2);
    CHECK(result.value("baselineCarried").toBool());
    CHECK(points[0].toMap().value("baseline").toBool());
    CHECK(points[0].toMap().value("carried").toBool());
    CHECK_FALSE(points[1].toMap().value("baseline").toBool());
    CHECK(points[0].toMap().value("label").toString() == "4.00 RUB");
    CHECK(points[2].toMap().value("label").toString() == "8.00 RUB");
    REQUIRE(result.value("series").toList().size() == 2);
    const auto noUpdates = balance_history_chart(captures, {}, "RUB",
        {true, {"a", "b"}, QDate(2026, 1, 3), QDate(2026, 1, 4), true});
    CHECK(noUpdates.value("observationCount").toInt() == 0);
    CHECK(noUpdates.value("points").toList().size() == 2);
}

TEST_CASE("Daily history defaults to today respects explicit end dates and bounds calendar expansion") {
    const auto today = QDate::currentDate();
    const auto stamp = [](const QDate& day) { return day.startOfDay().toUTC().toString(Qt::ISODateWithMs).toStdString(); };
    const std::vector<BalanceCapture> captures{
        capture(1, "a", stamp(today.addDays(-2)), {{"1", "A", "RUB", 100}})};
    const auto unbounded = balance_history_chart(captures, {}, "RUB", {false, {}, {}, {}, true});
    const auto future = balance_history_chart(captures, {}, "RUB", {false, {}, {}, today.addDays(10), true});
    CHECK(unbounded.value("points").toList().size() == 3);
    CHECK(unbounded.value("lastDate").toString() == today.toString("dd.MM.yyyy"));
    CHECK(unbounded.value("lastValue").toString() == "1.00 RUB");
    CHECK(unbounded.value("observationCount").toInt() == 1);
    CHECK(future.value("points").toList().size() == 13);
    CHECK(future.value("lastDate").toString() == today.addDays(10).toString("dd.MM.yyyy"));
    CHECK(future.value("lastValue").toString() == "1.00 RUB");
    CHECK(future.value("observationCount").toInt() == 1);
    CHECK(future.value("points").toList().back().toMap().value("carried").toBool());
    const auto excessive = balance_history_chart(
        {capture(1, "a", stamp(today.addDays(-36600)), {{"1", "A", "RUB", 100}})},
        {}, "RUB", {false, {}, {}, {}, true});
    CHECK_FALSE(excessive.value("error").toString().isEmpty());
    CHECK(excessive.value("points").toList().isEmpty());
}

TEST_CASE("Daily history preserves configured boundaries and carries the previous connection balance") {
    const auto stamp = [](const QDate& day) {
        return day.startOfDay().addSecs(3600).toUTC().toString(Qt::ISODateWithMs).toStdString();
    };
    const auto from = QDate::currentDate().addDays(-1);
    const auto to = from.addDays(5);
    const std::vector<BalanceCapture> captures{
        capture(1, "a", stamp(from.addDays(-2)), {{"1", "A", "RUB", 10000}}),
        capture(2, "b", stamp(from.addDays(-1)), {{"1", "B", "RUB", -2000}})};
    const auto result = balance_history_chart(captures, {}, "RUB", {false, {}, from, to, true});
    CHECK(result.value("error").toString().isEmpty());
    CHECK(result.value("firstDate").toString() == from.toString("dd.MM.yyyy"));
    CHECK(result.value("lastDate").toString() == to.toString("dd.MM.yyyy"));
    CHECK(result.value("openingValue").toString() == "80.00 RUB");
    CHECK(result.value("lastValue").toString() == "80.00 RUB");
    CHECK(result.value("observationCount").toInt() == 0);
    CHECK(result.value("baselineCarried").toBool());
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 6);
    CHECK(points.front().toMap().value("x").toDouble() == 0.0);
    CHECK(points.back().toMap().value("x").toDouble() == 1.0);
    for (const auto& point : points) {
        CHECK(point.toMap().value("label").toString() == "80.00 RUB");
        CHECK(point.toMap().value("carried").toBool());
        CHECK(point.toMap().value("timestamp").toString().startsWith(from.addDays(-1).toString("dd.MM.yyyy")));
    }
    REQUIRE(result.value("series").toList().size() == 2);
}

TEST_CASE("Daily history leaves unknown opening dates empty within the configured range") {
    const QDate from(2026, 1, 1), to(2026, 1, 5);
    const auto stamp = from.addDays(2).startOfDay().addSecs(3600).toUTC().toString(Qt::ISODateWithMs).toStdString();
    const std::vector<BalanceCapture> captures{
        capture(1, "a", stamp, {{"1", "A", "RUB", 100}})};
    const auto result = balance_history_chart(captures, {}, "RUB", {false, {}, from, to, true});
    CHECK(result.value("firstDate").toString() == "01.01.2026");
    CHECK(result.value("lastDate").toString() == "05.01.2026");
    CHECK(result.value("openingUnknown").toBool());
    const auto points = result.value("points").toList();
    REQUIRE(points.size() == 3);
    CHECK(points.front().toMap().value("dateLabel").toString() == "03.01.2026");
    CHECK(points.front().toMap().value("x").toDouble() == 0.5);
    CHECK_FALSE(points.front().toMap().value("baseline").toBool());
    const auto empty = balance_history_chart(captures, {}, "RUB", {false, {}, from, from.addDays(1), true});
    CHECK(empty.value("points").toList().isEmpty());
    CHECK(empty.value("firstDate").toString() == "01.01.2026");
    CHECK(empty.value("lastDate").toString() == "02.01.2026");
}
