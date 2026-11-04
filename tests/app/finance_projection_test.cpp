#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <cstdint>
#include <limits>

import wt.connectors;
import wt.finance_projection;
import wt.reporting;

namespace {
using namespace wealthtracker::app;
using namespace wealthtracker::connectors;

NormalizedTransaction transaction(const QString& id, const QDate& date,
                                  const std::int64_t amount) {
    NormalizedTransaction result;
    result.externalId = id;
    result.accountExternalId = QStringLiteral("account");
    result.postingDate = date;
    result.amount = {amount, QStringLiteral("RUB")};
    result.description = id;
    result.bankCategory = QStringLiteral("Продукты");
    return result;
}

const QDate today{2026, 11, 4};
const ReportingPeriod period{QDate{2026, 11, 1}, today};
}

TEST_CASE("Financial projection keeps balances independent of reporting dates and excludes own transfers") {
    QHash<QString, NormalizedBatch> batches;
    auto& first = batches["bank-a"];
    first.accounts.push_back({"account", "Zulu account", "current", "RUB", {}, false, {}});
    first.balances.push_back({"account", QDateTime::currentDateTimeUtc(), {25000,"RUB"}, {}});
    first.transactions = {
        transaction("income", QDate(2026,11,3), 30000),
        transaction("spend", QDate(2026,11,1), -10000),
        transaction("transfer-out", QDate(2026,11,2), -500),
        transaction("outside-report", QDate(2026,10,31), -90000),
    };
    auto& second = batches["bank-b"];
    second.accounts.push_back({"account", "Alpha account", "current", "RUB", {}, false, {}});
    second.balances.push_back({"account", QDateTime::currentDateTimeUtc(), {7500,"RUB"}, {}});
    second.transactions = {transaction("transfer-in", QDate(2026,11,2), 500)};
    // Unknown integrations are skipped just as they are by the controller facade.
    batches["unknown"].balances.push_back({"account", QDateTime::currentDateTimeUtc(), {1000000,"RUB"}, {}});
    const QHash<QString, ProjectionInstitution> metadata{
        {"bank-a", {"Zulu Bank", "#111111"}}, {"bank-b", {"Alpha Bank", "#222222"}}};
    const auto result = projectFinance(batches, metadata, period, today);
    CHECK(result.netWorth == QStringLiteral("325 ₽"));
    CHECK(result.income == QStringLiteral("300 ₽"));
    CHECK(result.spending == QStringLiteral("100 ₽"));
    CHECK(result.savings == QStringLiteral("200 ₽"));
    REQUIRE(result.accounts.size() == 2);
    CHECK(result.accounts.front().institution == QStringLiteral("Alpha Bank"));
    CHECK(result.accounts.front().accent == QStringLiteral("#222222"));
    REQUIRE(result.transactions.size() == 4);
    CHECK(result.transactions.front().title == QStringLiteral("income"));
    int transfers = 0;
    for (const auto& row : result.transactions) if (row.transfer) ++transfers;
    CHECK(transfers == 2);
    REQUIRE(result.categories.size() == 1);
    CHECK(result.categories.front().amount == QStringLiteral("100 ₽"));
    CHECK(result.categories.front().share == 1.0);
    const auto later = projectFinance(batches, metadata, {today,today}, today);
    CHECK(later.netWorth == result.netWorth);
    CHECK(later.income == QStringLiteral("0 ₽"));
    CHECK(later.spending == QStringLiteral("0 ₽"));
    CHECK(later.transactions.isEmpty());
}

TEST_CASE("Financial projection preserves instrument names exact values and deterministic date labels") {
    NormalizedBatch batch;
    batch.positions.push_back({"account", "internal-id", "1", 0, {12345,"USD"},
        QDateTime::currentDateTimeUtc(), {}, "TMOS", "Fund name"});
    batch.transactions.push_back(transaction("today", today, 1));
    const auto result = projectFinance({{"t-invest",batch}},
        {{"t-invest",{"Т-Инвестиции", "#123456"}}}, period, today);
    REQUIRE(result.holdings.size() == 1);
    CHECK(result.holdings.front().ticker == "TMOS");
    CHECK(result.holdings.front().name == "Fund name");
    CHECK(result.holdings.front().value == QStringLiteral("123,45 $"));
    CHECK(result.holdings.front().valueMinorUnits == 12345);
    CHECK(result.holdings.front().currency == "USD");
    REQUIRE(result.transactions.size() == 1);
    CHECK(result.transactions.front().date == QStringLiteral("Сегодня"));
    CHECK(result.transactions.front().amount == QStringLiteral("+0,01 ₽"));
}

TEST_CASE("Financial projection reports overflow and minimum signed amounts without fabricated totals") {
    NormalizedBatch batch;
    batch.balances = {
        {"first", QDateTime::currentDateTimeUtc(), {std::numeric_limits<std::int64_t>::max(),"RUB"}, {}},
        {"second", QDateTime::currentDateTimeUtc(), {1,"RUB"}, {}},
    };
    batch.transactions.push_back(transaction("min", today, std::numeric_limits<std::int64_t>::min()));
    const auto result = projectFinance({{"t-invest",batch}},
        {{"t-invest",{"Т-Инвестиции", "#123456"}}}, period, today);
    CHECK(result.netWorth == QStringLiteral("—"));
    CHECK(result.portfolioValue == QStringLiteral("—"));
    CHECK(result.spending == QStringLiteral("—"));
    CHECK(result.savings == QStringLiteral("—"));
    CHECK(result.categories.isEmpty());
    REQUIRE(result.transactions.size() == 1);
    CHECK(result.transactions.front().amount == QStringLiteral("−92 233 720 368 547 758,08 ₽"));
}
