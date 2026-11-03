#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QTimer>

import wt.connection_state;

using namespace wealthtracker::app;

TEST_CASE("Connection presentation separates saved material from validated reads") {
    ConnectionState state;
    CHECK(connectionPresentation(state).state == "disconnected");
    state.saved = true;
    CHECK(connectionPresentation(state).state == "saved");
    CHECK(connectionPresentation(state).text.contains("Токен сохранён"));
    state.accessMaterial = ConnectionAccessMaterial::Session;
    CHECK(connectionPresentation(state).text.contains("Сессия сохранена"));
    state.refreshActive = true;
    CHECK(connectionPresentation(state).state == "updating");
    state.loginActive = true;
    CHECK(connectionPresentation(state).text == "Вход в банк");
    state.refreshActive = false;
    state.loginActive = false;
    state.lastRead = ConnectionReadResult::Succeeded;
    CHECK(connectionPresentation(state).state == "ready");
    state.lastRead = ConnectionReadResult::Failed;
    CHECK(connectionPresentation(state).state == "error");
    CHECK(connectionPresentation(state).text.contains("Сессия сохранена"));
    state.saved = false;
    CHECK(connectionPresentation(state).state == "disconnected");
}

TEST_CASE("Unavailable and external connections preserve their own status") {
    ConnectionState state;
    state.saved = true;
    state.loginActive = true;
    state.refreshActive = true;
    state.lastRead = ConnectionReadResult::Succeeded;
    SECTION("Unavailable adapter") { state.available = false; }
    SECTION("Externally managed plugin") { state.externallyManaged = true; }
    const auto display = connectionPresentation(state, QStringLiteral("Выберите bundle"));
    CHECK(display.state == "unavailable");
    CHECK(display.text == "Выберите bundle");
}

TEST_CASE("Connection schedules retain the six hour cadence and minimum retry") {
    const auto now = QDateTime::fromString(QStringLiteral("2026-11-03T12:00:00.000Z"), Qt::ISODateWithMs);
    const auto normal = connectionRefreshSchedule(now, now);
    CHECK(normal.next == now.addSecs(6 * 60 * 60));
    CHECK(normal.delayMilliseconds == 6 * 60 * 60 * 1000);
    const auto overdue = connectionRefreshSchedule(now.addDays(-1), now);
    CHECK(overdue.next == now.addSecs(10));
    CHECK(overdue.delayMilliseconds == 10000);
    const auto near = connectionRefreshSchedule(now.addSecs(-6 * 60 * 60 + 5), now);
    CHECK(near.next == now.addSecs(10));
    CHECK(near.delayMilliseconds == 10000);
    const auto custom = connectionRefreshSchedule(now, now, 10);
    CHECK(custom.next == now.addSecs(10));
}

TEST_CASE("Refresh resets disabled schedules while completion scheduling preserves them") {
    QTimer timer;
    const auto from = QDateTime::currentDateTimeUtc();
    QDateTime next;
    scheduleConnectionRefreshAfter(timer, next, true, from);
    REQUIRE(timer.isActive());
    const auto scheduled = next;
    scheduleConnectionRefreshAfter(timer, next, false, from.addDays(1));
    CHECK(timer.isActive());
    CHECK(next == scheduled);
    refreshConnectionSchedule(timer, next, false, from);
    CHECK_FALSE(timer.isActive());
    CHECK_FALSE(next.isValid());
    refreshConnectionSchedule(timer, next, true, std::nullopt);
    CHECK_FALSE(timer.isActive());
    CHECK_FALSE(next.isValid());
    refreshConnectionSchedule(timer, next, true, from);
    CHECK(timer.isActive());
    CHECK(next == from.addSecs(6 * 60 * 60));
}
