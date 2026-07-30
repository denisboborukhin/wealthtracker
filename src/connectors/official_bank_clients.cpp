module;

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <optional>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr qsizetype maximumResponseBytes = 20 * 1024 * 1024;
constexpr auto alfaHost = "baas.alfabank.ru";
constexpr auto tbankBusinessHost = "business.tbank.ru";
constexpr auto modulbankHost = "api.modulbank.ru";

bool validBearerToken(const QByteArray &token)
{
    if (token.size() < 8 || token.size() > 16 * 1024) {
        return false;
    }
    return std::ranges::none_of(token, [](const char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte <= 0x20 || byte == 0x7f;
    });
}

ConnectorResult<QJsonDocument> parseDocument(const QByteArray &json, const QString &source)
{
    if (json.isEmpty() || json.size() > maximumResponseBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("%1 response is empty or exceeds the safety limit").arg(source),
            {}, false});
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError
        || (!document.isObject() && !document.isArray())) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed,
            QStringLiteral("Cannot parse the %1 response").arg(source),
            error.errorString(), false});
    }
    return document;
}

QString normalizedCurrency(QString currency)
{
    currency = currency.trimmed().toUpper();
    if (currency == QStringLiteral("RUR") || currency == QStringLiteral("643")
        || currency.isEmpty()) {
        return QStringLiteral("RUB");
    }
    return currency;
}

std::optional<std::int64_t> jsonInteger(const QJsonValue &value)
{
    bool ok = false;
    if (value.isString()) {
        const auto result = value.toString().toLongLong(&ok);
        return ok ? std::optional(result) : std::nullopt;
    }
    if (value.isDouble()) {
        const auto number = value.toDouble();
        constexpr double largestExactJsonInteger = 9'007'199'254'740'991.0;
        if (number >= -largestExactJsonInteger && number <= largestExactJsonInteger
            && number == std::trunc(number)) {
            return static_cast<std::int64_t>(number);
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> decimalMinorUnits(const QJsonValue &value)
{
    if (value.isDouble()) {
        const long double number = value.toDouble();
        const long double scaled = number * 100.0L;
        if (!std::isfinite(static_cast<double>(scaled))
            || scaled <= static_cast<long double>(std::numeric_limits<std::int64_t>::min())
            || scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(std::round(scaled));
    }
    if (!value.isString()) {
        return std::nullopt;
    }

    static const QRegularExpression pattern(
        QStringLiteral(R"(^([+-]?)(\d+)(?:[.,](\d{1,9}))?$)"));
    const auto match = pattern.match(value.toString().trimmed());
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
    const auto cents = fraction.left(2).toInt();
    if (whole == maximumMinor / 100
        && static_cast<unsigned>(cents) > maximumMinor % 100) {
        return std::nullopt;
    }
    const bool roundUp = fraction.size() > 2 && fraction.at(2) >= QLatin1Char('5');
    auto minor = static_cast<std::int64_t>(whole * 100 + static_cast<unsigned>(cents));
    if (roundUp) {
        if (minor == std::numeric_limits<std::int64_t>::max()) {
            return std::nullopt;
        }
        ++minor;
    }
    return match.captured(1) == QStringLiteral("-") ? -minor : minor;
}

std::optional<Money> decimalMoney(const QJsonValue &value, const QString &currency)
{
    const auto minor = decimalMinorUnits(value);
    if (!minor) {
        return std::nullopt;
    }
    return Money{*minor, normalizedCurrency(currency)};
}

std::optional<Money> alfaMoney(const QJsonObject &object, const QString &valueKey)
{
    const auto raw = jsonInteger(object.value(valueKey));
    const auto factor = jsonInteger(object.value(QStringLiteral("minorUnits"))).value_or(100);
    if (!raw || factor <= 0) {
        return std::nullopt;
    }
    const long double scaled =
        static_cast<long double>(*raw) * 100.0L / static_cast<long double>(factor);
    if (!std::isfinite(static_cast<double>(scaled))
        || scaled <= static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return Money{static_cast<std::int64_t>(std::round(scaled)),
                 normalizedCurrency(object.value(QStringLiteral("currency")).toString())};
}

QString firstString(const QJsonObject &object, const std::initializer_list<QString> &keys)
{
    for (const auto &key : keys) {
        const auto value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
        if (value.isDouble() && value.toDouble() == std::trunc(value.toDouble())) {
            return QString::number(static_cast<qlonglong>(value.toDouble()));
        }
    }
    return {};
}

QJsonArray arrayFromDocument(const QJsonDocument &document, const QString &key)
{
    return document.isArray() ? document.array() : document.object().value(key).toArray();
}

std::optional<QDateTime> parseBankDateTime(const QString &value)
{
    auto result = QDateTime::fromString(value, Qt::ISODate);
    if (result.isValid()) {
        return result;
    }
    const auto date = QDate::fromString(value, Qt::ISODate);
    if (!date.isValid()) {
        return std::nullopt;
    }
    return QDateTime(date, QTime(0, 0), QTimeZone::UTC);
}

QJsonObject readOnlyTokenSchema()
{
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("credentialReference")}},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("credentialReference"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("writeOnly"), true}}}}},
        {QStringLiteral("additionalProperties"), false},
    };
}

