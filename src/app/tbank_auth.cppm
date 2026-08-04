module;

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <expected>
#include <string_view>

export module wt.tbank_auth;

import wt.tbank_session;

export namespace wealthtracker::app {

enum class TBankAccessLevel { anonymous, client };

namespace detail {

constexpr qsizetype maximumTBankAuthResponseSize = 64 * 1024;

[[nodiscard]] std::expected<QJsonObject, QString> parseSuccessfulResponse(
    const QByteArray& response) {
    if (response.isEmpty()
        || response.size() > maximumTBankAuthResponseSize) {
        return std::unexpected(
            QStringLiteral("Т-Банк вернул ответ недопустимого размера"));
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return std::unexpected(
            QStringLiteral("Т-Банк вернул ответ неизвестного формата"));
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("resultCode")).toString()
        != QStringLiteral("OK")) {
        return std::unexpected(
            QStringLiteral("Т-Банк отклонил запрос авторизации"));
    }
    return object;
}

[[nodiscard]] std::expected<QString, QString> checkedSessionId(
    const QByteArray& sessionId) {
    const auto view = std::string_view{
        sessionId.constData(), static_cast<std::size_t>(sessionId.size())};
    if (!is_valid_tbank_session_id(view)) {
        return std::unexpected(
            QStringLiteral("Т-Банк вернул некорректную API-сессию"));
    }
    return QString::fromLatin1(sessionId);
}

[[nodiscard]] QUrl apiUrl(const QString& path, const QUrlQuery& query) {
    QUrl url(QStringLiteral("https://www.tbank.ru/api/common/v1/") + path);
    url.setQuery(query);
    return url;
}

}  // namespace detail

[[nodiscard]] std::expected<QUrl, QString> tbank_ping_url(
    const QByteArray& sessionId) {
    const auto checked = detail::checkedSessionId(sessionId);
    if (!checked) {
        return std::unexpected(checked.error());
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sessionid"), *checked);
    query.addQueryItem(QStringLiteral("origin"),
                       QStringLiteral("web,ib5,platform"));
    return detail::apiUrl(QStringLiteral("ping"), query);
}

[[nodiscard]] std::expected<TBankAccessLevel, QString>
parse_tbank_access_level(const QByteArray& response) {
    const auto object = detail::parseSuccessfulResponse(response);
    if (!object) {
        return std::unexpected(object.error());
    }
    const auto payload = object->value(QStringLiteral("payload")).toObject();
    const auto accessLevel =
        payload.value(QStringLiteral("accessLevel")).toString();
    if (accessLevel == QStringLiteral("ANONYMOUS")) {
        return TBankAccessLevel::anonymous;
    }
    if (accessLevel == QStringLiteral("CLIENT")) {
        return TBankAccessLevel::client;
    }
    return std::unexpected(
        QStringLiteral("Т-Банк вернул неизвестный уровень доступа"));
}

}  // namespace wealthtracker::app
