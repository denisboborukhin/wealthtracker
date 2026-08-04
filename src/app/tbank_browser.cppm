module;

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <string_view>
#include <utility>

export module wt.tbank_browser;

import wt.tbank_session;

export namespace wealthtracker::app {

enum class TBankBrowserEventKind { ready, session, error };

struct TBankBrowserEvent {
    TBankBrowserEventKind kind = TBankBrowserEventKind::error;
    QByteArray sessionId;
    QString message;
};

[[nodiscard]] std::expected<TBankBrowserEvent, QString>
parse_tbank_browser_event(const QByteArray& line) {
    constexpr qsizetype maximumEventSize = 8 * 1024;
    constexpr qsizetype maximumMessageSize = 1024;
    if (line.isEmpty() || line.size() > maximumEventSize) {
        return std::unexpected(
            QStringLiteral("Процесс входа вернул ответ недопустимого размера"));
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return std::unexpected(
            QStringLiteral("Процесс входа вернул некорректный ответ"));
    }

    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready")) {
        return TBankBrowserEvent{TBankBrowserEventKind::ready, {}, {}};
    }
    if (type == QStringLiteral("session")) {
        const auto value = object.value(QStringLiteral("sessionId"));
        if (!value.isString()) {
            return std::unexpected(
                QStringLiteral("Браузер не передал API-сессию Т-Банка"));
        }
        auto sessionId = value.toString().toLatin1();
        const auto view = std::string_view{
            sessionId.constData(),
            static_cast<std::size_t>(sessionId.size())};
        if (!is_valid_tbank_session_id(view)) {
            sessionId.fill('\0');
            return std::unexpected(
                QStringLiteral("Браузер передал некорректную API-сессию"));
        }
        return TBankBrowserEvent{
            TBankBrowserEventKind::session, std::move(sessionId), {}};
    }
    if (type == QStringLiteral("error")) {
        const auto value = object.value(QStringLiteral("message"));
        if (!value.isString()) {
            return std::unexpected(
                QStringLiteral("Процесс входа завершился с ошибкой"));
        }
        const auto message = value.toString();
        const auto hasControl = std::ranges::any_of(
            message, [](const QChar character) {
                return character.unicode() < 0x20;
            });
        if (message.isEmpty() || message.size() > maximumMessageSize
            || hasControl) {
            return std::unexpected(
                QStringLiteral("Процесс входа вернул некорректную ошибку"));
        }
        return TBankBrowserEvent{
            TBankBrowserEventKind::error, {}, message};
    }
    return std::unexpected(
        QStringLiteral("Процесс входа вернул неизвестное событие"));
}

}  // namespace wealthtracker::app
