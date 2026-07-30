#include <catch2/catch_test_macros.hpp>
#include <QDate>
#include <QDateTime>
#include <QNetworkRequest>
#include <QUrl>

import wt.connectors;

using namespace wealthtracker::connectors;

TEST_CASE("host allowlist requires exact HTTPS origins")
{
    const HostAllowlist policy({QStringLiteral("api.example.test")});

    CHECK(policy.permits(QUrl(QStringLiteral("https://api.example.test/v1/data"))));
    CHECK(policy.permits(QUrl(QStringLiteral("https://api.example.test:443/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("http://api.example.test/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("https://evil.api.example.test/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("https://api.example.test:8443/v1/data"))));
    CHECK_FALSE(policy.permits(
        QUrl(QStringLiteral("https://user:password@api.example.test/v1/data"))));

    const auto request = policy.makeRequest(
        QUrl(QStringLiteral("https://api.example.test/v1/data")));
    REQUIRE(request);
    CHECK(request->attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
          == QNetworkRequest::ManualRedirectPolicy);
}

TEST_CASE("T-Invest exposes only explicitly read-only REST methods")
{
    CHECK(TInvestClient::isReadOnlyPath(TInvestClient::restPath(TInvestReadMethod::Accounts)));
    CHECK(TInvestClient::isReadOnlyPath(
        TInvestClient::restPath(TInvestReadMethod::OperationsByCursor)));
    CHECK_FALSE(TInvestClient::isReadOnlyPath(
        QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OrdersService/PostOrder")));
}

TEST_CASE("T-Invest response parsers preserve cursor and exact portfolio values")
{
    SECTION("accounts")
    {
        const auto batch = TInvestClient::parseAccountsResponse(QByteArrayLiteral(
            R"({"accounts":[{"id":"broker-1","name":"ИИС","type":"ACCOUNT_TYPE_TINKOFF_IIS","status":"ACCOUNT_STATUS_OPEN"}]})"));
        REQUIRE(batch);
        REQUIRE(batch->accounts.size() == 1);
        CHECK(batch->accounts.front().externalId == QStringLiteral("broker-1"));
        CHECK_FALSE(batch->accounts.front().closed);
    }

    SECTION("cursor operations")
    {
        const auto batch = TInvestClient::parseOperationsResponse(
            QByteArrayLiteral(
                R"({"items":[{"id":"operation-1","date":"2026-07-30T09:15:00Z","type":"OPERATION_TYPE_DIVIDEND","instrumentUid":"uid-1","payment":{"currency":"rub","units":"12","nano":340000000},"quantity":"2"}],"hasNext":true,"nextCursor":"cursor-2"})"),
            QStringLiteral("broker-1"));
        REQUIRE(batch);
        REQUIRE(batch->investmentEvents.size() == 1);
        CHECK(batch->investmentEvents.front().payment
              == Money{1234, QStringLiteral("RUB")});
        CHECK(batch->investmentEvents.front().quantityMantissa
              == QStringLiteral("2000000000"));
        CHECK(batch->nextCursor == QByteArrayLiteral("cursor-2"));
    }

    SECTION("portfolio")
    {
        const auto observedAt = QDateTime::fromString(
            QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);
        const auto batch = TInvestClient::parsePortfolioResponse(
            QByteArrayLiteral(
                R"({"totalAmountPortfolio":{"currency":"rub","units":"1000","nano":0},"positions":[{"instrumentUid":"uid-1","quantity":{"units":"2","nano":500000000},"currentPrice":{"currency":"rub","units":"12","nano":340000000}}]})"),
            QStringLiteral("broker-1"), observedAt);
        REQUIRE(batch);
        REQUIRE(batch->positions.size() == 1);
        CHECK(batch->positions.front().currentValue
              == Money{3085, QStringLiteral("RUB")});
        REQUIRE(batch->balances.size() == 1);
        CHECK(batch->balances.front().balance.minorUnits == 100000);
    }
}

TEST_CASE("Bank of Russia daily XML is parsed without floating point conversion")
{
    const auto rates = CbrFxClient::parseDailyRates(QByteArrayLiteral(
        R"(<?xml version="1.0" encoding="windows-1251"?>
<ValCurs Date="30.07.2026" name="Foreign Currency Market">
  <Valute ID="R01235"><NumCode>840</NumCode><CharCode>USD</CharCode>
    <Nominal>1</Nominal><Name>US Dollar</Name><Value>81,2500</Value>
  </Valute>
  <Valute ID="R01375"><NumCode>156</NumCode><CharCode>CNY</CharCode>
    <Nominal>10</Nominal><Name>Yuan</Name><Value>112,3750</Value>
  </Valute>
</ValCurs>)"));

    REQUIRE(rates);
    REQUIRE(rates->size() == 2);
    CHECK(rates->at(0).effectiveDate == QDate(2026, 7, 30));
    CHECK(rates->at(0).currency == QStringLiteral("USD"));
    CHECK(rates->at(0).rublesPerNominal == QStringLiteral("81.2500"));
    CHECK(rates->at(1).nominal == 10);
}

TEST_CASE("Bank of Russia parser rejects DTDs and oversized responses")
{
    const auto dtd = CbrFxClient::parseDailyRates(
        QByteArrayLiteral("<!DOCTYPE x><ValCurs Date=\"30.07.2026\"></ValCurs>"));
    REQUIRE_FALSE(dtd);
    CHECK(dtd.error().code == ErrorCode::SecurityViolation);

    const auto oversized = CbrFxClient::parseDailyRates(QByteArray(2 * 1024 * 1024 + 1, 'x'));
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::SecurityViolation);
}
