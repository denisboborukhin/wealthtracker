module;

#include <QSet>
#include <QString>
#include <QUrl>

#include <expected>

module wt.connectors;

namespace wealthtracker::connectors {

ConnectorResult<void>
validateAuthenticationChallenge(const AuthenticationChallenge &challenge)
{
    if (challenge.sessionId.trimmed().isEmpty() || challenge.title.trimmed().isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Authentication challenges require a session and title"), {}, false});
    }

    if (challenge.action == AuthenticationAction::OpenBrowser) {
        const auto &url = challenge.authorizationUrl;
        if (!url.isValid()
            || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
            || url.host().isEmpty() || !url.userInfo().isEmpty()) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("Browser authentication requires a clean HTTPS URL"), {}, false});
        }
    }

    if (challenge.action == AuthenticationAction::CollectFields
        && (challenge.fields.isEmpty() || challenge.fields.size() > 8)) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Authentication forms require between one and eight fields"), {},
            false});
    }

    QSet<QString> keys;
    for (const auto &field : challenge.fields) {
        const auto key = field.key.trimmed();
        if (key.isEmpty() || field.label.trimmed().isEmpty() || keys.contains(key)) {
            return std::unexpected(ConnectorError{
                ErrorCode::InvalidInput,
                QStringLiteral("Authentication fields require unique keys and labels"), {},
                false});
        }
        keys.insert(key);

        const bool transientSecret =
            field.kind == AuthenticationFieldKind::Password
            || field.kind == AuthenticationFieldKind::OneTimeCode;
        if (transientSecret && field.retention != SecretRetention::Never) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("Passwords and one-time codes must never be persisted"), key,
                false});
        }
    }
    return {};
}

} // namespace wealthtracker::connectors
