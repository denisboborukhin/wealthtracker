module;

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkReply>
#include <QString>

#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr auto apiHost = "invest-public-api.tbank.ru";
constexpr qsizetype maximumResponseBytes = 20 * 1024 * 1024;
using BigInt = boost::multiprecision::cpp_int;

bool validToken(const QByteArray &token)
{
    return token.size() >= 20 && !token.contains('\r') && !token.contains('\n')
        && !token.contains('\0');
}

ConnectorResult<QJsonObject> parseObject(const QByteArray &json)
{
    if (json.isEmpty() || json.size() > maximumResponseBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The T-Invest response is empty or exceeds the safety limit"), {},
            false});
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("Cannot parse the T-Invest response"),
            error.errorString(), false});
    }
    return document.object();
}

ConnectorResult<QJsonArray> requiredArray(const QJsonObject &object,
                                          const QString &key,
                                          const QString &source)
{
    const auto value = object.value(key);
    if (!value.isArray()) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed,
            QStringLiteral("%1 response omitted the required %2 array").arg(source, key),
            {}, false});
    }
    return value.toArray();
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

struct NanoValue {
    BigInt nanos{0};
    QString currency;
};

std::optional<NanoValue> nanoValue(const QJsonObject &object, const bool hasCurrency)
{
    if (object.isEmpty()) {
        return std::nullopt;
    }
    const auto units = object.contains(QStringLiteral("units"))
        ? jsonInteger(object.value(QStringLiteral("units")))
        : std::optional<std::int64_t>(0);
    const auto nano = object.contains(QStringLiteral("nano"))
        ? jsonInteger(object.value(QStringLiteral("nano")))
        : std::optional<std::int64_t>(0);
    if (!units || !nano || *nano <= -1'000'000'000 || *nano >= 1'000'000'000) {
        return std::nullopt;
    }
    NanoValue result;
    result.nanos = BigInt(*units) * 1'000'000'000 + *nano;
    if (hasCurrency) {
        result.currency = object.value(QStringLiteral("currency")).toString().toUpper();
        if (result.currency.isEmpty()) {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<Money> moneyValue(const QJsonObject &object)
{
    const auto value = nanoValue(object, true);
    if (!value) {
        return std::nullopt;
    }
    const auto rounding = value->nanos >= 0 ? 5'000'000 : -5'000'000;
    const BigInt minor = (value->nanos + rounding) / 10'000'000;
    if (minor < std::numeric_limits<std::int64_t>::min()
        || minor > std::numeric_limits<std::int64_t>::max()) {
        return std::nullopt;
    }
    return Money{minor.convert_to<std::int64_t>(), value->currency};
}

QString bigIntString(const BigInt &value)
{
    return QString::fromStdString(value.convert_to<std::string>());
}

std::optional<Money> positionValue(const NanoValue &quantity, const NanoValue &price)
{
    const BigInt product = quantity.nanos * price.nanos;
    const BigInt denominator = BigInt(10'000'000) * 1'000'000'000;
    BigInt rounding = denominator / 2;
    if (product < 0) {
        rounding = -rounding;
    }
    const BigInt minor = (product + rounding) / denominator;
    if (minor < std::numeric_limits<std::int64_t>::min()
        || minor > std::numeric_limits<std::int64_t>::max()) {
        return std::nullopt;
    }
    return Money{minor.convert_to<std::int64_t>(), price.currency};
}

} // namespace

TInvestClient::TInvestClient(QNetworkAccessManager &network)
    : network_(network), allowlist_({QString::fromLatin1(apiHost)})
{
}

ConnectorManifest TInvestClient::manifest() const
{
    const QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("credentialReference")}},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("credentialReference"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("writeOnly"), true}}}}},
        {QStringLiteral("additionalProperties"), false},
    };
    return {QStringLiteral("t-invest-readonly-v1"),
            QStringLiteral("tbank"),
            QStringLiteral("Т-Инвестиции (только чтение)"),
            QStringLiteral("1.0.0"),
            {Capability::AccountSync, Capability::InvestmentSync},
            schema,
            {QString::fromLatin1(apiHost)}};
}

ConnectorResult<QNetworkReply *> TInvestClient::discoverAccounts(const QByteArray &readOnlyToken)
{
    return post(TInvestReadMethod::Accounts,
                QJsonObject{{QStringLiteral("status"), QStringLiteral("ACCOUNT_STATUS_OPEN")}},
                readOnlyToken);
}