ConnectorResult<QNetworkRequest>
authenticatedRequest(HostAllowlist &allowlist, const QUrl &url, const QByteArray &token,
                     const QString &connectorName)
{
    if (!validBearerToken(token)) {
        return std::unexpected(ConnectorError{
            ErrorCode::AuthenticationFailed,
            QStringLiteral("%1 requires a valid read-only bearer token").arg(connectorName),
            {}, false});
    }
    auto request = allowlist.makeRequest(url);
    if (!request) {
        return std::unexpected(request.error());
    }
    request->setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);
    request->setRawHeader("Accept", "application/json");
    return request;
}

std::optional<int> nonNegativeCursor(const QByteArray &cursor)
{
    if (cursor.isEmpty()) {
        return 0;
    }
    bool ok = false;
    const auto result = cursor.toInt(&ok);
    return ok && result >= 0 ? std::optional(result) : std::nullopt;
}

std::optional<Money> tbankMoney(const QJsonValue &value, const QString &fallbackCurrency)
{
    if (value.isObject()) {
        const auto object = value.toObject();
        const auto currency = firstString(
            object, {QStringLiteral("currency"), QStringLiteral("currencyName")});
        for (const auto &key :
             {QStringLiteral("amount"), QStringLiteral("value"), QStringLiteral("sum")}) {
            if (object.contains(key)) {
                if (const auto result =
                        decimalMoney(object.value(key),
                                     currency.isEmpty() ? fallbackCurrency : currency);
                    result) {
                    return result;
                }
            }
        }
    }
    return decimalMoney(value, fallbackCurrency);
}

QString maskedAccountNumber(const QString &number)
{
    return number.size() > 4 ? QStringLiteral("•••• %1").arg(number.right(4)) : number;
}

} // namespace

AlfaBankClient::AlfaBankClient(QNetworkAccessManager &network)
    : network_(network), allowlist_({QString::fromLatin1(alfaHost)})
{
}

ConnectorManifest AlfaBankClient::manifest() const
{
    return {QStringLiteral("alfa-b2c-readonly-v1"),
            QStringLiteral("alfabank"),
            QStringLiteral("Альфа-Банк B2C (только чтение)"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync},
            readOnlyTokenSchema(),
            {QString::fromLatin1(alfaHost)}};
}

QString AlfaBankClient::restPath(const AlfaBankReadMethod method)
{
    switch (method) {
    case AlfaBankReadMethod::Accounts:
        return QStringLiteral("/api/pp/v1/accounts");
    case AlfaBankReadMethod::Operations:
        return QStringLiteral("/api/pp/v1/operations");
    }
    return {};
}

