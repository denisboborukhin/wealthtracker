module;

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <expected>
#include <optional>
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

[[nodiscard]] std::optional<QString> singleDecodedQueryValue(
    const QUrlQuery& query, const QString& name) {
    const auto values = query.allQueryItemValues(name, QUrl::FullyDecoded);
    if (values.size() != 1) {
        return std::nullopt;
    }
    return values.front();
}

}  // namespace detail

[[nodiscard]] QUrl tbank_session_start_url() {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("origin"),
                       QStringLiteral("web,ib5,platform"));
    return detail::apiUrl(QStringLiteral("session"), query);
}

[[nodiscard]] std::expected<QByteArray, QString>
parse_tbank_session_start(const QByteArray& response) {
    const auto object = detail::parseSuccessfulResponse(response);
    if (!object) {
        return std::unexpected(object.error());
    }
    const auto session = object->value(QStringLiteral("payload")).toString();
    const auto bytes = session.toUtf8();
    if (auto checked = detail::checkedSessionId(bytes); !checked) {
        return std::unexpected(checked.error());
    }
    return bytes;
}

[[nodiscard]] std::expected<QUrl, QString> tbank_authorization_start_url(
    const QByteArray& sessionId) {
    const auto checked = detail::checkedSessionId(sessionId);
    if (!checked) {
        return std::unexpected(checked.error());
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sessionid"), *checked);
    query.addQueryItem(QStringLiteral("origin"),
                       QStringLiteral("web,ib5,platform"));
    query.addQueryItem(QStringLiteral("theme"), QStringLiteral("default"));
    query.addQueryItem(QStringLiteral("display"), QStringLiteral("page"));
    query.addQueryItem(QStringLiteral("complete_uri"),
                       QStringLiteral("https://www.tbank.ru/auth/"));
    return detail::apiUrl(QStringLiteral("session/authorize/"), query);
}

[[nodiscard]] std::expected<QUrl, QString>
validate_tbank_authorization_redirect(const QUrl& redirect) {
    if (!redirect.isValid() || redirect.scheme() != QStringLiteral("https")
        || redirect.host() != QStringLiteral("id.tbank.ru")
        || redirect.port(-1) != -1 || !redirect.userInfo().isEmpty()
        || redirect.path() != QStringLiteral("/auth/authorize")
        || redirect.hasFragment()) {
        return std::unexpected(
            QStringLiteral("Т-Банк вернул небезопасный адрес входа"));
    }
    const QUrlQuery query(redirect);
    const auto state = detail::singleDecodedQueryValue(
        query, QStringLiteral("state"));
    const auto challenge = detail::singleDecodedQueryValue(
        query, QStringLiteral("code_challenge"));
    const auto clientId = detail::singleDecodedQueryValue(
        query, QStringLiteral("client_id"));
    const auto responseType = detail::singleDecodedQueryValue(
        query, QStringLiteral("response_type"));
    const auto callback = detail::singleDecodedQueryValue(
        query, QStringLiteral("redirect_uri"));
    const auto challengeMethod = detail::singleDecodedQueryValue(
        query, QStringLiteral("code_challenge_method"));
    if (!state || !challenge || !clientId || !responseType || !callback
        || !challengeMethod || *clientId != QStringLiteral("portal-api")
        || *responseType != QStringLiteral("code")
        || *callback != QStringLiteral("https://www.tbank.ru/auth/")
        || *challengeMethod != QStringLiteral("S256")
        || state->size() < 64 || state->size() > 4096
        || challenge->size() < 32 || challenge->size() > 128) {
        return std::unexpected(
            QStringLiteral("Т-Банк вернул некорректный адрес входа"));
    }
    return redirect;
}

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
