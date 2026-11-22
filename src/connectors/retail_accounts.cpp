module;

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr qsizetype maximumAccounts = 256;

ConnectorError invalidProjection(QString message, QString detail = {}) {
    return {ErrorCode::ParseFailed, std::move(message), std::move(detail), false};
}

QString boundedText(const QJsonValue& value, const qsizetype maximum = 256) {
    auto text = value.toString().trimmed();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return text.left(maximum);
}

std::optional<std::int64_t> integerString(const QJsonValue& value) {
    if (!value.isString()) {
        return std::nullopt;
    }
    static const QRegularExpression pattern(QStringLiteral(R"(^[+-]?\d{1,19}$)"));
    const auto text = value.toString();
    if (!pattern.match(text).hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const auto parsed = text.toLongLong(&ok);
    return ok ? std::optional(parsed) : std::nullopt;
}

QString currencyCode(const QJsonValue& value) {
    const auto currency = value.toString().trimmed().toUpper();
    static const QRegularExpression pattern(QStringLiteral(R"(^[A-Z]{3}$)"));
    return pattern.match(currency).hasMatch() ? currency : QString{};
}

QString accountKind(const QJsonValue& value) {
    const auto kind = value.toString().trimmed().toLower();
    static const QSet<QString> supported{
        QStringLiteral("checking"), QStringLiteral("credit-card"),
        QStringLiteral("deposit"), QStringLiteral("loan"),
        QStringLiteral("wallet")};
    return supported.contains(kind) ? kind : QStringLiteral("checking");
}

ImportWarning rejectedRow(const QString& code, const QString& message,
                          const qsizetype row) {
    return {WarningSeverity::RejectedRow, code, message, row};
}

} // namespace

ConnectorResult<NormalizedBatch>
parseRetailAccounts(const QJsonArray& accounts, const QDateTime& observedAt,
                    const RetailBank bank) {
    const auto connectorId = bank == RetailBank::Alfa
        ? QStringLiteral("alfa-retail") : QStringLiteral("vtb-retail");
    const auto bankName = bank == RetailBank::Alfa
        ? QStringLiteral("Альфа-Банка") : QStringLiteral("ВТБ");
    if (!observedAt.isValid()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("Время снимка %1 некорректно").arg(bankName)));
    }

    if (accounts.isEmpty()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("Нет доступных счетов %1").arg(bankName)));
    }
    if (accounts.size() > maximumAccounts) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("Снимок %1 превысил безопасный лимит записей").arg(bankName),
            {}, false});
    }

    NormalizedBatch batch;
    QSet<QString> accountIds;
    for (qsizetype index = 0; index < accounts.size(); ++index) {
        if (!accounts.at(index).isObject()) {
            return std::unexpected(invalidProjection(
                QStringLiteral("Некорректный счёт %1").arg(bankName)));
        }
        const auto item = accounts.at(index).toObject();
        const auto id = boundedText(item.value(QStringLiteral("id")), 160);
        const auto currency = currencyCode(item.value(QStringLiteral("currency")));
        const auto balance = integerString(item.value(QStringLiteral("balanceMinor")));
        if (id.isEmpty() || currency.isEmpty() || !balance) {
            return std::unexpected(invalidProjection(
                QStringLiteral("Счёт %1 с неполными данными").arg(bankName)));
        }
        if (accountIds.contains(id)) {
            batch.warnings.push_back(rejectedRow(
                bank == RetailBank::Alfa ? QStringLiteral("duplicate-alfa-account")
                                         : QStringLiteral("duplicate-vtb-account"),
                QStringLiteral("Повторяющийся счёт %1 пропущен").arg(bankName), index));
            continue;
        }
        accountIds.insert(id);
        const auto name = boundedText(item.value(QStringLiteral("name")));
        const Provenance provenance{connectorId, {}, id, index};
        batch.accounts.push_back({id,
                                  name.isEmpty() ? QStringLiteral("Счёт %1").arg(bankName) : name,
                                  accountKind(item.value(QStringLiteral("kind"))),
                                  currency,
                                  boundedText(item.value(QStringLiteral("maskedNumber")), 32),
                                  false,
                                  provenance});
        batch.balances.push_back(
            {id, observedAt.toUTC(), Money{*balance, currency}, provenance});
    }
    return batch;
}

} // namespace wealthtracker::connectors