bool AlfaBankClient::isReadOnlyPath(const QStringView path)
{
    return path == restPath(AlfaBankReadMethod::Accounts)
        || path == restPath(AlfaBankReadMethod::Operations);
}

ConnectorResult<QNetworkReply *>
AlfaBankClient::discoverAccounts(const QByteArray &readOnlyToken)
{
    const QUrl url(QStringLiteral("https://%1%2")
                       .arg(QString::fromLatin1(alfaHost),
                            restPath(AlfaBankReadMethod::Accounts)));
    return get(url, readOnlyToken);
}

ConnectorResult<QVector<QNetworkReply *>>
AlfaBankClient::synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyToken)
{
    if (request.accountExternalId.trimmed().isEmpty() || request.pageSize < 1
        || request.pageSize > 100) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Alfa synchronization needs an account and page size from 1 to 100"),
            {}, false});
    }
    const auto offset = nonNegativeCursor(request.cursor);
    if (!offset || *offset % request.pageSize != 0) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Alfa pagination cursor must be a non-negative page offset"), {},
            false});
    }
    QUrl url(QStringLiteral("https://%1%2")
                 .arg(QString::fromLatin1(alfaHost),
                      restPath(AlfaBankReadMethod::Operations)));
    QUrlQuery query;
    const auto from = request.from.value_or(
        QDateTime(QDate::currentDate().addMonths(-1), QTime(0, 0), QTimeZone::UTC));
    query.addQueryItem(QStringLiteral("dateFrom"), from.date().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("dateTo"), QDate::currentDate().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("accounts"), request.accountExternalId);
    query.addQueryItem(QStringLiteral("offset"), QString::number(*offset));
    query.addQueryItem(QStringLiteral("limit"), QString::number(request.pageSize));
    url.setQuery(query);
    auto reply = get(url, readOnlyToken);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    return QVector<QNetworkReply *>{*reply};
}

ConnectorResult<QNetworkReply *>
AlfaBankClient::get(const QUrl &url, const QByteArray &readOnlyToken)
{
    if (!isReadOnlyPath(url.path())) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The Alfa method is not in the read-only allowlist"), {}, false});
    }
    auto request =
        authenticatedRequest(allowlist_, url, readOnlyToken, QStringLiteral("Alfa API"));
    if (!request) {
        return std::unexpected(request.error());
    }
    return network_.get(*request);
}

