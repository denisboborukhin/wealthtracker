#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <QVector>

#include <array>
#include <cstdint>
#include <utility>

import wt.transfer_matching;

namespace {

wealthtracker::app::OwnedTransaction transaction(QString connector, QString account, QString id,
                                                 const QDate date, const std::int64_t minorUnits,
                                                 QString currency = QStringLiteral("RUB"),
                                                 const bool alreadyInternal = false) {
    return {std::move(connector),
            std::move(account),
            std::move(id),
            date,
            QDateTime(date, QTime{12, 0}, QTimeZone::UTC),
            minorUnits,
            std::move(currency),
            alreadyInternal};
}

} // namespace

TEST_CASE("equal opposite operations across owned bank connections are transfers") {
    const std::array transactions{
        transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-1"), QStringLiteral("out"),
                    QDate{2026, 8, 10}, -50'000),
        transaction(QStringLiteral("alfa-retail"), QStringLiteral("a-1"), QStringLiteral("in"),
                    QDate{2026, 8, 11}, 50'000),
        transaction(QStringLiteral("vtb-retail"), QStringLiteral("v-1"), QStringLiteral("expense"),
                    QDate{2026, 8, 10}, -50'000),
    };

    const auto matches = wealthtracker::app::findOwnAccountTransfers(transactions);
    REQUIRE(matches.size() == 2);
    CHECK(matches.contains(0));
    CHECK(matches.contains(1));
    CHECK_FALSE(matches.contains(2));
}

TEST_CASE("transfer matching requires amount currency proximity and another connection") {
    const std::array transactions{
        transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-1"), QStringLiteral("out"),
                    QDate{2026, 8, 10}, -50'000),
        transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-2"),
                    QStringLiteral("same-bank"), QDate{2026, 8, 10}, 50'000),
        transaction(QStringLiteral("alfa-retail"), QStringLiteral("a-1"),
                    QStringLiteral("wrong-currency"), QDate{2026, 8, 10}, 50'000,
                    QStringLiteral("USD")),
        transaction(QStringLiteral("vtb-retail"), QStringLiteral("v-1"), QStringLiteral("too-late"),
                    QDate{2026, 8, 14}, 50'000),
    };

    CHECK(wealthtracker::app::findOwnAccountTransfers(transactions).isEmpty());
}

TEST_CASE("bank-marked transfers remain excluded and classify their opposite side") {
    const std::array transactions{
        transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-1"), QStringLiteral("out"),
                    QDate{2026, 8, 10}, -25'000, QStringLiteral("RUB"), true),
        transaction(QStringLiteral("alfa-retail"), QStringLiteral("a-1"), QStringLiteral("in"),
                    QDate{2026, 8, 10}, 25'000),
    };

    const auto matches = wealthtracker::app::findOwnAccountTransfers(transactions);
    REQUIRE(matches.size() == 2);
    CHECK(matches.contains(0));
    CHECK(matches.contains(1));
}

TEST_CASE("bank transfer markers win ambiguous one-to-one matches") {
    const std::array transactions{
        transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-1"),
                    QStringLiteral("regular-out"), QDate{2026, 8, 10}, -25'000),
        transaction(QStringLiteral("alfa-retail"), QStringLiteral("a-1"),
                    QStringLiteral("marked-out"), QDate{2026, 8, 10}, -25'000,
                    QStringLiteral("RUB"), true),
        transaction(QStringLiteral("vtb-retail"), QStringLiteral("v-1"), QStringLiteral("in"),
                    QDate{2026, 8, 10}, 25'000),
    };

    const auto matches = wealthtracker::app::findOwnAccountTransfers(transactions);
    REQUIRE(matches.size() == 2);
    CHECK_FALSE(matches.contains(0));
    CHECK(matches.contains(1));
    CHECK(matches.contains(2));
}

TEST_CASE("highly ambiguous transfers are not inferred") {
    QVector<wealthtracker::app::OwnedTransaction> transactions;
    transactions.push_back(transaction(QStringLiteral("tbank-retail"), QStringLiteral("t-1"),
                                       QStringLiteral("out"), QDate{2026, 8, 10}, -100));
    for (int index = 0; index < 65; ++index) {
        transactions.push_back(transaction(QStringLiteral("alfa-retail"), QStringLiteral("a-1"),
                                           QStringLiteral("in-%1").arg(index), QDate{2026, 8, 10},
                                           100));
    }

    CHECK(wealthtracker::app::findOwnAccountTransfers(transactions).isEmpty());
}
