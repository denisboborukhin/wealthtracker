module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <optional>
}

export module wt.connection_state;

export namespace wealthtracker::app {

enum class ConnectionReadResult { Unverified, Succeeded, Failed };
enum class ConnectionAccessMaterial { Session, Token };

struct ConnectionActivity {
    bool connected = false;
    bool loginActive = false;
    bool refreshActive = false;
    bool savedSession = false;
    bool pendingSessionValidation = false;
    ConnectionReadResult lastRead = ConnectionReadResult::Unverified;
};

class ConnectionActivityRegistry {
public:
    ConnectionActivity& operator[](const QString& connectorId) {
        return activities_[connectorId];
    }

    [[nodiscard]] ConnectionActivity value(const QString& connectorId) const {
        return activities_.value(connectorId);
    }

    void resetRead(const QString& connectorId) {
        auto activity = activities_.find(connectorId);
        if (activity != activities_.end())
            activity->lastRead = ConnectionReadResult::Unverified;
    }

private:
    QHash<QString, ConnectionActivity> activities_;
};

// Saved access material does not establish that the bank currently returns data.
struct ConnectionState {
    bool available = true;
    bool externallyManaged = false;
    bool saved = false;
    bool loginActive = false;
    bool refreshActive = false;
    ConnectionReadResult lastRead = ConnectionReadResult::Unverified;
    ConnectionAccessMaterial accessMaterial = ConnectionAccessMaterial::Token;
};

struct ConnectionPresentation {
    QString state;
    QString text;
};

[[nodiscard]] ConnectionPresentation connectionPresentation(
    const ConnectionState& connection, const QString& unavailableText = {}) {
    if (!connection.available || connection.externallyManaged)
        return {QStringLiteral("unavailable"), unavailableText};
    if (connection.loginActive || connection.refreshActive)
        return {QStringLiteral("updating"), connection.loginActive
            ? QStringLiteral("Вход в банк") : QStringLiteral("Обновляем…")};
    if (!connection.saved)
        return {QStringLiteral("disconnected"), QStringLiteral("Не подключено")};
    if (connection.lastRead == ConnectionReadResult::Succeeded)
        return {QStringLiteral("ready"), QStringLiteral("Данные получены")};
    const bool session = connection.accessMaterial == ConnectionAccessMaterial::Session;
    if (connection.lastRead == ConnectionReadResult::Failed)
        return {QStringLiteral("error"), session
            ? QStringLiteral("Сессия сохранена · данные не получены")
            : QStringLiteral("Токен сохранён · данные не получены")};
    return {QStringLiteral("saved"), session
        ? QStringLiteral("Сессия сохранена · доступ не проверен")
        : QStringLiteral("Токен сохранён · доступ не проверен")};
}

struct ConnectionRefreshSchedule {
    QDateTime next;
    int delayMilliseconds;
};

[[nodiscard]] ConnectionRefreshSchedule connectionRefreshSchedule(
    const QDateTime& from, const QDateTime& now, const int intervalSeconds = 6 * 60 * 60) {
    auto next = from.toUTC().addSecs(intervalSeconds);
    auto seconds = now.secsTo(next);
    if (seconds < 10) {
        seconds = 10;
        next = now.addSecs(seconds);
    }
    const auto milliseconds = std::min<qint64>(seconds * 1000LL, std::numeric_limits<int>::max());
    return {next, static_cast<int>(milliseconds)};
}

// Refresh resets a previous schedule even when auto-update is disabled.
void refreshConnectionSchedule(QTimer& timer, QDateTime& next, const bool enabled,
                               const std::optional<QDateTime>& lastSync) {
    timer.stop();
    next = {};
    if (!enabled || !lastSync) return;
    const auto schedule = connectionRefreshSchedule(*lastSync, QDateTime::currentDateTimeUtc());
    next = schedule.next;
    timer.start(schedule.delayMilliseconds);
}

// Scheduling after a completion leaves the current schedule untouched if disabled.
void scheduleConnectionRefreshAfter(QTimer& timer, QDateTime& next, const bool enabled,
                                    const QDateTime& from) {
    if (!enabled) return;
    const auto schedule = connectionRefreshSchedule(from, QDateTime::currentDateTimeUtc());
    next = schedule.next;
    timer.start(schedule.delayMilliseconds);
}

} // namespace wealthtracker::app