ConnectorResult<NormalizedBatch>
AlfaBankClient::parseAccountsResponse(const QByteArray &json, const QDateTime &observedAt)
{
    if (!observedAt.isValid()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("A valid observation time is required"), {},
            false});
    }
    const auto document = parseDocument(json, QStringLiteral("Alfa accounts"));
    if (!document) {
        return std::unexpected(document.error());
    }
    NormalizedBatch batch;
    const auto accounts = arrayFromDocument(*document, QStringLiteral("accounts"));
    for (qsizetype index = 0; index < accounts.size(); ++index) {
        const auto item = accounts.at(index).toObject();
        const auto id = item.value(QStringLiteral("number")).toString().trimmed();
        const auto balance =
            alfaMoney(item.value(QStringLiteral("balance")).toObject(),
                      QStringLiteral("amount"));
        if (id.isEmpty() || !balance) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-alfa-account"),
                 QStringLiteral("Alfa returned an account with invalid required fields"),
                 index});
            continue;
        }
        const auto name = item.value(QStringLiteral("mnemonic")).toString().trimmed();
        const auto type = item.value(QStringLiteral("typeDescription")).toString().trimmed();
        const Provenance provenance{
            QStringLiteral("alfa-b2c-readonly-v1"), {}, id, index};
        batch.accounts.push_back(
            {id,
             name.isEmpty() ? maskedAccountNumber(id) : name,
             type.isEmpty() ? item.value(QStringLiteral("type")).toString() : type,
             balance->currency,
             maskedAccountNumber(id),
             item.value(QStringLiteral("status")).toString()
                 != QStringLiteral("ACTIVE"),
             provenance});
        batch.balances.push_back({id, observedAt, *balance, provenance});
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
AlfaBankClient::parseOperationsResponse(const QByteArray &json,
                                        const QString &accountExternalId,
                                        const int currentOffset,
                                        const int requestedPageSize)
{
    if (accountExternalId.trimmed().isEmpty() || currentOffset < 0
        || requestedPageSize < 1 || requestedPageSize > 100) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Alfa operation parsing needs a valid account and page"), {},
            false});
    }
    const auto document = parseDocument(json, QStringLiteral("Alfa operations"));
    if (!document) {
        return std::unexpected(document.error());
    }
    NormalizedBatch batch;
    const auto operations = arrayFromDocument(*document, QStringLiteral("operations"));
    for (qsizetype index = 0; index < operations.size(); ++index) {
        const auto item = operations.at(index).toObject();
        const auto id = item.value(QStringLiteral("id")).toString().trimmed();
        const auto occurredAt =
            parseBankDateTime(item.value(QStringLiteral("dateTime")).toString());
        auto amount = alfaMoney(item.value(QStringLiteral("amount")).toObject(),
                                QStringLiteral("value"));
        if (id.isEmpty() || !occurredAt || !amount) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-alfa-operation"),
                 QStringLiteral("Alfa returned an operation with invalid required fields"),
                 index});
            continue;
        }
        if (item.value(QStringLiteral("direction")).toString()
            == QStringLiteral("EXPENSE")) {
            amount->minorUnits = -std::abs(amount->minorUnits);
        } else {
            amount->minorUnits = std::abs(amount->minorUnits);
        }
        const auto category = item.value(QStringLiteral("category")).toObject();
        batch.transactions.push_back(
            {id,
             accountExternalId,
             occurredAt->date(),
             *occurredAt,
             *amount,
             item.value(QStringLiteral("title")).toString(),
             item.value(QStringLiteral("terminal")).toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             category.value(QStringLiteral("name")).toString(),
             item.value(QStringLiteral("mcc")).toString(),
             {{QStringLiteral("comment"), item.value(QStringLiteral("comment")).toString()},
              {QStringLiteral("status"), item.value(QStringLiteral("status")).toString()}},
             {QStringLiteral("alfa-b2c-readonly-v1"), {}, id, index}});
    }
    if (operations.size() == requestedPageSize) {
        batch.nextCursor = QByteArray::number(currentOffset + operations.size());
    }
    return batch;
}

TBankBusinessClient::TBankBusinessClient(QNetworkAccessManager &network)
    : network_(network), allowlist_({QString::fromLatin1(tbankBusinessHost)})
{
}

ConnectorManifest TBankBusinessClient::manifest() const
{
    return {QStringLiteral("tbank-business-readonly-v1"),
            QStringLiteral("tbank"),
            QStringLiteral("Т-Банк Бизнес (только чтение)"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync},
            readOnlyTokenSchema(),
            {QString::fromLatin1(tbankBusinessHost)}};
}

QString TBankBusinessClient::restPath(const TBankBusinessReadMethod method)
{
    switch (method) {
    case TBankBusinessReadMethod::Accounts:
        return QStringLiteral("/openapi/api/v4/bank-accounts");
    case TBankBusinessReadMethod::Statement:
        return QStringLiteral("/openapi/api/v1/statement");
    }
    return {};
}

bool TBankBusinessClient::isReadOnlyPath(const QStringView path)
{
    return path == restPath(TBankBusinessReadMethod::Accounts)
        || path == restPath(TBankBusinessReadMethod::Statement);
}

