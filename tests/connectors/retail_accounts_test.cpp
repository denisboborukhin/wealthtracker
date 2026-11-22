#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

import wt.connectors;

using namespace wealthtracker::connectors;

TEST_CASE("Retail accounts preserve exact balances and bank provenance") {
    const auto bank = GENERATE(RetailBank::Alfa, RetailBank::Vtb);
    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-08-15T09:00:00+03:00"), Qt::ISODate);
    const auto connectorId = bank == RetailBank::Alfa
        ? QStringLiteral("alfa-retail") : QStringLiteral("vtb-retail");
    const QJsonObject account{{"id", "card"}, {"currency", "RUB"},
                              {"balanceMinor", "9223372036854775807"}};
    const auto parsed = parseRetailAccounts(
        {account, account, QJsonObject{{"id", "loan"}, {"currency", "USD"},
                                      {"balanceMinor", "-9223372036854775808"}}},
        observedAt, bank);
    REQUIRE(parsed);
    REQUIRE(parsed->accounts.size() == 2);
    REQUIRE(parsed->balances.size() == 2);
    CHECK(parsed->balances.front().balance.minorUnits == INT64_MAX);
    CHECK(parsed->balances.back().balance.minorUnits == INT64_MIN);
    CHECK(parsed->balances.front().observedAt == observedAt.toUTC());
    CHECK(parsed->accounts.front().provenance.connectorId == connectorId);
    CHECK(parsed->balances.back().provenance.connectorId == connectorId);
    CHECK(parsed->accounts.front().name == (bank == RetailBank::Alfa
        ? QStringLiteral("Счёт Альфа-Банка") : QStringLiteral("Счёт ВТБ")));
    CHECK(parsed->transactions.isEmpty());
    REQUIRE(parsed->warnings.size() == 1);
    CHECK(parsed->warnings.front().code == (bank == RetailBank::Alfa
        ? QStringLiteral("duplicate-alfa-account") : QStringLiteral("duplicate-vtb-account")));
}

TEST_CASE("Retail accounts reject malformed balances and incomplete accounts") {
    const auto bank = GENERATE(RetailBank::Alfa, RetailBank::Vtb);
    const auto value = GENERATE(QJsonValue("1.5"), QJsonValue(100),
                               QJsonValue("9223372036854775808"), QJsonValue());
    const auto now = QDateTime::currentDateTimeUtc();
    CHECK_FALSE(parseRetailAccounts(
        {QJsonObject{{"id", "card"}, {"currency", "RUB"}, {"balanceMinor", value}}},
        now, bank));
    CHECK_FALSE(parseRetailAccounts({QJsonObject{{"balanceMinor", "100"}}}, now, bank));
    CHECK_FALSE(parseRetailAccounts({QJsonValue("account")}, now, bank));
    CHECK_FALSE(parseRetailAccounts({}, now, bank));
}

TEST_CASE("Retail accounts enforce record limits and a valid observation time") {
    const auto bank = GENERATE(RetailBank::Alfa, RetailBank::Vtb);
    QJsonArray accounts;
    for (int index = 0; index < 256; ++index) {
        accounts.append(QJsonObject{{"id", QString::number(index)},
                                   {"currency", "RUB"}, {"balanceMinor", "0"}});
    }
    const auto now = QDateTime::currentDateTimeUtc();
    CHECK(parseRetailAccounts(accounts, now, bank));
    CHECK_FALSE(parseRetailAccounts(accounts, {}, bank));
    accounts.append(accounts.first());
    const auto oversized = parseRetailAccounts(accounts, now, bank);
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::SecurityViolation);
}
