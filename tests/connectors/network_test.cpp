#include <catch2/catch_test_macros.hpp>
#include <QDate>
#include <QDateTime>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

import wt.connectors;

using namespace wealthtracker::connectors;

namespace {

class CapturedReply final : public QNetworkReply {
public:
    CapturedReply(const QNetworkAccessManager::Operation operation,
                  const QNetworkRequest& request, QObject* parent)
        : QNetworkReply(parent) {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        setFinished(true);
    }

    void abort() override {}

protected:
    qint64 readData(char*, qint64) override { return -1; }
};

class CapturingNetworkAccessManager final : public QNetworkAccessManager {
public:
    Operation operation = UnknownOperation;
    QNetworkRequest request;
    QByteArray body;
    QVector<QByteArray> bodies;

protected:
    QNetworkReply* createRequest(
        const Operation requestedOperation,
        const QNetworkRequest& requested,
        QIODevice* outgoingData = nullptr) override {
        operation = requestedOperation;
        request = requested;
        body = outgoingData ? outgoingData->readAll() : QByteArray{};
        bodies.push_back(body);
        return new CapturedReply(requestedOperation, requested, this);
    }
};

}  // namespace

TEST_CASE("host allowlist requires exact HTTPS origins")
{
    const HostAllowlist policy({QStringLiteral("api.example.test")});

    CHECK(policy.permits(QUrl(QStringLiteral("https://api.example.test/v1/data"))));
    CHECK(policy.permits(QUrl(QStringLiteral("https://api.example.test:443/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("http://api.example.test/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("https://evil.api.example.test/v1/data"))));
    CHECK_FALSE(policy.permits(QUrl(QStringLiteral("https://api.example.test:8443/v1/data"))));
    CHECK_FALSE(policy.permits(
        QUrl(QStringLiteral("https://user:password@api.example.test/v1/data"))));

    const auto request = policy.makeRequest(
        QUrl(QStringLiteral("https://api.example.test/v1/data")));
    REQUIRE(request);
    CHECK(request->attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
          == QNetworkRequest::ManualRedirectPolicy);
}

TEST_CASE("T-Invest exposes only explicitly read-only REST methods")
{
    CHECK(TInvestClient::isReadOnlyPath(TInvestClient::restPath(TInvestReadMethod::Accounts)));
    CHECK(TInvestClient::isReadOnlyPath(
        TInvestClient::restPath(TInvestReadMethod::OperationsByCursor)));
    CHECK_FALSE(TInvestClient::isReadOnlyPath(
        QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OrdersService/PostOrder")));
}

TEST_CASE("T-Invest response parsers preserve cursor and exact portfolio values")
{
    SECTION("accounts")
    {
        const auto batch = TInvestClient::parseAccountsResponse(QByteArrayLiteral(
            R"({"accounts":[{"id":"broker-1","name":"ИИС","type":"ACCOUNT_TYPE_TINKOFF_IIS","status":"ACCOUNT_STATUS_OPEN"}]})"));
        REQUIRE(batch);
        REQUIRE(batch->accounts.size() == 1);
        CHECK(batch->accounts.front().externalId == QStringLiteral("broker-1"));
        CHECK_FALSE(batch->accounts.front().closed);
    }

    SECTION("cursor operations")
    {
        const auto batch = TInvestClient::parseOperationsResponse(
            QByteArrayLiteral(
                R"({"items":[{"id":"operation-1","date":"2026-07-30T09:15:00Z","type":"OPERATION_TYPE_DIVIDEND","instrumentUid":"uid-1","payment":{"currency":"rub","units":"12","nano":340000000},"quantity":"2"}],"hasNext":true,"nextCursor":"cursor-2"})"),
            QStringLiteral("broker-1"));
        REQUIRE(batch);
        REQUIRE(batch->investmentEvents.size() == 1);
        CHECK(batch->investmentEvents.front().payment
              == Money{1234, QStringLiteral("RUB")});
        CHECK(batch->investmentEvents.front().quantityMantissa
              == QStringLiteral("2000000000"));
        CHECK(batch->nextCursor == QByteArrayLiteral("cursor-2"));
    }

    SECTION("portfolio")
    {
        const auto observedAt = QDateTime::fromString(
            QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);
        const auto batch = TInvestClient::parsePortfolioResponse(
            QByteArrayLiteral(
                R"({"totalAmountPortfolio":{"currency":"rub","units":"1000","nano":0},"positions":[{"instrumentUid":"uid-1","quantity":{"units":"2","nano":500000000},"currentPrice":{"currency":"rub","units":"12","nano":340000000}}]})"),
            QStringLiteral("broker-1"), observedAt);
        REQUIRE(batch);
        REQUIRE(batch->positions.size() == 1);
        CHECK(batch->positions.front().currentValue
              == Money{3085, QStringLiteral("RUB")});
        REQUIRE(batch->balances.size() == 1);
        CHECK(batch->balances.front().balance.minorUnits == 100000);
    }
}

TEST_CASE("T-Invest accepts omitted protobuf collections and rejects error envelopes")
{
    const auto accounts = TInvestClient::parseAccountsResponse(QByteArrayLiteral("{}"));
    REQUIRE(accounts);
    CHECK(accounts->accounts.isEmpty());
    const auto operations = TInvestClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"hasNext":false})"), QStringLiteral("account"));
    REQUIRE(operations);
    CHECK(operations->investmentEvents.isEmpty());
    const auto portfolio = TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"totalAmountPortfolio":{"currency":"rub"}})"),
        QStringLiteral("account"), QDateTime::currentDateTimeUtc());
    REQUIRE(portfolio);
    CHECK(portfolio->positions.isEmpty());
    REQUIRE(portfolio->balances.size() == 1);
    CHECK(portfolio->balances.front().balance.minorUnits == 0);
    CHECK_FALSE(TInvestClient::parseAccountsResponse(
        QByteArrayLiteral(R"({"code":16,"message":"Unauthenticated"})")));
    CHECK_FALSE(TInvestClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"items":{}})"), QStringLiteral("account")));
}

