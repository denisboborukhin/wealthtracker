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
        TInvestClient::restPath(TInvestReadMethod::Portfolio)));
    CHECK(TInvestClient::isReadOnlyPath(
        TInvestClient::restPath(TInvestReadMethod::Instrument)));
    for (const auto& path : {
             QStringLiteral("/rest/tinkoff.public.invest.api.contract.v1.OperationsService/GetOperationsByCursor"),
             QStringLiteral("/rest/tinkoff.public.invest.api.contract.v1.OperationsService/GetPositions")}) {
        CHECK_FALSE(TInvestClient::isReadOnlyPath(path));
    }
    CHECK_FALSE(TInvestClient::isReadOnlyPath(
        QStringLiteral(
            "/rest/tinkoff.public.invest.api.contract.v1.OrdersService/PostOrder")));
}

TEST_CASE("T-Invest response parsers preserve accounts and exact portfolio values")
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
    const auto portfolio = TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"totalAmountPortfolio":{"currency":"rub"}})"),
        QStringLiteral("account"), QDateTime::currentDateTimeUtc());
    REQUIRE(portfolio);
    CHECK(portfolio->positions.isEmpty());
    REQUIRE(portfolio->balances.size() == 1);
    CHECK(portfolio->balances.front().balance.minorUnits == 0);
    CHECK_FALSE(TInvestClient::parseAccountsResponse(
        QByteArrayLiteral(R"({"code":16,"message":"Unauthenticated"})")));
}

TEST_CASE("T-Invest rejects missing portfolio totals")
{
    CHECK_FALSE(TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"positions":[]})"), QStringLiteral("account"),
        QDateTime::currentDateTimeUtc()));
}

TEST_CASE("T-Invest rejects overflowing portfolio totals")
{
    const auto overflow = TInvestClient::parsePortfolioResponse(
        QByteArrayLiteral(R"({"totalAmountPortfolio":{"currency":"rub","units":"9223372036854775807"},"positions":[]})"),
        QStringLiteral("account"), QDateTime::currentDateTimeUtc());
    CHECK_FALSE(overflow);
}

TEST_CASE("T-Invest bounds credentials before issuing requests")
{
    CapturingNetworkAccessManager network;
    TInvestClient client(network);
    CHECK_FALSE(client.post(TInvestReadMethod::Accounts, {}, QByteArray(16 * 1024 + 1, 'a')));
    CHECK_FALSE(client.post(TInvestReadMethod::Accounts, {}, QByteArrayLiteral("readonly-token with-space")));
    CHECK_FALSE(client.post(TInvestReadMethod::Accounts, {}, QByteArrayLiteral("readonly-token\twith-tab")));
    CHECK(network.bodies.isEmpty());
}

TEST_CASE("T-Invest instrument metadata validates identity and bounded display fields")
{
    const auto parsed = TInvestClient::parseInstrumentResponse(QByteArrayLiteral(
        R"({"instrument":{"uid":"uid-1","figi":"BBG000B9XRY4","ticker":"SBER","name":"Сбер Банк"}})"),
        QStringLiteral("uid-1"));
    REQUIRE(parsed);
    CHECK(parsed->instrumentId == QStringLiteral("uid-1"));
    CHECK(parsed->ticker == QStringLiteral("SBER"));
    CHECK(parsed->name == QStringLiteral("Сбер Банк"));
    CHECK(TInvestClient::parseInstrumentResponse(QByteArrayLiteral(
        R"({"instrument":{"uid":"uid-1","figi":"BBG000B9XRY4","ticker":"SBER","name":"Сбер Банк"}})"),
        QStringLiteral("BBG000B9XRY4")));
    CHECK(TInvestClient::parseInstrumentResponse(QByteArrayLiteral(
        R"({"instrument":{"uid":"uid-1","figi":"","name":"Инструмент без тикера"}})"),
        QStringLiteral("uid-1")));
    for (const auto& body : {
            QByteArrayLiteral(R"({"instrument":{"uid":"other","ticker":"SBER","name":"Other"}})"),
            QByteArrayLiteral(R"({"instrument":{"uid":"uid-1","ticker":123,"name":"Other"}})"),
            QByteArrayLiteral(R"({"instrument":{"uid":"uid-1","name":"bad\nname"}})"),
            QByteArrayLiteral(R"({"instrument":{"uid":"uid-1"}})"),
            QByteArrayLiteral(R"({"code":5,"message":"Not found"})"),
            QByteArrayLiteral(R"({"instrument":[]})"),
            QByteArray(64 * 1024 + 1, ' ')}) {
        CHECK_FALSE(TInvestClient::parseInstrumentResponse(body, QStringLiteral("uid-1")));
    }
    QJsonObject instrument{{QStringLiteral("uid"), QStringLiteral("uid-1")},
        {QStringLiteral("ticker"), QString(65, QLatin1Char('A'))},
        {QStringLiteral("name"), QStringLiteral("Name")}};
    CHECK_FALSE(TInvestClient::parseInstrumentResponse(QJsonDocument(
        QJsonObject{{QStringLiteral("instrument"), instrument}}).toJson(), QStringLiteral("uid-1")));
    instrument[QStringLiteral("ticker")] = QStringLiteral("TICKER");
    instrument[QStringLiteral("name")] = QString(257, QLatin1Char('A'));
    CHECK_FALSE(TInvestClient::parseInstrumentResponse(QJsonDocument(
        QJsonObject{{QStringLiteral("instrument"), instrument}}).toJson(), QStringLiteral("uid-1")));
}

