#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

import wt.connectors;

TEST_CASE("Alfa retail projection normalizes balances and internal transfers") {
    using wealthtracker::connectors::AlfaRetailClient;
    using wealthtracker::connectors::NormalizedTransactionKind;

    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-08-11T10:00:00Z"), Qt::ISODate);
    const auto parsed = AlfaRetailClient::parseProjection(
        QByteArrayLiteral(R"({
          "version":1,
          "authenticated":true,
          "accounts":[
            {"id":"current-1","name":"Альфа-Карта","kind":"checking","currency":"RUB","maskedNumber":"•••• 1234","balanceMinor":"150025"},
            {"id":"saving-2","name":"Накопительный","kind":"deposit","currency":"RUB","maskedNumber":"","balanceMinor":"250000"}
          ],
          "transactions":[
            {"id":"purchase-1","accountId":"current-1","postingDate":"2026-08-10","occurredAt":"2026-08-10T12:30:00+03:00","amountMinor":"-25990","currency":"RUB","description":"Кофейня","merchant":"Coffee","category":"Кафе","mcc":"5814","internalTransfer":false},
            {"id":"transfer-1","accountId":"saving-2","postingDate":"2026-08-09","occurredAt":"2026-08-09T09:00:00Z","amountMinor":"50000","currency":"RUB","description":"Между своими счетами","merchant":"","category":"Переводы","mcc":"","internalTransfer":true}
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

TEST_CASE("Alfa retail projection rejects authentication and unsafe bounds") {
    using wealthtracker::connectors::AlfaRetailClient;
    using wealthtracker::connectors::ErrorCode;

    const auto expired = AlfaRetailClient::parseProjection(
        QByteArrayLiteral(
            R"({"version":1,"authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(expired);
    CHECK(expired.error().code == ErrorCode::AuthenticationFailed);

    const auto malformed = AlfaRetailClient::parseProjection(
        QByteArrayLiteral(
            R"({"version":1,"authenticated":true,"accounts":[{"id":"a","currency":"RUB","balanceMinor":"1.5"}],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().code == ErrorCode::ParseFailed);

    const auto oversized = AlfaRetailClient::parseProjection(
        QByteArray(8 * 1024 * 1024 + 1, 'x'), QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::SecurityViolation);
}

TEST_CASE("Alfa retail connector is bounded to official web hosts") {
    const wealthtracker::connectors::AlfaRetailClient connector;
    const auto manifest = connector.manifest();
    CHECK(manifest.id == QStringLiteral("alfa-retail"));
    CHECK(manifest.allowedHosts.contains(QStringLiteral("web.alfabank.ru")));
    CHECK(manifest.allowedHosts.contains(QStringLiteral("private.auth.alfabank.ru")));
    CHECK_FALSE(manifest.allowedHosts.contains(QStringLiteral("baas.alfabank.ru")));
}