ConnectorResult<QVector<QNetworkReply *>>
TInvestClient::synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyToken)
{
    if (request.accountExternalId.trimmed().isEmpty() || request.pageSize < 1
        || request.pageSize > 1000) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("T-Invest synchronization needs an account and page size from 1 to 1000"),
            {}, false});
    }
    QJsonObject operations{
        {QStringLiteral("accountId"), request.accountExternalId},
        {QStringLiteral("limit"), request.pageSize},
        {QStringLiteral("state"), QStringLiteral("OPERATION_STATE_EXECUTED")},
    };
    if (!request.cursor.isEmpty()) {
        operations.insert(QStringLiteral("cursor"), QString::fromUtf8(request.cursor));
    }
    if (request.from && request.from->isValid()) {
        operations.insert(QStringLiteral("from"), request.from->toUTC().toString(Qt::ISODateWithMs));
    }
    const QJsonObject account{{QStringLiteral("accountId"), request.accountExternalId}};

    auto operationReply = post(TInvestReadMethod::OperationsByCursor, operations, readOnlyToken);
    if (!operationReply) {
        return std::unexpected(operationReply.error());
    }
    auto portfolioReply = post(TInvestReadMethod::Portfolio, account, readOnlyToken);
    if (!portfolioReply) {
        (*operationReply)->abort();
        (*operationReply)->deleteLater();
        return std::unexpected(portfolioReply.error());
    }
    auto positionReply = post(TInvestReadMethod::Positions, account, readOnlyToken);
    if (!positionReply) {
        (*operationReply)->abort();
        (*portfolioReply)->abort();
        (*operationReply)->deleteLater();
        (*portfolioReply)->deleteLater();
        return std::unexpected(positionReply.error());
    }
    return QVector<QNetworkReply *>{*operationReply, *portfolioReply, *positionReply};
}

QString TInvestClient::restPath(const TInvestReadMethod method)
{
    switch (method) {
    case TInvestReadMethod::Accounts:
        return QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.UsersService/GetAccounts");
    case TInvestReadMethod::OperationsByCursor:
        return QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OperationsService/"
            "GetOperationsByCursor");
    case TInvestReadMethod::Portfolio:
        return QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OperationsService/GetPortfolio");
    case TInvestReadMethod::Positions:
        return QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OperationsService/GetPositions");
    case TInvestReadMethod::InstrumentBy:
        return QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.InstrumentsService/GetInstrumentBy");
    }
    return {};
}

bool TInvestClient::isReadOnlyPath(const QStringView path)
{
    return path == restPath(TInvestReadMethod::Accounts)
        || path == restPath(TInvestReadMethod::OperationsByCursor)
        || path == restPath(TInvestReadMethod::Portfolio)
        || path == restPath(TInvestReadMethod::Positions)
        || path == restPath(TInvestReadMethod::InstrumentBy);
}

ConnectorResult<NormalizedBatch> TInvestClient::parseAccountsResponse(const QByteArray &json)
{
    const auto root = parseObject(json);
    if (!root) {
        return std::unexpected(root.error());
    }
    NormalizedBatch batch;
    const auto accounts = requiredArray(
        *root, QStringLiteral("accounts"), QStringLiteral("T-Invest accounts"));
    if (!accounts) {
        return std::unexpected(accounts.error());
    }
    for (qsizetype index = 0; index < accounts->size(); ++index) {
        const auto item = accounts->at(index).toObject();
        const auto id = item.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("missing-account-id"),
                 QStringLiteral("T-Invest returned an account without an ID"), index});
            continue;
        }
        const auto status = item.value(QStringLiteral("status")).toString();
        batch.accounts.push_back(
            {id,
             item.value(QStringLiteral("name")).toString(),
             item.value(QStringLiteral("type")).toString(),
             QStringLiteral("RUB"),
             {},
             status == QStringLiteral("ACCOUNT_STATUS_CLOSED"),
             {QStringLiteral("t-invest-readonly-v1"), {}, id, index}});
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
TInvestClient::parseOperationsResponse(const QByteArray &json, const QString &accountExternalId)
{
    if (accountExternalId.trimmed().isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("An account ID is required"), {}, false});
    }
    const auto root = parseObject(json);
    if (!root) {
        return std::unexpected(root.error());
    }
    NormalizedBatch batch;
    const auto operations = requiredArray(
        *root, QStringLiteral("items"), QStringLiteral("T-Invest operations"));
    if (!operations) {
        return std::unexpected(operations.error());
    }
    for (qsizetype index = 0; index < operations->size(); ++index) {
        const auto item = operations->at(index).toObject();
        const auto id = item.value(QStringLiteral("id")).toString().trimmed();
        const auto occurredAt =
            QDateTime::fromString(item.value(QStringLiteral("date")).toString(), Qt::ISODate);
        const auto payment = moneyValue(item.value(QStringLiteral("payment")).toObject());
        if (id.isEmpty() || !occurredAt.isValid() || !payment) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-investment-operation"),
                 QStringLiteral("T-Invest returned an operation with invalid required fields"),
                 index});
            continue;
        }
        NanoValue quantity;
        const auto quantityValue = item.value(QStringLiteral("quantity"));
        if (quantityValue.isObject()) {
            quantity = nanoValue(quantityValue.toObject(), false).value_or(NanoValue{});
        } else if (const auto units = jsonInteger(quantityValue); units) {
            quantity.nanos = BigInt(*units) * 1'000'000'000;
        }
        batch.investmentEvents.push_back(
            {id,
             accountExternalId,
             occurredAt,
             item.value(QStringLiteral("type")).toString(),
             item.value(QStringLiteral("instrumentUid")).toString(),
             *payment,
             bigIntString(quantity.nanos),
             9,
             {QStringLiteral("t-invest-readonly-v1"), {}, id, index}});
    }
    if (root->value(QStringLiteral("hasNext")).toBool()) {
        batch.nextCursor = root->value(QStringLiteral("nextCursor")).toString().toUtf8();
        if (batch.nextCursor.isEmpty()) {
            batch.warnings.push_back(
                {WarningSeverity::ReviewRequired, QStringLiteral("missing-next-cursor"),
                 QStringLiteral("T-Invest signaled another page but omitted the cursor"), -1});
        }
    }
    return batch;
}

