#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QString>

import wt.connectors;

TEST_CASE("VTB retail projection normalizes accounts and internal transfers") {
    using wealthtracker::connectors::NormalizedTransactionKind;
    using wealthtracker::connectors::VtbRetailClient;

    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-08-15T06:00:00Z"), Qt::ISODate);
    const auto parsed = VtbRetailClient::parseProjection(
        QByteArrayLiteral(R"({
          "version":1,
          "authenticated":true,
          "accounts":[
            {"id":"card-account","name":"Мультикарта","kind":"checking","currency":"RUB","maskedNumber":"•••• 1234","balanceMinor":"150025"},
            {"id":"saving-account","name":"Сейф","kind":"deposit","currency":"RUB","maskedNumber":"","balanceMinor":"250000"}
          ],
          "transactions":[
            {"id":"purchase-1","accountId":"card-account","postingDate":"2026-08-14","occurredAt":"2026-08-14T12:30:00Z","amountMinor":"-25990","currency":"RUB","description":"Кофейня","merchant":"Coffee","category":"Кафе","mcc":"5814","internalTransfer":false},
            {"id":"transfer-1","accountId":"saving-account","postingDate":"2026-08-13","occurredAt":"2026-08-13T09:00:00Z","amountMinor":"50000","currency":"RUB","description":"Между своими счетами","merchant":"","category":"Внутренний перевод","mcc":"","internalTransfer":true}
          ]
        })"),
        observedAt);

    REQUIRE(parsed);
    REQUIRE(parsed->accounts.size() == 2);
    REQUIRE(parsed->balances.size() == 2);
    CHECK(parsed->balances.front().balance.minorUnits == 150025);
    CHECK(parsed->balances.front().observedAt == observedAt.toUTC());
    REQUIRE(parsed->transactions.size() == 2);
    CHECK(parsed->transactions.front().merchant == QStringLiteral("Coffee"));
    CHECK(parsed->transactions.back().kind ==
          NormalizedTransactionKind::InternalTransfer);
    CHECK(parsed->warnings.isEmpty());
}

TEST_CASE("VTB retail projection rejects expired and unsafe snapshots") {
    using wealthtracker::connectors::ErrorCode;
    using wealthtracker::connectors::VtbRetailClient;

    const auto expired = VtbRetailClient::parseProjection(
        QByteArrayLiteral(
            R"({"version":1,"authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(expired);
    CHECK(expired.error().code == ErrorCode::AuthenticationFailed);

    const auto malformed = VtbRetailClient::parseProjection(
        QByteArrayLiteral(
            R"({"version":1,"authenticated":true,"accounts":[{"id":"a","currency":"RUB","balanceMinor":"1.5"}],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().code == ErrorCode::ParseFailed);

    const auto oversized = VtbRetailClient::parseProjection(
        QByteArray(8 * 1024 * 1024 + 1, 'x'),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::SecurityViolation);
}

TEST_CASE("VTB retail connector is bounded to the official web origin") {
    const wealthtracker::connectors::VtbRetailClient connector;
    const auto manifest = connector.manifest();
    CHECK(manifest.id == QStringLiteral("vtb-retail"));
    REQUIRE(manifest.allowedHosts.size() == 1);
    CHECK(manifest.allowedHosts.front() == QStringLiteral("online.vtb.ru"));
    CHECK_FALSE(manifest.allowedHosts.contains(QStringLiteral("vtbst.ru")));
}
