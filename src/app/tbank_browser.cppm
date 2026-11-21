module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <QVector>

#include <cstddef>
#include <expected>
#include <string_view>
#include <utility>

}

export module wt.tbank_browser;

import wt.connectors;

export namespace wealthtracker::app {

[[nodiscard]] const QString& tbank_session_probe_script() {
    static const QString script = QStringLiteral(R"JS((() => {
  if (location.origin !== 'https://www.tbank.ru') return '[]'
  const sessions = new Set()
  for (const entry of performance.getEntriesByType('resource')) {
    try {
      const url = new URL(entry.name)
      if (url.protocol !== 'https:' ||
          url.hostname !== 'www.tbank.ru' ||
          (url.port !== '' && url.port !== '443') ||
          url.username !== '' || url.password !== '' ||
          !url.pathname.startsWith('/api/common/v1/')) continue
      const values = url.searchParams.getAll('sessionid')
      if (values.length === 1 &&
          /^[A-Za-z0-9._-]{16,4096}$/.test(values[0])) {
        sessions.delete(values[0])
        sessions.add(values[0])
        if (sessions.size > 8) sessions.delete(sessions.values().next().value)
      }
    } catch (ignored) {}
  }
  return JSON.stringify([...sessions])
})())JS");
    return script;
}

[[nodiscard]] std::expected<QVector<QByteArray>, QString>
parse_tbank_session_projection(const QString& projection) {
    constexpr qsizetype maximumProjectionSize = 40 * 1024;
    constexpr qsizetype maximumSessionCount = 8;
    if (projection.isEmpty() || projection.size() > maximumProjectionSize) {
        return std::unexpected(QStringLiteral(
            "Встроенная страница вернула ответ недопустимого размера"));
    }

    QJsonParseError parseError;
    const auto document =
        QJsonDocument::fromJson(projection.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() > maximumSessionCount) {
        return std::unexpected(QStringLiteral(
            "Встроенная страница вернула некорректный ответ"));
    }

    QVector<QByteArray> sessions;
    sessions.reserve(document.array().size());
    for (const auto& value : document.array()) {
        if (!value.isString()) {
            return std::unexpected(QStringLiteral(
                "Встроенная страница вернула некорректную API-сессию"));
        }
        auto session = value.toString().toLatin1();
        const auto view = std::string_view{
            session.constData(), static_cast<std::size_t>(session.size())};
        if (!wealthtracker::connectors::isValidTBankRetailSessionId(view)) {
            session.fill('\0');
            return std::unexpected(QStringLiteral(
                "Встроенная страница вернула некорректную API-сессию"));
        }
        if (!sessions.contains(session)) {
            sessions.push_back(std::move(session));
        } else {
            session.fill('\0');
        }
    }
    return sessions;
}

}  // namespace wealthtracker::app