TEST_CASE("T-Invest rejects ambiguous pagination and invalid portfolio totals")
{
    for (const auto& json : {QByteArrayLiteral(R"({"items":[],"hasNext":true})"),
                             QByteArrayLiteral(R"({"items":[],"hasNext":"true","nextCursor":"next"})"),
                             QByteArrayLiteral(R"({"items":[],"hasNext":true,"nextCursor":12})")}) {
        const auto result = TInvestClient::parseOperationsResponse(json, QStringLiteral("account"));
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::ParseFailed);
    }
    CHECK_FALSE(TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"positions":[]})"), QStringLiteral("account"),
        QDateTime::currentDateTimeUtc()));
}

TEST_CASE("T-Invest rejects malformed quantities without inventing zero positions")
{
    const auto invalid = TInvestClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"items":[{"id":"op","date":"2026-07-30T09:15:00Z","payment":{"currency":"rub","units":"1"},"quantity":"broken"}]})"),
        QStringLiteral("account"));
    REQUIRE(invalid);
    CHECK(invalid->investmentEvents.isEmpty());
    REQUIRE(invalid->warnings.size() == 1);
    CHECK(invalid->warnings.front().severity == WarningSeverity::RejectedRow);

    const auto overflow = TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"totalAmountPortfolio":{"currency":"rub","units":"9223372036854775807"},"positions":[]})"),
        QStringLiteral("account"), QDateTime::currentDateTimeUtc());
    CHECK_FALSE(overflow);
}

