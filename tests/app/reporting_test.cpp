#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QString>

import wt.reporting;

TEST_CASE("reporting period defaults to the latest inclusive month") {
    using wealthtracker::app::defaultReportingPeriod;

    const auto period = defaultReportingPeriod(QDate{2026, 8, 16});
    CHECK(period.from == QDate{2026, 7, 17});
    CHECK(period.to == QDate{2026, 8, 16});
    CHECK(period.contains(QDate{2026, 7, 17}));
    CHECK(period.contains(QDate{2026, 8, 16}));
    CHECK_FALSE(period.contains(QDate{2026, 7, 16}));
}

TEST_CASE("reporting period parser is strict and rejects reverse ranges") {
    using wealthtracker::app::parseReportingPeriod;
    using wealthtracker::app::reportingInputDate;

    const auto parsed =
        parseReportingPeriod(QStringLiteral("01.06.2026"), QStringLiteral("31.07.2026"));
    REQUIRE(parsed);
    CHECK(reportingInputDate(parsed->from) == QStringLiteral("01.06.2026"));
    CHECK(reportingInputDate(parsed->to) == QStringLiteral("31.07.2026"));

    CHECK_FALSE(parseReportingPeriod(QStringLiteral("1.6.2026"), QStringLiteral("31.07.2026")));
    CHECK_FALSE(parseReportingPeriod(QStringLiteral("31.02.2026"), QStringLiteral("01.03.2026")));
    CHECK_FALSE(parseReportingPeriod(QStringLiteral("02.08.2026"), QStringLiteral("01.08.2026")));
}