TEST_CASE("T-Invest reads optional portfolio tickers without coupling balances to metadata")
{
    QJsonObject position{{QStringLiteral("instrumentUid"), QStringLiteral("fund-fixture")},
        {QStringLiteral("quantity"), QJsonObject{{QStringLiteral("units"), "2"}}},
        {QStringLiteral("currentPrice"), QJsonObject{{QStringLiteral("currency"), "rub"},
                                                  {QStringLiteral("units"), "60"}}}};
    QJsonValue ticker;
    SECTION("TMOS ticker") { ticker = QStringLiteral("TMOS"); }
    SECTION("Exchange suffix") { ticker = QStringLiteral("TMOS@"); }
    SECTION("No ticker") {}
    SECTION("Wrong type") { ticker = 42; }
    SECTION("Control character") { ticker = QStringLiteral("TMOS\n"); }
    SECTION("Oversized ticker") { ticker = QString(65, QLatin1Char('X')); }
    position[QStringLiteral("ticker")] = ticker;
    const auto portfolio = TInvestClient::parsePortfolioResponse(QJsonDocument(QJsonObject{
        {QStringLiteral("totalAmountPortfolio"), QJsonObject{{QStringLiteral("currency"), "rub"},
                                                           {QStringLiteral("units"), "120"}}},
        {QStringLiteral("positions"), QJsonArray{position}}}).toJson(),
        QStringLiteral("account"), QDateTime::currentDateTimeUtc());
    REQUIRE(portfolio);
    REQUIRE(portfolio->positions.size() == 1);
    CHECK(portfolio->positions.front().currentValue.minorUnits == 12000);
    CHECK(portfolio->balances.front().balance.minorUnits == 12000);
    const bool validTicker = ticker == QJsonValue(QStringLiteral("TMOS"))
        || ticker == QJsonValue(QStringLiteral("TMOS@"));
    CHECK(portfolio->positions.front().ticker == (validTicker ? ticker.toString() : QString{}));
}

TEST_CASE("T-Invest metadata requests reject invalid identifiers before networking")
{
    CapturingNetworkAccessManager network;
    TInvestClient client(network);
    const auto token = QByteArrayLiteral("read-only-token-1234567890");
    for (const auto& id : {QString{}, QStringLiteral("bad identifier"),
            QStringLiteral("../bad/id"), QString(129, QLatin1Char('a'))}) {
        CHECK_FALSE(client.post(TInvestReadMethod::Instrument,
            {{QStringLiteral("idType"), QStringLiteral("INSTRUMENT_ID_TYPE_ID")},
             {QStringLiteral("id"), id}}, token));
    }
    CHECK(network.bodies.isEmpty());
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

    CHECK_FALSE(client.discoverAccounts(QByteArrayLiteral("unsafe;session-value")));
    CHECK_FALSE(TBankRetailClient::isReadOnlyPath(
        QStringLiteral("/api/common/v1/operations")));
    CHECK_FALSE(TBankRetailClient::isReadOnlyPath(
        QStringLiteral("/api/common/v1/payments")));
}

TEST_CASE("T-Bank retail responses normalize balances")
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

    const auto rejected = TBankRetailClient::parseAccountsResponse(
        QByteArrayLiteral(
            R"({"resultCode":"OPERATION_REJECTED","plainMessage":"secret account detail"})"),
        observedAt);
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