ConnectorResult<QNetworkReply *>
TBankBusinessClient::discoverAccounts(const QByteArray &readOnlyToken)
{
    QUrl url(QStringLiteral("https://%1%2")
                 .arg(QString::fromLatin1(tbankBusinessHost),
                      restPath(TBankBusinessReadMethod::Accounts)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("withInvest"), QStringLiteral("false"));
    url.setQuery(query);
    return get(url, readOnlyToken);
}

ConnectorResult<QVector<QNetworkReply *>>
TBankBusinessClient::synchronize(const LiveSyncRequest &request,
                                 const QByteArray &readOnlyToken)
{
    static const QRegularExpression accountPattern(QStringLiteral(R"(^\d{20}$)"));
    if (!accountPattern.match(request.accountExternalId).hasMatch()
        || request.pageSize < 1 || request.pageSize > 5000
        || request.cursor.size() > 100) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral(
                "T-Bank Business synchronization needs a 20-digit account and valid page"),
            {}, false});
    }
    QUrl url(QStringLiteral("https://%1%2")
                 .arg(QString::fromLatin1(tbankBusinessHost),
                      restPath(TBankBusinessReadMethod::Statement)));
    QUrlQuery query;
    const auto from = request.from.value_or(
        QDateTime(QDate::currentDate().addMonths(-1), QTime(0, 0), QTimeZone::UTC));
    const QDateTime until(QDate::currentDate().addDays(1), QTime(0, 0), QTimeZone::UTC);
    query.addQueryItem(QStringLiteral("accountNumber"), request.accountExternalId);
    query.addQueryItem(QStringLiteral("from"), from.toUTC().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("to"), until.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("limit"), QString::number(request.pageSize));
    query.addQueryItem(QStringLiteral("operationStatus"), QStringLiteral("Transaction"));
    if (request.cursor.isEmpty()) {
        query.addQueryItem(QStringLiteral("withBalances"), QStringLiteral("true"));
    } else {
        query.addQueryItem(QStringLiteral("cursor"), QString::fromUtf8(request.cursor));
    }
    url.setQuery(query);
    auto reply = get(url, readOnlyToken);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    return QVector<QNetworkReply *>{*reply};
}

ConnectorResult<QNetworkReply *>
TBankBusinessClient::get(const QUrl &url, const QByteArray &readOnlyToken)
{
    if (!isReadOnlyPath(url.path())) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The T-Bank Business method is not in the read-only allowlist"),
            {}, false});
    }
    auto request = authenticatedRequest(
        allowlist_, url, readOnlyToken, QStringLiteral("T-Bank Business API"));
    if (!request) {
        return std::unexpected(request.error());
    }
    request->setRawHeader("X-Request-Id",
                          QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    return network_.get(*request);
}