TEST_CASE("T-Invest synchronization preserves the requested date window")
{
    CapturingNetworkAccessManager network;
    TInvestClient client(network);
    LiveSyncRequest request;
    request.accountExternalId = QStringLiteral("account");
    request.from = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    request.to = QDateTime::fromString(QStringLiteral("2026-02-01T00:00:00Z"), Qt::ISODate);
    const auto replies = client.synchronize(request, QByteArrayLiteral("readonly-token-with-safe-length"));
    REQUIRE(replies);
    REQUIRE(network.bodies.size() == 3);
    const auto body = QJsonDocument::fromJson(network.bodies.front()).object();
    CHECK(body.value(QStringLiteral("from")).toString() == QStringLiteral("2026-01-01T00:00:00.000Z"));
    CHECK(body.value(QStringLiteral("to")).toString() == QStringLiteral("2026-02-01T00:00:00.000Z"));
    request.to = request.from->addDays(-1);
    CHECK_FALSE(client.synchronize(request, QByteArrayLiteral("readonly-token-with-safe-length")));
    request.to = QDateTime{};
    CHECK_FALSE(client.synchronize(request, QByteArrayLiteral("readonly-token-with-safe-length")));
    CHECK(network.bodies.size() == 3);
}

TEST_CASE("T-Invest bounds credentials and cursors before issuing requests")
{
    CapturingNetworkAccessManager network;
    TInvestClient client(network);
    CHECK_FALSE(client.discoverAccounts(QByteArray(16 * 1024 + 1, 'a')));
    CHECK_FALSE(client.discoverAccounts(QByteArrayLiteral("readonly-token with-space")));
    CHECK_FALSE(client.discoverAccounts(QByteArrayLiteral("readonly-token\twith-tab")));
    LiveSyncRequest request;
    request.accountExternalId = QStringLiteral("account");
    request.cursor = QByteArray(4097, 'a');
    CHECK_FALSE(client.synchronize(request, QByteArrayLiteral("readonly-token-with-safe-length")));
    CHECK(network.bodies.isEmpty());

    const QJsonObject response{
        {QStringLiteral("hasNext"), true},
        {QStringLiteral("nextCursor"), QString(4097, QLatin1Char('a'))}};
    CHECK_FALSE(TInvestClient::parseOperationsResponse(
        QJsonDocument(response).toJson(), QStringLiteral("account")));
}

TEST_CASE("official bank clients expose only documented read-only methods")
{
    CHECK(AlfaBankClient::isReadOnlyPath(
        AlfaBankClient::restPath(AlfaBankReadMethod::Accounts)));
    CHECK(AlfaBankClient::isReadOnlyPath(
        AlfaBankClient::restPath(AlfaBankReadMethod::Operations)));
    CHECK_FALSE(
        AlfaBankClient::isReadOnlyPath(QStringLiteral("/api/pp/v1/payments")));

    CHECK(TBankBusinessClient::isReadOnlyPath(
        TBankBusinessClient::restPath(TBankBusinessReadMethod::Accounts)));
    CHECK(TBankBusinessClient::isReadOnlyPath(
        TBankBusinessClient::restPath(TBankBusinessReadMethod::Statement)));
    CHECK_FALSE(TBankBusinessClient::isReadOnlyPath(
        QStringLiteral("/openapi/api/v1/payment")));

    const auto modulbankAccount = QStringLiteral(
        "d0b9f257-6f5b-4f0f-a45f-6ef09c2d6dd7");
    CHECK(ModulbankClient::isReadOnlyPath(
        ModulbankClient::restPath(ModulbankReadMethod::Accounts)));
    CHECK(ModulbankClient::isReadOnlyPath(ModulbankClient::restPath(
        ModulbankReadMethod::OperationHistory, modulbankAccount)));
    CHECK_FALSE(
        ModulbankClient::isReadOnlyPath(QStringLiteral("/v1/operation-upload")));
}

