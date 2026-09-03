module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr qsizetype maximumResponseBytes = 20 * 1024 * 1024;
constexpr qsizetype maximumAccounts = 10'000;
constexpr qsizetype maximumOperations = 100'000;
constexpr auto tbankRetailHost = "www.tbank.ru";
constexpr auto apiPrefix = "/api/common/v1/";

ConnectorError invalidResponse(QString message, QString detail = {}) {
    return {ErrorCode::ParseFailed, std::move(message), std::move(detail), false};
}

ConnectorResult<QJsonValue> payloadFromResponse(const QByteArray& json, const QString& operation,
                                                const bool allowNoData = false) {
    if (json.isEmpty() || json.size() > maximumResponseBytes) {
        return std::unexpected(
            ConnectorError{ErrorCode::SecurityViolation,
                           QStringLiteral("Ответ Т-Банка пуст или превышает безопасный лимит"),
                           {},
                           false});
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(invalidResponse(
            QStringLiteral("Т-Банк вернул ответ неизвестного формата"), parseError.errorString()));
    }
    const auto object = document.object();
    const auto resultCode = object.value(QStringLiteral("resultCode")).toString();
    if (allowNoData && resultCode == QStringLiteral("NO_DATA_FOUND")) {
        return QJsonArray{};
    }
    if (resultCode == QStringLiteral("INSUFFICIENT_PRIVILEGES") ||
        resultCode == QStringLiteral("AUTHENTICATION_FAILED")) {
        return std::unexpected(
            ConnectorError{ErrorCode::AuthenticationFailed,
                           QStringLiteral("Сессия Т-Банка истекла. Войдите снова."),
                           {},
                           false});
    }
    if (resultCode == QStringLiteral("REQUEST_RATE_LIMIT_EXCEEDED")) {
        return std::unexpected(ConnectorError{
            ErrorCode::RateLimited,
            QStringLiteral("Т-Банк ограничил частоту запросов %1 (TBR-RATE)").arg(operation),
            {},
            true});
    }
    if (resultCode == QStringLiteral("OPERATION_REJECTED")) {
        return std::unexpected(ConnectorError{
            ErrorCode::RemoteFailure,
            QStringLiteral("Т-Банк отклонил запрос %1 (TBR-REJECTED)").arg(operation),
            {},
            true});
    }
    if (resultCode == QStringLiteral("INVALID_REQUEST_DATA")) {
        return std::unexpected(ConnectorError{
            ErrorCode::RemoteFailure,
            QStringLiteral("Т-Банк отклонил формат запроса %1 (TBR-INVALID)").arg(operation),
            {},
            false});
    }
    if (resultCode != QStringLiteral("OK")) {
        return std::unexpected(ConnectorError{
            ErrorCode::RemoteFailure, QStringLiteral("Т-Банк отклонил запрос %1").arg(operation),
            {}, true});
    }
    return object.value(QStringLiteral("payload"));
}

std::optional<std::int64_t> integerValue(const QJsonValue& value) {
    bool ok = false;
    if (value.isString()) {
        const auto parsed = value.toString().toLongLong(&ok);
        return ok ? std::optional(parsed) : std::nullopt;
    }
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    constexpr double largestExactJsonInteger = 9'007'199'254'740'991.0;
    if (!std::isfinite(number) || number != std::trunc(number) ||
        number < -largestExactJsonInteger || number > largestExactJsonInteger) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
}