ConnectorResult<NormalizedBatch>
TBankBusinessClient::parseAccountsResponse(const QByteArray &json,
                                           const QDateTime &observedAt)
{
    if (!observedAt.isValid()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("A valid observation time is required"), {},
            false});
    }
    const auto document = parseDocument(json, QStringLiteral("T-Bank Business accounts"));
    if (!document) {
        return std::unexpected(document.error());
    }
    NormalizedBatch batch;
    const auto accounts = arrayFromDocument(*document, QStringLiteral("accounts"));
    for (qsizetype index = 0; index < accounts.size(); ++index) {
        const auto item = accounts.at(index).toObject();
        const auto id =
            firstString(item, {QStringLiteral("accountNumber"), QStringLiteral("id")});
        const auto currency = normalizedCurrency(
            firstString(item, {QStringLiteral("currency"), QStringLiteral("currencyCode")}));
        auto balance = tbankMoney(item.value(QStringLiteral("balance")), currency);
        if (!balance && item.value(QStringLiteral("balance")).isObject()) {
            const auto balanceObject = item.value(QStringLiteral("balance")).toObject();
            balance = tbankMoney(balanceObject.value(QStringLiteral("amount")), currency);
        }
        if (id.isEmpty()) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-tbank-account"),
                 QStringLiteral("T-Bank Business returned an account without a number"),
                 index});
            continue;
        }
        const auto name =
            firstString(item, {QStringLiteral("name"), QStringLiteral("accountName")});
        const auto status =
            firstString(item, {QStringLiteral("status"), QStringLiteral("accountStatus")});
        const Provenance provenance{
            QStringLiteral("tbank-business-readonly-v1"), {}, id, index};
        batch.accounts.push_back(
            {id,
             name.isEmpty() ? maskedAccountNumber(id) : name,
             firstString(item,
                         {QStringLiteral("accountType"), QStringLiteral("type")}),
             currency,
             maskedAccountNumber(id),
             status.contains(QStringLiteral("CLOSED"), Qt::CaseInsensitive),
             provenance});
        if (balance) {
            batch.balances.push_back({id, observedAt, *balance, provenance});
        }
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
TBankBusinessClient::parseStatementResponse(const QByteArray &json,
                                            const QString &accountExternalId)
{
    if (accountExternalId.trimmed().isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("An account ID is required"), {}, false});
    }
    const auto document = parseDocument(json, QStringLiteral("T-Bank Business statement"));
    if (!document) {
        return std::unexpected(document.error());
    }
    const auto root = document->isObject() ? document->object() : QJsonObject{};
    NormalizedBatch batch;
    const auto operations = document->isArray()
        ? document->array()
        : root.value(QStringLiteral("operations")).toArray();
    for (qsizetype index = 0; index < operations.size(); ++index) {
        const auto item = operations.at(index).toObject();
        const auto id =
            firstString(item, {QStringLiteral("operationId"), QStringLiteral("id")});
        const auto dateText = firstString(
            item, {QStringLiteral("operationDate"), QStringLiteral("trxnPostDate"),
                   QStringLiteral("transactionDate"), QStringLiteral("docDate")});
        const auto occurredAt = parseBankDateTime(dateText);
        const auto currency = normalizedCurrency(
            firstString(item, {QStringLiteral("currency"), QStringLiteral("currencyCode")}));
        auto amount = tbankMoney(item.value(QStringLiteral("credit")), currency);
        bool isCredit = amount && amount->minorUnits != 0;
        if (!isCredit) {
            amount = tbankMoney(item.value(QStringLiteral("debit")), currency);
        }
        if (!amount && item.contains(QStringLiteral("amount"))) {
            amount = tbankMoney(item.value(QStringLiteral("amount")), currency);
            isCredit = firstString(
                           item, {QStringLiteral("direction"),
                                  QStringLiteral("operationType")})
                           .contains(QStringLiteral("INCOME"), Qt::CaseInsensitive);
        }
        if (id.isEmpty() || !occurredAt || !amount) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-tbank-operation"),
                 QStringLiteral(
                     "T-Bank Business returned an operation with invalid required fields"),
                 index});
            continue;
        }
        amount->minorUnits =
            isCredit ? std::abs(amount->minorUnits) : -std::abs(amount->minorUnits);
        const auto merchant = item.value(QStringLiteral("merch")).toObject();
        const auto categoryValue = item.value(QStringLiteral("category"));
        const auto category = categoryValue.isObject()
            ? firstString(categoryValue.toObject(),
                          {QStringLiteral("name"), QStringLiteral("id")})
            : categoryValue.toString();
        batch.transactions.push_back(
            {id,
             accountExternalId,
             occurredAt->date(),
             *occurredAt,
             *amount,
             firstString(item,
                         {QStringLiteral("description"), QStringLiteral("payPurpose")}),
             firstString(merchant,
                         {QStringLiteral("name"), QStringLiteral("merchantName")}),
             category,
             firstString(item, {QStringLiteral("mcc")}),
             {{QStringLiteral("status"),
               firstString(item, {QStringLiteral("operationStatus")})}},
             {QStringLiteral("tbank-business-readonly-v1"), {}, id, index}});
    }
    batch.nextCursor = root.value(QStringLiteral("nextCursor")).toString().toUtf8();
    return batch;
}

ModulbankClient::ModulbankClient(QNetworkAccessManager &network)
    : network_(network), allowlist_({QString::fromLatin1(modulbankHost)})
{
}