TEST_CASE("official bank requests preserve read-only origins and sandbox mode")
{
    CapturingNetworkAccessManager network;

    AlfaBankClient alfa(network);
    const auto alfaReply =
        alfa.discoverAccounts(QByteArrayLiteral("alfa-token"));
    REQUIRE(alfaReply);
    CHECK(network.operation == QNetworkAccessManager::GetOperation);
    CHECK(network.request.url() ==
          QUrl(QStringLiteral("https://baas.alfabank.ru/api/pp/v1/accounts")));
    CHECK(network.request.rawHeader("Authorization") ==
          QByteArrayLiteral("Bearer alfa-token"));

    TBankBusinessClient tbank(network);
    const auto tbankReply =
        tbank.discoverAccounts(QByteArrayLiteral("tbank-token"));
    REQUIRE(tbankReply);
    CHECK(network.operation == QNetworkAccessManager::GetOperation);
    CHECK(network.request.url().host() ==
          QStringLiteral("business.tbank.ru"));
    CHECK(network.request.url().path() ==
          QStringLiteral("/openapi/api/v4/bank-accounts"));
    CHECK_FALSE(network.request.rawHeader("X-Request-Id").isEmpty());

    ModulbankClient modulbank(network);
    const auto sandboxReply =
        modulbank.discoverAccounts(QByteArrayLiteral("sandboxtoken"));
    REQUIRE(sandboxReply);
    CHECK(network.operation == QNetworkAccessManager::PostOperation);
    CHECK(network.request.url() ==
          QUrl(QStringLiteral("https://api.modulbank.ru/v1/account-info")));
    CHECK(network.request.rawHeader("sandbox") == QByteArrayLiteral("on"));
    CHECK(network.body == QByteArrayLiteral("{}"));

    const auto liveReply =
        modulbank.discoverAccounts(QByteArrayLiteral("production-token"));
    REQUIRE(liveReply);
    CHECK(network.request.rawHeader("sandbox").isEmpty());
}

TEST_CASE("Alfa B2C responses normalize accounts and paginated operations")
{
    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);

    const auto accounts = AlfaBankClient::parseAccountsResponse(
        QByteArrayLiteral(
            R"({"accounts":[{"mnemonic":"Основной","number":"40817810000000000001","type":"EE","typeDescription":"Текущий счёт","status":"ACTIVE","balance":{"currency":"RUR","minorUnits":100,"amount":2400050}}]})"),
        observedAt);
    REQUIRE(accounts);
    REQUIRE(accounts->accounts.size() == 1);
    REQUIRE(accounts->balances.size() == 1);
    CHECK(accounts->accounts.front().currency == QStringLiteral("RUB"));
    CHECK(accounts->accounts.front().maskedNumber == QStringLiteral("•••• 0001"));
    CHECK(accounts->balances.front().balance
          == Money{2400050, QStringLiteral("RUB")});

    const auto operations = AlfaBankClient::parseOperationsResponse(
        QByteArrayLiteral(
            R"({"operations":[{"id":"alfa-operation-1","dateTime":"2026-07-29T12:30:00Z","title":"МТС","amount":{"value":10000,"currency":"RUR","minorUnits":100},"direction":"EXPENSE","category":{"name":"Связь"},"mcc":"4814","status":"SUCCESS"}]})"),
        QStringLiteral("40817810000000000001"), 100, 1);
    REQUIRE(operations);
    REQUIRE(operations->transactions.size() == 1);
    CHECK(operations->transactions.front().amount
          == Money{-10000, QStringLiteral("RUB")});
    CHECK(operations->transactions.front().bankCategory == QStringLiteral("Связь"));
    CHECK(operations->nextCursor == QByteArrayLiteral("101"));
}

TEST_CASE("T-Bank Business responses normalize statement signs and cursor")
{
    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);
    const auto accounts = TBankBusinessClient::parseAccountsResponse(
        QByteArrayLiteral(
            R"([{"accountNumber":"40702810000000000001","name":"Расчётный","accountType":"Current","currency":"643","balance":{"balance":1200.50,"realOtb":1100.25,"otb":1300},"status":"NORM"}])"),
        observedAt);
    REQUIRE(accounts);
    REQUIRE(accounts->balances.size() == 1);
    CHECK(accounts->balances.front().balance
          == Money{120050, QStringLiteral("RUB")});

    const auto statement = TBankBusinessClient::parseStatementResponse(
        QByteArrayLiteral(
            R"({"balances":{"balanceBegin":1000,"balanceEnd":1125.15},"operations":[{"operationId":"tbank-operation-1","operationDate":"2026-07-29T08:15:00Z","typeOfOperation":"Credit","accountAmount":150.25,"accountCurrencyDigitalCode":"643","description":"Оплата от клиента"},{"operationId":"tbank-operation-2","operationDate":"2026-07-29T09:15:00Z","typeOfOperation":"Debit","accountAmount":25.10,"accountCurrencyDigitalCode":"643","description":"Комиссия"}],"nextCursor":"cursor-2"})"),
        QStringLiteral("40702810000000000001"));
    REQUIRE(statement);
    REQUIRE(statement->transactions.size() == 2);
    CHECK(statement->transactions.at(0).amount.minorUnits == 15025);
    CHECK(statement->transactions.at(1).amount.minorUnits == -2510);
    CHECK(statement->nextCursor == QByteArrayLiteral("cursor-2"));
}

