#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QTimer>

import wt.connection_state;

using namespace wealthtracker::app;

TEST_CASE("Connection activity preserves saved and pending T-Bank sessions independently") {
    ConnectionActivityRegistry activities;
    auto& tbank = activities[QStringLiteral("tbank-retail")];
    tbank.savedSession = true;
    tbank.pendingSessionValidation = true;
    CHECK_FALSE(tbank.connected);
    CHECK(tbank.pendingSessionValidation);
    CHECK(tbank.lastRead == ConnectionReadResult::Unverified);

    tbank.pendingSessionValidation = false;
    tbank.lastRead = ConnectionReadResult::Failed;
    const auto activity = activities.value(QStringLiteral("tbank-retail"));
    CHECK_FALSE(activity.connected);
    CHECK(activity.savedSession);
    CHECK_FALSE(activity.pendingSessionValidation);
    CHECK(connectionPresentation(ConnectionState{
        .saved = activity.savedSession || activity.connected,
        .lastRead = activity.lastRead,
        .accessMaterial = ConnectionAccessMaterial::Session,
    }).state == "error");
}

TEST_CASE("Connection activity read resets preserve per-connector activity") {
    ConnectionActivityRegistry activities;
    const auto tbankId = QStringLiteral("tbank-retail");
    const auto investmentId = QStringLiteral("t-invest");
    const auto apiId = QStringLiteral("bank-api");
    activities[tbankId] = ConnectionActivity{
        .connected = true,
        .loginActive = true,
        .refreshActive = true,
        .savedSession = true,
        .pendingSessionValidation = true,
        .lastRead = ConnectionReadResult::Failed,
    };
    activities[investmentId].lastRead = ConnectionReadResult::Succeeded;
    activities[apiId].lastRead = ConnectionReadResult::Failed;

    activities.resetRead(tbankId);
    const auto tbank = activities.value(tbankId);
    CHECK(tbank.lastRead == ConnectionReadResult::Unverified);
    CHECK(tbank.connected);
    CHECK(tbank.loginActive);
    CHECK(tbank.refreshActive);
    CHECK(tbank.savedSession);
    CHECK(tbank.pendingSessionValidation);
    CHECK(activities.value(investmentId).lastRead == ConnectionReadResult::Succeeded);
    CHECK(activities.value(apiId).lastRead == ConnectionReadResult::Failed);

    activities.resetRead(QStringLiteral("missing"));
    CHECK(activities.value(QStringLiteral("missing")).lastRead == ConnectionReadResult::Unverified);
    CHECK_FALSE(activities.value(QStringLiteral("missing")).connected);
    activities[tbankId] = {};
    CHECK_FALSE(activities.value(tbankId).savedSession);
    CHECK(activities.value(investmentId).lastRead == ConnectionReadResult::Succeeded);
}

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