std::optional<std::int64_t> minorUnits(const QJsonValue& value) {
    const auto amount = value.isObject() ? value.toObject().value(QStringLiteral("value")) : value;
    if (amount.isDouble()) {
        const long double scaled = amount.toDouble() * 100.0L;
        if (!std::isfinite(static_cast<double>(scaled)) ||
            scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
            scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(std::round(scaled));
    }
    if (!amount.isString()) {
        return std::nullopt;
    }

    static const QRegularExpression pattern(QStringLiteral(R"(^([+-]?)(\d+)(?:[.,](\d{1,9}))?$)"));
    const auto match = pattern.match(amount.toString().trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const auto whole = match.captured(2).toULongLong(&ok);
    constexpr auto maximumMinor =
        static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max());
    if (!ok || whole > maximumMinor / 100) {
        return std::nullopt;
    }
    auto fraction = match.captured(3);
    while (fraction.size() < 3) {
        fraction.append(QLatin1Char('0'));
    }
    const auto cents = static_cast<unsigned>(fraction.left(2).toUInt());
    if (whole == maximumMinor / 100 && cents > maximumMinor % 100) {
        return std::nullopt;
    }
    auto minor = static_cast<std::int64_t>(whole * 100 + cents);
    if (fraction.size() > 2 && fraction.at(2) >= QLatin1Char('5')) {
        if (minor == std::numeric_limits<std::int64_t>::max()) {
            return std::nullopt;
        }
        ++minor;
    }
    return match.captured(1) == QStringLiteral("-") ? -minor : minor;
}

QString currencyFromValue(const QJsonValue& value) {
    QJsonValue candidate = value;
    if (candidate.isObject()) {
        const auto object = candidate.toObject();
        candidate = object.value(QStringLiteral("currency"));
        if (candidate.isObject()) {
            const auto currency = candidate.toObject();
            candidate = currency.value(QStringLiteral("name"));
            if (!candidate.isString()) {
                candidate = currency.value(QStringLiteral("strCode"));
            }
        }
        if (!candidate.isString()) {
            candidate = object.value(QStringLiteral("strCode"));
        }
        if (!candidate.isString()) {
            candidate = object.value(QStringLiteral("name"));
        }
    }
    auto result = candidate.toString().trimmed().toUpper();
    if (result == QStringLiteral("RUR") || result == QStringLiteral("643")) {
        result = QStringLiteral("RUB");
    }
    if (result.size() != 3 ||
        !std::ranges::all_of(result, [](const QChar character) { return character.isLetter(); })) {
        return {};
    }
    return result;
}

QString firstCurrency(const QJsonObject& object) {
    for (const auto& key :
         {QStringLiteral("moneyAmount"), QStringLiteral("accountBalance"),
          QStringLiteral("creditLimit"), QStringLiteral("debtBalance"),
          QStringLiteral("accountAmount"), QStringLiteral("amount"), QStringLiteral("currency")}) {
        const auto currency = currencyFromValue(object.value(key));
        if (!currency.isEmpty()) {
            return currency;
        }
    }
    return {};
}

QString accountKind(const QString& value) {
    const auto normalized = value.toLower();
    if (normalized == QStringLiteral("credit")) {
        return QStringLiteral("credit-card");
    }
    if (normalized == QStringLiteral("deposit") || normalized == QStringLiteral("multideposit")) {
        return QStringLiteral("deposit");
    }
    if (normalized == QStringLiteral("cashloan") || normalized == QStringLiteral("kupivkredit")) {
        return QStringLiteral("loan");
    }
    if (normalized == QStringLiteral("wallet")) {
        return QStringLiteral("wallet");
    }
    return QStringLiteral("checking");
}

QString boundedText(const QJsonValue& value, const QString& fallback = {}) {
    auto text = value.toString().trimmed();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return (text.isEmpty() ? fallback : text).left(256);
}

QString maskedNumber(const QJsonObject& account) {
    QString candidate = account.value(QStringLiteral("linkedAccountNumber")).toString();
    const auto cards = account.value(QStringLiteral("cards")).toArray();
    if (candidate.isEmpty() && !cards.isEmpty()) {
        candidate = cards.first().toObject().value(QStringLiteral("value")).toString();
    }
    QString digits;
    for (const auto character : candidate) {
        if (character.isDigit()) {
            digits += character;
        }
    }
    return digits.size() >= 4 ? QStringLiteral("•••• %1").arg(digits.last(4)) : QString{};
}

std::optional<QDateTime> operationDate(const QJsonObject& operation) {
    for (const auto& key : {QStringLiteral("operationTime"), QStringLiteral("debitingTime")}) {
        const auto object = operation.value(key).toObject();
        if (const auto milliseconds = integerValue(object.value(QStringLiteral("milliseconds")))) {
            const auto date = QDateTime::fromMSecsSinceEpoch(*milliseconds, QTimeZone::UTC);
            if (date.isValid()) {
                return date;
            }
        }
    }
    const auto raw = operation.value(QStringLiteral("date"));
    if (const auto milliseconds = integerValue(raw)) {
        const auto date = QDateTime::fromMSecsSinceEpoch(*milliseconds, QTimeZone::UTC);
        if (date.isValid()) {
            return date;
        }
    }
    auto date = QDateTime::fromString(raw.toString(), Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(raw.toString(), Qt::ISODate);
    }
    return date.isValid() ? std::optional(date.toUTC()) : std::nullopt;
}

QString operationDescription(const QJsonObject& operation) {
    for (const auto& candidate :
         {operation.value(QStringLiteral("merchant")).toObject().value(QStringLiteral("name")),
          operation.value(QStringLiteral("brand")).toObject().value(QStringLiteral("name")),
          operation.value(QStringLiteral("description")),
          operation.value(QStringLiteral("message"))}) {
        const auto text = boundedText(candidate);
        if (!text.isEmpty()) {
            return text;
        }
    }
    return QStringLiteral("Операция Т-Банка");
}

QString operationId(const QJsonObject& operation, const QString& accountId) {
    auto result = boundedText(operation.value(QStringLiteral("id")));
    if (result.isEmpty()) {
        result = boundedText(operation.value(QStringLiteral("payment"))
                                 .toObject()
                                 .value(QStringLiteral("paymentId")));
    }
    if (!result.isEmpty()) {
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(accountId.toUtf8());
    hash.addData(QJsonDocument(operation).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

bool isValidTBankRetailSessionId(const std::string_view value) {
    return value.size() >= 16 && value.size() <= 4096 &&
           std::ranges::all_of(value, [](const unsigned char character) {
               return (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '.' ||
                      character == '-' || character == '_';
           });
}

TBankRetailClient::TBankRetailClient(QNetworkAccessManager& network)
    : network_(network), allowlist_({QString::fromLatin1(tbankRetailHost)}) {}

ConnectorManifest TBankRetailClient::manifest() const {
    return {QStringLiteral("tbank-retail"),
            QStringLiteral("Т-Банк"),
            QStringLiteral("Т-Банк для физических лиц"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync},
            QJsonObject{},
            allowlist_.allowedHosts()};
}

QString TBankRetailClient::restPath(const TBankRetailReadMethod method) {
    switch (method) {
    case TBankRetailReadMethod::Accounts:
        return QString::fromLatin1(apiPrefix) + QStringLiteral("accounts_light_ib");
    case TBankRetailReadMethod::Operations:
        return QString::fromLatin1(apiPrefix) + QStringLiteral("operations");
    }
    return {};
}

bool TBankRetailClient::isReadOnlyPath(const QStringView path) {
    return path == restPath(TBankRetailReadMethod::Accounts) ||
           path == restPath(TBankRetailReadMethod::Operations);
}

ConnectorResult<QNetworkReply*> TBankRetailClient::get(const QUrl& url,
                                                       const QByteArray& sessionId) {
    const auto view =
        std::string_view{sessionId.constData(), static_cast<std::size_t>(sessionId.size())};
    if (!isValidTBankRetailSessionId(view) || !isReadOnlyPath(url.path())) {
        return std::unexpected(
            ConnectorError{ErrorCode::SecurityViolation,
                           QStringLiteral("Некорректный запрос розничного Т-Банка"),
                           {},
                           false});
    }
    auto checked = allowlist_.makeRequest(url);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    checked->setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    checked->setRawHeader(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
    checked->setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    checked->setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    checked->setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    checked->setAttribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute,
                          maximumResponseBytes);
    checked->setTransferTimeout(std::chrono::seconds(45));
    return network_.get(*checked);
}

ConnectorResult<QNetworkReply*> TBankRetailClient::discoverAccounts(const QByteArray& sessionId) {
    QUrl url(QStringLiteral("https://www.tbank.ru") + restPath(TBankRetailReadMethod::Accounts));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("origin"), QStringLiteral("web,ib5,platform"));
    query.addQueryItem(QStringLiteral("sessionid"), QString::fromLatin1(sessionId));
    url.setQuery(query);
    return get(url, sessionId);
}

ConnectorResult<QVector<QNetworkReply*>>
TBankRetailClient::synchronize(const LiveSyncRequest& request, const QByteArray& sessionId) {
    if (request.accountExternalId.trimmed().isEmpty() || request.accountExternalId.size() > 256 ||
        !request.cursor.isEmpty() || !request.from || !request.from->isValid() || !request.to ||
        !request.to->isValid() ||
        *request.from > *request.to || *request.to > request.from->addYears(2).addDays(1)) {
        return std::unexpected(
            ConnectorError{ErrorCode::InvalidInput,
                           QStringLiteral("Некорректный период или счёт Т-Банка"),
                           {},
                           false});
    }
    QUrl url(QStringLiteral("https://www.tbank.ru") + restPath(TBankRetailReadMethod::Operations));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("origin"), QStringLiteral("web,ib5,platform"));
    query.addQueryItem(QStringLiteral("sessionid"), QString::fromLatin1(sessionId));
    query.addQueryItem(QStringLiteral("account"), request.accountExternalId);
    query.addQueryItem(QStringLiteral("start"), QString::number(request.from->toMSecsSinceEpoch()));
    query.addQueryItem(QStringLiteral("end"), QString::number(request.to->toMSecsSinceEpoch()));
    url.setQuery(query);
    auto reply = get(url, sessionId);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    return QVector<QNetworkReply*>{*reply};
}

ConnectorResult<NormalizedBatch>
TBankRetailClient::parseAccountsResponse(const QByteArray& json, const QDateTime& observedAt) {
    const auto payload = payloadFromResponse(json, QStringLiteral("счетов"));
    if (!payload) {
        return std::unexpected(payload.error());
    }
    if (!payload->isArray() || payload->toArray().size() > maximumAccounts) {
        return std::unexpected(
            invalidResponse(QStringLiteral("Т-Банк вернул некорректный список счетов")));
    }

    NormalizedBatch batch;
    QSet<QString> accountIds;
    for (const auto& value : payload->toArray()) {
        if (!value.isObject()) {
            return std::unexpected(
                invalidResponse(QStringLiteral("Т-Банк вернул некорректную запись счёта")));
        }
        const auto account = value.toObject();
        const auto sourceType = account.value(QStringLiteral("accountType")).toString().trimmed();
        if (sourceType.startsWith(QStringLiteral("external"), Qt::CaseInsensitive) ||
            sourceType.startsWith(QStringLiteral("imported"), Qt::CaseInsensitive) ||
            sourceType.startsWith(QStringLiteral("telecom"), Qt::CaseInsensitive)) {
            continue;
        }
        const auto id = boundedText(account.value(QStringLiteral("id")));
        const auto currency = firstCurrency(account);
        std::optional<std::int64_t> balance;
        const auto available = minorUnits(account.value(QStringLiteral("moneyAmount")));
        const auto creditLimit = minorUnits(account.value(QStringLiteral("creditLimit")));
        const auto statedBalance = minorUnits(account.contains(QStringLiteral("accountBalance"))
                                                  ? account.value(QStringLiteral("accountBalance"))
                                                  : account.value(QStringLiteral("balance")));
        const auto debt = minorUnits(account.contains(QStringLiteral("debtBalance"))
                                         ? account.value(QStringLiteral("debtBalance"))
                                         : account.value(QStringLiteral("debtAmount")));
        if (sourceType.compare(QStringLiteral("credit"), Qt::CaseInsensitive) == 0 && creditLimit &&
            *creditLimit < 0) {
            return std::unexpected(invalidResponse(
                QStringLiteral("Т-Банк вернул отрицательный кредитный лимит"), id.left(64)));
        }
        if (sourceType.compare(QStringLiteral("credit"), Qt::CaseInsensitive) == 0 && available &&
            creditLimit && *creditLimit >= 0 &&
            *available >= std::numeric_limits<std::int64_t>::min() + *creditLimit) {
            balance = *available - *creditLimit;
        } else if (statedBalance) {
            balance = statedBalance;
        } else if (sourceType.compare(QStringLiteral("credit"), Qt::CaseInsensitive) == 0 && debt &&
                   *debt != std::numeric_limits<std::int64_t>::min()) {
            balance = -std::abs(*debt);
        } else {
            balance = available;
        }
        if (id.isEmpty() || accountIds.contains(id) || currency.isEmpty() || !balance) {
            return std::unexpected(invalidResponse(
                QStringLiteral("Т-Банк вернул некорректные поля счёта"), id.left(64)));
        }
        accountIds.insert(id);
        const Provenance provenance{QStringLiteral("tbank-retail"), {}, id, -1};
        batch.accounts.push_back(
            {id, boundedText(account.value(QStringLiteral("name")), QStringLiteral("Счёт Т-Банка")),
             accountKind(sourceType), currency, maskedNumber(account), false, provenance});
        batch.balances.push_back({id, observedAt, Money{*balance, currency}, provenance});
    }
    if (batch.accounts.isEmpty()) {
        return std::unexpected(
            invalidResponse(QStringLiteral("Т-Банк не вернул доступных счетов")));
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
TBankRetailClient::parseOperationsResponse(const QByteArray& json,
                                           const QString& accountExternalId,
                                           const QByteArray& currentCursor) {
    if (accountExternalId.trimmed().isEmpty() || accountExternalId.size() > 256) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("Некорректный счёт Т-Банка"), {}, false});
    }
    if (!currentCursor.isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("Некорректная страница истории Т-Банка"),
            {}, false});
    }
    const auto payload = payloadFromResponse(json, QStringLiteral("операций"), true);
    if (!payload) {
        return std::unexpected(payload.error());
    }
    if (!payload->isArray() || payload->toArray().size() > maximumOperations) {
        return std::unexpected(
            invalidResponse(QStringLiteral("Т-Банк вернул некорректный список операций")));
    }

    NormalizedBatch batch;
    QHash<QString, int> duplicateIds;
    const auto operations = payload->toArray();
    for (qsizetype sourceRow = 0; sourceRow < operations.size(); ++sourceRow) {
        const auto& value = operations.at(sourceRow);
        if (!value.isObject()) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("tbank-operation-shape"),
                 QStringLiteral("Пропущена некорректная операция Т-Банка"), sourceRow});
            continue;
        }
        const auto operation = value.toObject();
        if (operation.value(QStringLiteral("status")).toString() == QStringLiteral("FAILED")) {
            continue;
        }
        const auto date = operationDate(operation);
        const auto amount = minorUnits(operation.contains(QStringLiteral("accountAmount"))
                                           ? operation.value(QStringLiteral("accountAmount"))
                                           : operation.value(QStringLiteral("amount")));
        const auto currency = firstCurrency(operation);
        if (!date || !amount || *amount == 0 || currency.isEmpty() ||
            *amount == std::numeric_limits<std::int64_t>::min()) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("tbank-operation-fields"),
                 QStringLiteral("Пропущена операция Т-Банка с некорректной суммой или датой"),
                 sourceRow});
            continue;
        }
        auto id = operationId(operation, accountExternalId);
        const auto duplicate = duplicateIds.value(id);
        duplicateIds.insert(id, duplicate + 1);
        if (duplicate > 0) {
            id += QStringLiteral(":%1").arg(duplicate);
        }
        const bool credit = operation.value(QStringLiteral("type"))
                                .toString()
                                .compare(QStringLiteral("credit"), Qt::CaseInsensitive) == 0;
        const auto absoluteAmount = std::abs(*amount);
        const auto description = operationDescription(operation);
        auto category = boundedText(operation.value(QStringLiteral("spendingCategory"))
                                        .toObject()
                                        .value(QStringLiteral("name")));
        if (category.isEmpty()) {
            category = boundedText(operation.value(QStringLiteral("category"))
                                       .toObject()
                                       .value(QStringLiteral("name")));
        }
        QString mcc;
        if (const auto parsedMcc = integerValue(operation.value(QStringLiteral("mcc")));
            parsedMcc && *parsedMcc > 10 && *parsedMcc <= 9999) {
            mcc = QString::number(*parsedMcc);
        }
        QMap<QString, QString> fields;
        if (!operation.contains(QStringLiteral("debitingTime")) ||
            operation.value(QStringLiteral("debitingTime")).isNull()) {
            fields.insert(QStringLiteral("hold"), QStringLiteral("true"));
        }
        const auto kind = operation.value(QStringLiteral("isInner")).toBool()
                              ? NormalizedTransactionKind::InternalTransfer
                              : NormalizedTransactionKind::Regular;
        batch.transactions.push_back({id, accountExternalId, date->date(), *date,
                                      Money{credit ? absoluteAmount : -absoluteAmount, currency},
                                      description, description, category, mcc, std::move(fields),
                                      Provenance{QStringLiteral("tbank-retail"), {}, id, sourceRow},
                                      kind});
    }
    batch.warnings.push_back(
        {WarningSeverity::Information, QStringLiteral("tbank-history-unverifiable"),
         QStringLiteral("Полноту ответа Т-Банка за запрошенный период нельзя подтвердить"), -1});
    return batch;
}

} // namespace wealthtracker::connectors