TEST_CASE("T-Bank retail connector permits only exact read requests")
{
    CapturingNetworkAccessManager network;
    TBankRetailClient client(network);
    const QByteArray session{"verified-session-123456"};

    const auto accounts = client.discoverAccounts(session);
    REQUIRE(accounts);
    CHECK(network.operation == QNetworkAccessManager::GetOperation);
    CHECK(network.request.url().scheme() == QStringLiteral("https"));
    CHECK(network.request.url().host() == QStringLiteral("www.tbank.ru"));
    CHECK(network.request.url().path()
          == QStringLiteral("/api/common/v1/accounts_light_ib"));
    CHECK(QUrlQuery(network.request.url()).queryItemValue(
              QStringLiteral("sessionid")) == QString::fromLatin1(session));
    CHECK(network.request.attribute(
              QNetworkRequest::RedirectPolicyAttribute).toInt()
          == QNetworkRequest::ManualRedirectPolicy);
    CHECK(network.request.attribute(
              QNetworkRequest::CookieLoadControlAttribute).toInt()
          == QNetworkRequest::Manual);
    CHECK(network.request.attribute(
              QNetworkRequest::CookieSaveControlAttribute).toInt()
          == QNetworkRequest::Manual);
    CHECK_FALSE(network.request.attribute(QNetworkRequest::Http2AllowedAttribute).toBool());
    CHECK(network.request.transferTimeout() == 45'000);

    const auto requests = client.synchronize(
        LiveSyncRequest{
            .accountExternalId = QStringLiteral("account-1"),
            .from = QDateTime::fromString(
                QStringLiteral("2026-07-01T00:00:00Z"), Qt::ISODate),
            .to = QDateTime::fromString(
                QStringLiteral("2026-08-01T12:34:56Z"), Qt::ISODate),
            .pageSize = 1'000,
        },
        session);
    REQUIRE(requests);
    CHECK(network.operation == QNetworkAccessManager::GetOperation);
    CHECK(network.request.url().path()
          == QStringLiteral("/api/common/v1/operations"));
    CHECK(QUrlQuery(network.request.url()).queryItemValue(
              QStringLiteral("account")) == QStringLiteral("account-1"));
    CHECK(QUrlQuery(network.request.url()).queryItemValue(
              QStringLiteral("start")) == QStringLiteral("1782864000000"));
    CHECK(QUrlQuery(network.request.url()).queryItemValue(
              QStringLiteral("end")) == QStringLiteral("1785587696000"));

    CHECK_FALSE(client.synchronize(
        LiveSyncRequest{
            .accountExternalId = QStringLiteral("account-1"),
            .cursor = QByteArrayLiteral("1"),
            .from = QDateTime::fromString(
                QStringLiteral("2024-08-26T00:00:00Z"), Qt::ISODate),
            .to = QDateTime::fromString(
                QStringLiteral("2026-08-26T12:34:56Z"), Qt::ISODate),
            .pageSize = 1'000,
        },
        session));

    CHECK_FALSE(client.discoverAccounts(QByteArrayLiteral("unsafe;session-value")));
    CHECK_FALSE(client.synchronize(
        LiveSyncRequest{
            .accountExternalId = QStringLiteral("account-1"),
            .cursor = QByteArrayLiteral("unexpected-page"),
            .from = QDateTime::fromString(
                QStringLiteral("2024-08-26T00:00:00Z"), Qt::ISODate),
            .to = QDateTime::fromString(
                QStringLiteral("2026-08-26T12:34:56Z"), Qt::ISODate),
        },
        session));
    CHECK_FALSE(TBankRetailClient::isReadOnlyPath(
        QStringLiteral("/api/common/v1/payments")));
}

TEST_CASE("T-Bank retail responses normalize balances and operations")
{
    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);
    const auto accounts = TBankRetailClient::parseAccountsResponse(
        QByteArrayLiteral(R"({"resultCode":"OK","payload":[{"id":"account-1","name":"Black","accountType":"Debit","moneyAmount":{"value":"12500.255","currency":{"name":"RUR"}},"cards":[{"value":"553691******1234"}]},{"id":"credit-1","name":"Platinum","accountType":"Credit","moneyAmount":{"value":55000,"currency":{"name":"RUB"}},"creditLimit":{"value":100000,"currency":{"name":"RUB"}}}]})"),
        observedAt);
    REQUIRE(accounts);
    REQUIRE(accounts->accounts.size() == 2);
    REQUIRE(accounts->balances.size() == 2);
    CHECK(accounts->accounts.front().maskedNumber == QStringLiteral("•••• 1234"));
    CHECK(accounts->balances.front().balance
          == Money{1'250'026, QStringLiteral("RUB")});
    CHECK(accounts->accounts.at(1).kind == QStringLiteral("credit-card"));
    CHECK(accounts->balances.at(1).balance
          == Money{-4'500'000, QStringLiteral("RUB")});

    const auto operations = TBankRetailClient::parseOperationsResponse(
        QByteArrayLiteral(
            R"({"resultCode":"OK","payload":[{"id":"operation-1","status":"OK","type":"Debit","isInner":true,"accountAmount":{"value":-427.5,"currency":{"name":"RUB"}},"operationTime":{"milliseconds":1785489300000},"debitingTime":{"milliseconds":1785489400000},"merchant":{"name":"Кофейня"},"spendingCategory":{"name":"Кафе"},"mcc":5814},{"id":"malformed-operation","status":"OK","type":"Debit","accountAmount":{"value":0,"currency":{"name":"RUB"}}}]})"),
        QStringLiteral("account-1"));
    REQUIRE(operations);
    REQUIRE(operations->transactions.size() == 1);
    REQUIRE(operations->warnings.size() == 2);
    const auto& operation = operations->transactions.front();
    CHECK(operation.externalId == QStringLiteral("operation-1"));
    CHECK(operation.amount == Money{-42'750, QStringLiteral("RUB")});
    CHECK(operation.merchant == QStringLiteral("Кофейня"));
    CHECK(operation.bankCategory == QStringLiteral("Кафе"));
    CHECK(operation.mcc == QStringLiteral("5814"));
    CHECK(operation.kind == NormalizedTransactionKind::InternalTransfer);
    CHECK_FALSE(operation.sourceFields.contains(QStringLiteral("hold")));
    CHECK(operations->nextCursor.isEmpty());
    CHECK(operations->warnings.back().code ==
          QStringLiteral("tbank-history-unverifiable"));

    const auto invalidCursor = TBankRetailClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"resultCode":"NO_DATA_FOUND"})"),
        QStringLiteral("account-1"), QByteArrayLiteral("23"));
    REQUIRE_FALSE(invalidCursor);

    const auto noData = TBankRetailClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"resultCode":"NO_DATA_FOUND","errorMessage":"No operations"})"),
        QStringLiteral("account-1"));
    REQUIRE(noData);
    CHECK(noData->transactions.isEmpty());
    REQUIRE(noData->warnings.size() == 1);
    CHECK(noData->warnings.front().code ==
          QStringLiteral("tbank-history-unverifiable"));

    const auto rejected = TBankRetailClient::parseOperationsResponse(
        QByteArrayLiteral(
            R"({"resultCode":"OPERATION_REJECTED","plainMessage":"secret account detail"})"),
        QStringLiteral("account-1"));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().message.contains(QStringLiteral("TBR-REJECTED")));
    CHECK_FALSE(rejected.error().message.contains(QStringLiteral("secret account detail")));
    CHECK(rejected.error().detail.isEmpty());

    const auto expired = TBankRetailClient::parseAccountsResponse(
        QByteArrayLiteral(
            R"({"resultCode":"INSUFFICIENT_PRIVILEGES","errorMessage":"Authentication required"})"),
        observedAt);
    REQUIRE_FALSE(expired);
    CHECK(expired.error().code == ErrorCode::AuthenticationFailed);

    const auto invalidCreditLimit = TBankRetailClient::parseAccountsResponse(
        QByteArrayLiteral(R"({"resultCode":"OK","payload":[{"id":"credit-1","accountType":"Credit","moneyAmount":{"value":"100.00","currency":{"name":"RUB"}},"creditLimit":{"value":"-500.00","currency":{"name":"RUB"}}}]})"),
        observedAt);
    REQUIRE_FALSE(invalidCreditLimit);
    CHECK(invalidCreditLimit.error().code == ErrorCode::ParseFailed);
}