ConnectorResult<NormalizedBatch>
TInvestClient::parsePortfolioResponse(const QByteArray &json, const QString &accountExternalId,
                                      const QDateTime &observedAt)
{
    if (accountExternalId.trimmed().isEmpty() || !observedAt.isValid()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("An account ID and observation time are required"), {}, false});
    }
    const auto root = parseObject(json);
    if (!root) {
        return std::unexpected(root.error());
    }
    NormalizedBatch batch;
    const auto positions = requiredArray(
        *root, QStringLiteral("positions"), QStringLiteral("T-Invest portfolio"));
    if (!positions) {
        return std::unexpected(positions.error());
    }
    for (qsizetype index = 0; index < positions->size(); ++index) {
        const auto item = positions->at(index).toObject();
        auto instrumentId = item.value(QStringLiteral("instrumentUid")).toString();
        if (instrumentId.isEmpty()) {
            instrumentId = item.value(QStringLiteral("figi")).toString();
        }
        const auto quantity = nanoValue(item.value(QStringLiteral("quantity")).toObject(), false);
        const auto price = nanoValue(item.value(QStringLiteral("currentPrice")).toObject(), true);
        if (instrumentId.isEmpty() || !quantity || !price) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-portfolio-position"),
                 QStringLiteral("T-Invest returned a position with invalid required fields"),
                 index});
            continue;
        }
        const auto currentValue = positionValue(*quantity, *price);
        if (!currentValue) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("position-value-overflow"),
                 QStringLiteral("A T-Invest position is too large to represent safely"), index});
            continue;
        }
        batch.positions.push_back(
            {accountExternalId,
             instrumentId,
             bigIntString(quantity->nanos),
             9,
             *currentValue,
             observedAt,
             {QStringLiteral("t-invest-readonly-v1"), {}, instrumentId, index}});
    }
    if (const auto total =
            moneyValue(root->value(QStringLiteral("totalAmountPortfolio")).toObject());
        total) {
        batch.balances.push_back(
            {accountExternalId, observedAt, *total,
             {QStringLiteral("t-invest-readonly-v1"), {}, QStringLiteral("portfolio-total"), -1}});
    }
    return batch;
}

ConnectorResult<QNetworkReply *> TInvestClient::post(const TInvestReadMethod method,
                                                     const QJsonObject &body,
                                                     const QByteArray &readOnlyToken)
{
    if (!validToken(readOnlyToken)) {
        return std::unexpected(ConnectorError{
            ErrorCode::AuthenticationFailed,
            QStringLiteral("A non-empty T-Invest read-only token is required"), {}, false});
    }
    const auto path = restPath(method);
    if (!isReadOnlyPath(path)) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The requested T-Invest method is not in the read-only allowlist"), {},
            false});
    }
    const QUrl url(QStringLiteral("https://%1%2").arg(QString::fromLatin1(apiHost), path));
    auto request = allowlist_.makeRequest(url);
    if (!request) {
        return std::unexpected(request.error());
    }
    request->setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request->setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + readOnlyToken);
    request->setRawHeader("Accept", "application/json");
    return network_.post(*request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

} // namespace wealthtracker::connectors