ConnectorManifest ModulbankClient::manifest() const
{
    return {QStringLiteral("modulbank-readonly-v1"),
            QStringLiteral("modulbank"),
            QStringLiteral("Модульбанк (только чтение)"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync},
            readOnlyTokenSchema(),
            {QString::fromLatin1(modulbankHost)}};
}

QString ModulbankClient::restPath(const ModulbankReadMethod method,
                                  const QStringView accountExternalId)
{
    switch (method) {
    case ModulbankReadMethod::Accounts:
        return QStringLiteral("/v1/account-info");
    case ModulbankReadMethod::OperationHistory:
        return QStringLiteral("/v1/operation-history/%1").arg(accountExternalId.toString());
    }
    return {};
}

bool ModulbankClient::isReadOnlyPath(const QStringView path)
{
    if (path == restPath(ModulbankReadMethod::Accounts)) {
        return true;
    }
    static const QRegularExpression pattern(
        QStringLiteral(
            R"(^/v1/operation-history/[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)"));
    return pattern.match(path.toString()).hasMatch();
}

ConnectorResult<QNetworkReply *>
ModulbankClient::discoverAccounts(const QByteArray &readOnlyToken)
{
    const QUrl url(QStringLiteral("https://%1%2")
                       .arg(QString::fromLatin1(modulbankHost),
                            restPath(ModulbankReadMethod::Accounts)));
    return post(url, {}, readOnlyToken);
}

ConnectorResult<QVector<QNetworkReply *>>
ModulbankClient::synchronize(const LiveSyncRequest &request,
                             const QByteArray &readOnlyToken)
{
    const auto path =
        restPath(ModulbankReadMethod::OperationHistory, request.accountExternalId);
    const auto offset = nonNegativeCursor(request.cursor);
    if (!isReadOnlyPath(path) || !offset || request.pageSize < 1
        || request.pageSize > 50) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral(
                "Modulbank synchronization needs a UUID account and page size from 1 to 50"),
            {}, false});
    }
    QJsonObject body{
        {QStringLiteral("statuses"),
         QJsonArray{QStringLiteral("Executed"), QStringLiteral("Received"),
                    QStringLiteral("PayReceived")}},
        {QStringLiteral("skip"), *offset},
        {QStringLiteral("records"), request.pageSize},
    };
    if (request.from && request.from->isValid()) {
        body.insert(QStringLiteral("from"), request.from->date().toString(Qt::ISODate));
    }
    body.insert(QStringLiteral("till"), QDate::currentDate().toString(Qt::ISODate));
    const QUrl url(
        QStringLiteral("https://%1%2").arg(QString::fromLatin1(modulbankHost), path));
    auto reply = post(url, body, readOnlyToken);
    if (!reply) {
        return std::unexpected(reply.error());
    }
    return QVector<QNetworkReply *>{*reply};
}