TEST_CASE("Modulbank responses normalize business accounts and directions")
{
    const auto accountId =
        QStringLiteral("d0b9f257-6f5b-4f0f-a45f-6ef09c2d6dd7");
    const auto observedAt = QDateTime::fromString(
        QStringLiteral("2026-07-30T10:00:00Z"), Qt::ISODate);
    const auto accounts = ModulbankClient::parseAccountsResponse(
        QByteArrayLiteral(
            R"([{"companyName":"ООО Пример","bankAccounts":[{"id":"d0b9f257-6f5b-4f0f-a45f-6ef09c2d6dd7","accountName":"Основной","category":"Checking","currency":"RUR","number":"40702810000000000001","balance":900000,"status":"Open"}]}])"),
        observedAt);
    REQUIRE(accounts);
    REQUIRE(accounts->accounts.size() == 1);
    REQUIRE(accounts->balances.size() == 1);
    CHECK(accounts->accounts.front().name == QStringLiteral("ООО Пример · Основной"));
    CHECK(accounts->balances.front().balance
          == Money{90000000, QStringLiteral("RUB")});

    const auto operations = ModulbankClient::parseOperationsResponse(
        QByteArrayLiteral(
            R"([{"id":"modul-operation-1","executed":"2026-07-29T10:15:00Z","amount":100000,"currency":"RUR","category":"Debet","paymentPurpose":"Оплата по счёту","contragentName":"ООО Клиент","status":"Executed"},{"id":"modul-operation-2","executed":"2026-07-29T11:15:00Z","amount":"500.25","currency":"RUR","category":"Credit","paymentPurpose":"Комиссия","status":"Executed"}])"),
        accountId, 0, 2);
    REQUIRE(operations);
    REQUIRE(operations->transactions.size() == 2);
    CHECK(operations->transactions.at(0).amount.minorUnits == 10000000);
    CHECK(operations->transactions.at(1).amount.minorUnits == -50025);
    CHECK(operations->nextCursor == QByteArrayLiteral("2"));
}

TEST_CASE("official bank parsers reject unsafe or malformed responses")
{
    const auto observedAt = QDateTime::currentDateTimeUtc();
    const auto oversized = AlfaBankClient::parseAccountsResponse(
        QByteArray(20 * 1024 * 1024 + 1, 'x'), observedAt);
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::SecurityViolation);

    const auto malformed = ModulbankClient::parseOperationsResponse(
        QByteArrayLiteral("{not-json}"), QStringLiteral("account"), 0, 50);
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().code == ErrorCode::ParseFailed);

    const auto missingAlfaOperations = AlfaBankClient::parseOperationsResponse(
        QByteArrayLiteral(R"({"status":"ok"})"), QStringLiteral("account"), 0, 100);
    REQUIRE_FALSE(missingAlfaOperations);
    CHECK(missingAlfaOperations.error().code == ErrorCode::ParseFailed);

    const auto missingTBankStatement = TBankBusinessClient::parseStatementResponse(
        QByteArrayLiteral(R"({"balances":{}})"), QStringLiteral("account"));
    REQUIRE_FALSE(missingTBankStatement);
    CHECK(missingTBankStatement.error().code == ErrorCode::ParseFailed);
}
