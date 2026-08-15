module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

constexpr qsizetype maximumProjectionBytes = 8 * 1024 * 1024;
constexpr qsizetype maximumAccounts = 256;
constexpr qsizetype maximumTransactions = 20'000;

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

std::optional<QDateTime> occurredAt(const QJsonValue& value) {
    if (!value.isString()) {
        return std::nullopt;
    }
    auto result = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!result.isValid()) {
        result = QDateTime::fromString(value.toString(), Qt::ISODate);
    }
    return result.isValid() ? std::optional(result.toUTC()) : std::nullopt;
}

QString generatedTransactionId(const QJsonObject& item, const QString& accountId) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(accountId.toUtf8());
    hash.addData(QJsonDocument(item).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(hash.result().toHex());
}

ImportWarning rejectedRow(const QString& code, const QString& message,
                          const qsizetype row) {
    return {WarningSeverity::RejectedRow, code, message, row};
}

} // namespace

ConnectorManifest VtbRetailClient::manifest() const {
    return {QStringLiteral("vtb-retail"),
            QStringLiteral("ВТБ"),
            QStringLiteral("ВТБ Онлайн для физических лиц"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync},
            QJsonObject{},
            {QStringLiteral("online.vtb.ru")}};
}

ConnectorResult<NormalizedBatch>
VtbRetailClient::parseProjection(const QByteArray& json, const QDateTime& observedAt) {
    if (json.isEmpty() || json.size() > maximumProjectionBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("Снимок ВТБ пуст или превышает безопасный лимит"),
            {}, false});
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("ВТБ вернул снимок неизвестного формата"),
            parseError.errorString()));
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        return std::unexpected(invalidProjection(
            QStringLiteral("Версия снимка ВТБ не поддерживается")));
    }
    if (!root.value(QStringLiteral("authenticated")).toBool()) {
        return std::unexpected(ConnectorError{
            ErrorCode::AuthenticationFailed,
            QStringLiteral("Сессия ВТБ истекла. Войдите снова."),
            {}, false});
    }
    if (!observedAt.isValid()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("Время снимка ВТБ некорректно")));
    }

    const auto accounts = root.value(QStringLiteral("accounts")).toArray();
    const auto transactions = root.value(QStringLiteral("transactions")).toArray();
    if (accounts.isEmpty()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("ВТБ не вернул доступных счетов")));
    }
    if (accounts.size() > maximumAccounts || transactions.size() > maximumTransactions) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("Снимок ВТБ превысил безопасный лимит записей"),
            {}, false});
    }

    NormalizedBatch batch;
    QSet<QString> accountIds;
    for (qsizetype index = 0; index < accounts.size(); ++index) {
        if (!accounts.at(index).isObject()) {
            return std::unexpected(invalidProjection(
                QStringLiteral("ВТБ вернул некорректный счёт")));
        }
        const auto item = accounts.at(index).toObject();
        const auto id = boundedText(item.value(QStringLiteral("id")), 160);
        const auto currency = currencyCode(item.value(QStringLiteral("currency")));
        const auto balance = integerString(item.value(QStringLiteral("balanceMinor")));
        if (id.isEmpty() || currency.isEmpty() || !balance) {
            return std::unexpected(invalidProjection(
                QStringLiteral("ВТБ вернул счёт с неполными данными")));
        }
        if (accountIds.contains(id)) {
            batch.warnings.push_back(rejectedRow(
                QStringLiteral("duplicate-vtb-account"),
                QStringLiteral("Повторяющийся счёт ВТБ пропущен"), index));
            continue;
        }
        accountIds.insert(id);
        const auto name = boundedText(item.value(QStringLiteral("name")));
        const Provenance provenance{QStringLiteral("vtb-retail"), {}, id, index};
        batch.accounts.push_back({id,
                                  name.isEmpty() ? QStringLiteral("Счёт ВТБ") : name,
                                  accountKind(item.value(QStringLiteral("kind"))),
                                  currency,
                                  boundedText(item.value(QStringLiteral("maskedNumber")), 32),
                                  false,
                                  provenance});
        batch.balances.push_back(
            {id, observedAt.toUTC(), Money{*balance, currency}, provenance});
    }
    if (batch.accounts.isEmpty()) {
        return std::unexpected(invalidProjection(
            QStringLiteral("ВТБ не вернул уникальных счетов")));
    }

    QSet<QString> transactionIds;
    for (qsizetype index = 0; index < transactions.size(); ++index) {
        if (!transactions.at(index).isObject()) {
            batch.warnings.push_back(rejectedRow(
                QStringLiteral("invalid-vtb-operation"),
                QStringLiteral("Некорректная операция ВТБ пропущена"), index));
            continue;
        }
        const auto item = transactions.at(index).toObject();
        auto accountId = boundedText(item.value(QStringLiteral("accountId")), 160);
        if (accountId.isEmpty() && batch.accounts.size() == 1) {
            accountId = batch.accounts.front().externalId;
        }
        const auto amount = integerString(item.value(QStringLiteral("amountMinor")));
        const auto currency = currencyCode(item.value(QStringLiteral("currency")));
        const auto postingDate =
            QDate::fromString(item.value(QStringLiteral("postingDate")).toString(), Qt::ISODate);
        if (!accountIds.contains(accountId) || !amount || *amount == 0 || currency.isEmpty() ||
            !postingDate.isValid()) {
            batch.warnings.push_back(rejectedRow(
                QStringLiteral("incomplete-vtb-operation"),
                QStringLiteral("Операция ВТБ с неполными данными пропущена"), index));
            continue;
        }
        auto id = boundedText(item.value(QStringLiteral("id")), 192);
        if (id.isEmpty()) {
            id = generatedTransactionId(item, accountId);
        }
        const auto identity = accountId + QLatin1Char('\n') + id;
        if (transactionIds.contains(identity)) {
            batch.warnings.push_back(rejectedRow(
                QStringLiteral("duplicate-vtb-operation"),
                QStringLiteral("Повторяющаяся операция ВТБ пропущена"), index));
            continue;
        }
        transactionIds.insert(identity);
        const auto description = boundedText(item.value(QStringLiteral("description")));
        const Provenance provenance{QStringLiteral("vtb-retail"), {}, id, index};
        batch.transactions.push_back(
            {id,
             accountId,
             postingDate,
             occurredAt(item.value(QStringLiteral("occurredAt"))),
             Money{*amount, currency},
             description.isEmpty() ? QStringLiteral("Операция ВТБ") : description,
             boundedText(item.value(QStringLiteral("merchant"))),
             boundedText(item.value(QStringLiteral("category"))),
             boundedText(item.value(QStringLiteral("mcc")), 8),
             {},
             provenance,
             item.value(QStringLiteral("internalTransfer")).toBool()
                 ? NormalizedTransactionKind::InternalTransfer
                 : NormalizedTransactionKind::Regular});
    }
    return batch;
}

} // namespace wealthtracker::connectors