ConnectorResult<QNetworkReply *>
ModulbankClient::post(const QUrl &url, const QJsonObject &body,
                      const QByteArray &readOnlyToken)
{
    if (!isReadOnlyPath(url.path())) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The Modulbank method is not in the read-only allowlist"), {},
            false});
    }
    auto request =
        authenticatedRequest(allowlist_, url, readOnlyToken, QStringLiteral("Modulbank API"));
    if (!request) {
        return std::unexpected(request.error());
    }
    request->setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return network_.post(*request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

ConnectorResult<NormalizedBatch>
ModulbankClient::parseAccountsResponse(const QByteArray &json,
                                       const QDateTime &observedAt)
{
    if (!observedAt.isValid()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("A valid observation time is required"), {},
            false});
    }
    const auto document = parseDocument(json, QStringLiteral("Modulbank accounts"));
    if (!document) {
        return std::unexpected(document.error());
    }
    NormalizedBatch batch;
    const auto companies = arrayFromDocument(*document, QStringLiteral("companies"));
    for (const auto &companyValue : companies) {
        const auto company = companyValue.toObject();
        const auto companyName = company.value(QStringLiteral("companyName")).toString();
        const auto accounts = company.value(QStringLiteral("bankAccounts")).toArray();
        for (qsizetype index = 0; index < accounts.size(); ++index) {
            const auto item = accounts.at(index).toObject();
            const auto id = item.value(QStringLiteral("id")).toString().trimmed();
            const auto currency =
                normalizedCurrency(item.value(QStringLiteral("currency")).toString());
            const auto balance =
                decimalMoney(item.value(QStringLiteral("balance")), currency);
            if (id.isEmpty() || !balance) {
                batch.warnings.push_back(
                    {WarningSeverity::RejectedRow,
                     QStringLiteral("invalid-modulbank-account"),
                     QStringLiteral(
                         "Modulbank returned an account with invalid required fields"),
                     index});
                continue;
            }
            const auto number = item.value(QStringLiteral("number")).toString();
            const Provenance provenance{
                QStringLiteral("modulbank-readonly-v1"), {}, id, index};
            batch.accounts.push_back(
                {id,
                 item.value(QStringLiteral("accountName")).toString(),
                 item.value(QStringLiteral("category")).toString(),
                 currency,
                 maskedAccountNumber(number),
                 item.value(QStringLiteral("status")).toString()
                     .contains(QStringLiteral("closed"), Qt::CaseInsensitive),
                 provenance});
            batch.balances.push_back({id, observedAt, *balance, provenance});
            if (!companyName.isEmpty()) {
                batch.accounts.back().name =
                    QStringLiteral("%1 · %2").arg(companyName, batch.accounts.back().name);
            }
        }
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
ModulbankClient::parseOperationsResponse(const QByteArray &json,
                                         const QString &accountExternalId,
                                         const int currentOffset,
                                         const int requestedPageSize)
{
    if (accountExternalId.trimmed().isEmpty() || currentOffset < 0
        || requestedPageSize < 1 || requestedPageSize > 50) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Modulbank operation parsing needs a valid account and page"),
            {}, false});
    }
    const auto document = parseDocument(json, QStringLiteral("Modulbank operations"));
    if (!document) {
        return std::unexpected(document.error());
    }
    NormalizedBatch batch;
    const auto operations = arrayFromDocument(*document, QStringLiteral("operations"));
    for (qsizetype index = 0; index < operations.size(); ++index) {
        const auto item = operations.at(index).toObject();
        const auto id = item.value(QStringLiteral("id")).toString().trimmed();
        const auto occurredAt = parseBankDateTime(firstString(
            item, {QStringLiteral("executed"), QStringLiteral("created")}));
        auto amount = decimalMoney(
            item.value(QStringLiteral("amount")),
            normalizedCurrency(item.value(QStringLiteral("currency")).toString()));
        if (id.isEmpty() || !occurredAt || !amount) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow,
                 QStringLiteral("invalid-modulbank-operation"),
                 QStringLiteral(
                     "Modulbank returned an operation with invalid required fields"),
                 index});
            continue;
        }
        const auto category = item.value(QStringLiteral("category")).toString();
        amount->minorUnits = category.compare(QStringLiteral("Debet"),
                                              Qt::CaseInsensitive)
                == 0
            ? std::abs(amount->minorUnits)
            : -std::abs(amount->minorUnits);
        batch.transactions.push_back(
            {id,
             accountExternalId,
             occurredAt->date(),
             *occurredAt,
             *amount,
             item.value(QStringLiteral("paymentPurpose")).toString(),
             item.value(QStringLiteral("contragentName")).toString(),
             {},
             {},
             {{QStringLiteral("status"), item.value(QStringLiteral("status")).toString()},
              {QStringLiteral("direction"), category},
              {QStringLiteral("documentNumber"),
               item.value(QStringLiteral("docNumber")).toString()}},
             {QStringLiteral("modulbank-readonly-v1"), {}, id, index}});
    }
    if (operations.size() == requestedPageSize) {
        batch.nextCursor = QByteArray::number(currentOffset + operations.size());
    }
    return batch;
}

} // namespace wealthtracker::connectors
