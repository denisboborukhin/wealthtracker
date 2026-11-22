#include <catch2/catch_test_macros.hpp>

#include "app/AppController.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

import wt.crypto_wallet;

namespace {

using wealthtracker::app::CryptoPortfolio;
using wealthtracker::app::CryptoWalletSync;
using wealthtracker::app::parse_crypto_portfolio;

const QString wallet = QStringLiteral("0x1111111111111111111111111111111111111111");
const QString tokenContract = QStringLiteral("0x2222222222222222222222222222222222222222");
const QString usdtContract = QString::fromLatin1(wealthtracker::app::ethereumUsdtContract);

QJsonArray chains() {
    return {QJsonObject{{"id", "eth"}, {"name", "Ethereum"}, {"native_token_id", "eth"}},
            QJsonObject{{"id", "arb"}, {"name", "Arbitrum"}, {"native_token_id", "arb"}}};
}
QJsonObject quote(const QString& price = "1") {
    return {{"id", usdtContract}, {"chain", "eth"}, {"price", price}};
}
QJsonObject token(const QString& chain = "eth", const QString& id = "eth",
                  const QString& raw = "0xde0b6b3a7640001", const QString& price = "2000") {
    return {{"id", id}, {"chain", chain}, {"decimals", "18"}, {"raw_amount_hex_str", raw},
        {"price", price}, {"symbol", "TEST"}, {"name", "Synthetic token"}};
}

struct Response {
    QByteArray body;
    int status = 200;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int delay = 0;
    QByteArray retryAfter{};
};

Response jsonResponse(const QJsonObject& object) {
    return {QJsonDocument(object).toJson(QJsonDocument::Compact)};
}
Response jsonResponse(const QJsonArray& array) {
    return {QJsonDocument(array).toJson(QJsonDocument::Compact)};
}
QList<Response> portfolioResponses(const QJsonArray& tokens = {token()}) {
    return {jsonResponse(chains()), jsonResponse(QJsonArray{chains()[0]}),
            jsonResponse(quote()), jsonResponse(tokens)};
}

class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), response_(std::move(response)) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
        if (!response_.retryAfter.isEmpty()) setRawHeader("Retry-After", response_.retryAfter);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(response_.delay, this, [this] { finish(); });
    }

    void abort() override {
        setError(OperationCanceledError, QStringLiteral("Cancelled"));
        finish();
    }

    qint64 bytesAvailable() const override {
        return response_.body.size() - offset_ + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* destination, qint64 maximum) override {
        if (offset_ >= response_.body.size()) return -1;
        const auto length = std::min(maximum, response_.body.size() - offset_);
        std::copy_n(response_.body.constData() + offset_, length, destination);
        offset_ += length;
        return length;
    }

private:
    void finish() {
        if (isFinished()) return;
        if (response_.error != NoError) setError(response_.error, QStringLiteral("Synthetic error"));
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    Response response_;
    qint64 offset_ = 0;
};

class Network final : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QNetworkRequest> requests;
    bool onlyGets = true;
    bool unexpectedRequest = false;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override {
        onlyGets &= operation == GetOperation;
        requests.push_back(request);
        if (responses.isEmpty()) {
            unexpectedRequest = true;
            return new Reply(request, {"{}", 500}, this);
        }
        return new Reply(request, responses.takeFirst(), this);
    }
};

bool waitUntil(const std::function<bool()>& predicate, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

struct Outcome {
    int completed = 0;
    int failed = 0;
    QString message;
    std::optional<CryptoPortfolio> portfolio;
};

Outcome run(Network& network) {
    Outcome outcome;
    CryptoWalletSync sync(network,
        [&](CryptoPortfolio value) { ++outcome.completed; outcome.portfolio = std::move(value); },
        [&](QString message) { ++outcome.failed; outcome.message = std::move(message); }, nullptr);
    sync.start(wallet);
    REQUIRE(waitUntil([&] { return outcome.completed + outcome.failed != 0; }));
    CHECK_FALSE(network.unexpectedRequest);
    return outcome;
}

struct SettingsScope {
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QSettings::Format format = QSettings::defaultFormat();
    QTemporaryDir directory;

    SettingsScope() {
        QCoreApplication::setOrganizationName(QStringLiteral("WealthTrackerCryptoTests"));
        QCoreApplication::setApplicationName(QStringLiteral("CryptoWallet"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    }

    ~SettingsScope() {
        QSettings::setDefaultFormat(format);
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setApplicationName(application);
    }
};

using Secrets = QHash<QString, QByteArray>;

class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    explicit Credentials(std::shared_ptr<Secrets> secrets) : secrets_(std::move(secrets)) {}
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString& key, const QByteArray& secret) override {
        secrets_->insert(key, secret);
        return {};
    }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        const auto found = secrets_->constFind(key);
        if (found == secrets_->cend()) return std::optional<QByteArray>{};
        return std::optional<QByteArray>{*found};
    }
    std::expected<void, QString> remove(const QString& key) override {
        secrets_->remove(key);
        return {};
    }

private:
    std::shared_ptr<Secrets> secrets_;
};

} // namespace

TEST_CASE("crypto valuation preserves exact raw balances and numeric price lexemes", "[app][crypto]") {
    const auto parsed = wealthtracker::app::parse_crypto_json(R"({"price":0.1234567890123456789012345678,"small":1e-8,"name":"123 \"test\"","integer":9007199254740993})");
    REQUIRE(parsed);
    CHECK(parsed->object().value("price").toString() == "0.1234567890123456789012345678");
    CHECK(parsed->object().value("integer").toString() == "9007199254740993");
    CHECK_FALSE(wealthtracker::app::parse_crypto_json("{broken}"));
    CHECK_FALSE(wealthtracker::app::parse_crypto_json("{1:2}"));
    CHECK_FALSE(wealthtracker::app::parse_crypto_json("[01]"));
    CHECK_FALSE(wealthtracker::app::parse_crypto_json("[1.2.3]"));
    const auto result = parse_crypto_portfolio(chains(), {token()}, quote("0.8"));
    REQUIRE(result);
    CHECK(result->usdtMinor == 250000);
    CHECK(result->holdings.front().toMap().value("quantity").toString() == "1.000000000000000001");
    const auto longPrice = parse_crypto_portfolio(chains(), {token("eth", "eth", "0xde0b6b3a7640000", "1.067220695822275e-7")}, quote());
    REQUIRE(longPrice);
    CHECK(longPrice->unpricedCount == 0);
    const auto tiny = parse_crypto_portfolio(chains(), {token("eth", "eth", "0x1", "1e18")}, quote());
    REQUIRE(tiny);
    CHECK(tiny->usdtMinor == 100);
}

TEST_CASE("crypto parses recorded Rabby metadata and multichain holdings exactly", "[app][crypto]") {
    QFile file(QStringLiteral(WEALTHTRACKER_SOURCE_DIR "/tests/fixtures/crypto/rabby.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto parsed = wealthtracker::app::parse_crypto_json(file.readAll());
    REQUIRE(parsed);
    const auto fixture = parsed->object();
    const auto result = parse_crypto_portfolio(fixture.value("chains").toArray(),
        fixture.value("tokens").toArray(), fixture.value("quote").toObject());
    REQUIRE(result);
    CHECK(result->supportedChains.size() == 71);
    CHECK(result->usedChains.size() == 2);
    CHECK(result->unpricedCount == 2);
    CHECK(result->usdtMinor == 1390142);
}

TEST_CASE("crypto combines chain qualified native and ERC20 assets without mixing identities", "[app][crypto]") {
    auto eth = token("eth", tokenContract);
    auto arb = token("arb", tokenContract);
    const auto result = parse_crypto_portfolio(chains(), {eth, arb, token("arb", "arb")}, quote());
    REQUIRE(result);
    CHECK(result->holdings.size() == 3);
    CHECK(result->usdtMinor == 600000);
    CHECK(result->supportedChains.size() == 2);
    CHECK(result->usedChains.size() == 2);
    CHECK_FALSE(parse_crypto_portfolio(chains(), {eth, eth}, quote()));
    CHECK_FALSE(parse_crypto_portfolio(chains(), {token("unknown")}, quote()));
    CHECK_FALSE(parse_crypto_portfolio(chains(), {token("arb", "eth")}, quote()));
}

TEST_CASE("crypto missing prices and suspicious tokens remain visibly excluded", "[app][crypto]") {
    auto missing = token("eth", tokenContract, "0x1", "0");
    auto suspicious = token("arb", tokenContract);
    suspicious["is_suspicious"] = true;
    const auto result = parse_crypto_portfolio(chains(), {missing, suspicious}, quote());
    REQUIRE(result);
    CHECK(result->unpricedCount == 2);
    CHECK(result->usdtMinor == 0);
    CHECK_FALSE(result->holdings.front().toMap().value("priced").toBool());
    CHECK_FALSE(parse_crypto_portfolio(chains(), {}, quote("0")));
    auto malformed = token(); malformed["raw_amount_hex_str"] = "0xno";
    CHECK_FALSE(parse_crypto_portfolio(chains(), {malformed}, quote()));
    malformed["raw_amount_hex_str"] = "0x" + QString(65, 'f');
    CHECK_FALSE(parse_crypto_portfolio(chains(), {malformed}, quote()));
    auto wrongQuote = quote(); wrongQuote["chain"] = "arb";
    CHECK_FALSE(parse_crypto_portfolio(chains(), {}, wrongQuote));
}

TEST_CASE("crypto keyless sync discovers used networks and explicitly reads every chain", "[app][crypto]") {
    Network network;
    network.responses = {jsonResponse(chains()), jsonResponse(chains()), jsonResponse(quote()),
        jsonResponse(QJsonArray{token()}), jsonResponse(QJsonArray{token("arb", "arb")})};
    const auto outcome = run(network);
    REQUIRE(outcome.completed == 1);
    REQUIRE(outcome.portfolio);
    CHECK(outcome.portfolio->usdtMinor == 400000);
    REQUIRE(network.requests.size() == 5);
    CHECK(network.onlyGets);
    for (const auto& request : network.requests) {
        CHECK(request.url().host() == "api.rabby.io");
        CHECK(request.url().scheme() == "https");
        CHECK_FALSE(request.hasRawHeader("Authorization"));
        CHECK_FALSE(request.hasRawHeader("AccessKey"));
        CHECK(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
    }
    CHECK(network.requests[0].url().path() == "/v1/chain/list");
    CHECK(network.requests[1].url().path() == "/v1/user/used_chain_list");
    for (const auto index : {3,4}) {
        const QUrlQuery query(network.requests[index].url());
        CHECK(query.queryItemValue("id") == wallet);
        CHECK(query.queryItemValue("chain_id") == (index == 3 ? "eth" : "arb"));
        CHECK(query.queryItemValue("is_all") == "true");
    }
}

TEST_CASE("crypto failed chains and invalid network responses never publish a partial portfolio", "[app][crypto]") {
    Network network;
    network.responses = portfolioResponses();
    SECTION("denied") { network.responses.last().status = 403; }
    SECTION("rate limited") {
        Response limited{"{}",429}; limited.retryAfter = "1";
        network.responses.last() = limited;
        network.responses.push_back(limited);
        network.responses.push_back(limited);
    }
    SECTION("TLS") { network.responses.last().error = QNetworkReply::SslHandshakeFailedError; }
    SECTION("oversize") { network.responses.last().body = QByteArray(4*1024*1024+1, ' '); }
    SECTION("wrong chain") { network.responses.last() = jsonResponse(QJsonArray{token("arb", "arb")}); }
    SECTION("wrapped response") { network.responses.last() = jsonResponse(QJsonObject{{"items", QJsonArray{}}}); }
    SECTION("unknown used network") { network.responses[1] = jsonResponse(QJsonArray{QJsonObject{{"id", "unknown"}}}); }
    const auto outcome = run(network);
    CHECK(outcome.completed == 0);
    CHECK(outcome.failed == 1);
}

TEST_CASE("crypto cancellation discards scheduled requests and permits clean coordinator reuse", "[app][crypto]") {
    Network network;
    network.responses = {jsonResponse(chains())};
    Outcome outcome;
    CryptoWalletSync sync(network, [&](CryptoPortfolio p) { ++outcome.completed; outcome.portfolio = std::move(p); },
        [&](QString error) { ++outcome.failed; outcome.message = error; }, nullptr);
    sync.start(wallet);
    REQUIRE(waitUntil([&] { return network.requests.size() == 1; }));
    QCoreApplication::processEvents();
    sync.cancel();
    network.responses = portfolioResponses({});
    sync.start(wallet);
    REQUIRE(waitUntil([&] { return outcome.completed + outcome.failed > 0; }));
    CHECK(outcome.completed == 1);
    REQUIRE(outcome.portfolio);
    CHECK(outcome.portfolio->holdings.isEmpty());
    CHECK_FALSE(network.unexpectedRequest);
    CHECK(network.requests.size() == 5);
}

TEST_CASE("controller keyless crypto history survives failure restart and removal", "[app][crypto]") {
    SettingsScope settings;
    REQUIRE(settings.directory.isValid());
    const auto secrets = std::make_shared<Secrets>();
    const auto path = settings.directory.filePath("history.db");
    QVariantMap saved;
    {
        auto network = std::make_unique<Network>(); auto* observed = network.get();
        observed->responses = portfolioResponses();
        wealthtracker::app::AppController controller(std::make_unique<Credentials>(secrets), std::move(network), nullptr, path);
        REQUIRE(controller.addCryptoWallet("Synthetic wallet", wallet));
        REQUIRE(waitUntil([&] { return !controller.cryptoSyncRunning(); }));
        saved = controller.balanceHistoryChart({}, "USDT");
        CHECK(saved.value("observationCount").toInt() == 1);
        CHECK(saved.value("lastValue").toString() == "2000.00 USDT");
        CHECK_FALSE(secrets->contains("blockscout-api-key"));
        observed->responses = {{"{}",403}};
        REQUIRE(controller.refreshCryptoWallets());
        REQUIRE(waitUntil([&] { return !controller.cryptoSyncRunning(); }));
        CHECK(controller.balanceHistoryChart({}, "USDT") == saved);
    }
    {
        auto network = std::make_unique<Network>(); network->responses = {{"{}",403}};
        wealthtracker::app::AppController restored(std::make_unique<Credentials>(secrets), std::move(network), nullptr, path);
        REQUIRE(restored.cryptoWallets().size() == 1);
        CHECK(restored.cryptoWallets().front().toMap().value("value").toString() == "2000.00 USDT");
        REQUIRE(waitUntil([&] { return !restored.cryptoSyncRunning(); }));
        QCoreApplication::processEvents();
        REQUIRE(waitUntil([&] { return !restored.cryptoSyncRunning(); }));
        REQUIRE(restored.removeCryptoWallet(wallet));
        CHECK(restored.balanceHistoryChart({}, "USDT") == saved);
    }
    auto network = std::make_unique<Network>();
    wealthtracker::app::AppController restored(std::make_unique<Credentials>(secrets), std::move(network), nullptr, path);
    CHECK(restored.cryptoWallets().isEmpty());
    CHECK(restored.balanceHistoryChart({}, "USDT") == saved);
}

TEST_CASE("controller privacy lock cancels keyless crypto refresh", "[app][crypto]") {
    SettingsScope settings;
    const auto secrets = std::make_shared<Secrets>();
    auto network = std::make_unique<Network>(); auto* observed = network.get();
    observed->responses = portfolioResponses();
    wealthtracker::app::AppController controller(std::make_unique<Credentials>(secrets), std::move(network), nullptr,
        settings.directory.filePath("history.db"));
    REQUIRE(controller.addCryptoWallet("Wallet",wallet));
    controller.lock();
    CHECK_FALSE(controller.cryptoSyncRunning());
    CHECK_FALSE(controller.refreshCryptoWallets());
    CHECK_FALSE(waitUntil([&] { return observed->requests.size() > 1; }, 650));
    CHECK(controller.balanceHistoryChart({}, "USDT").value("observationCount").toInt() == 0);
}

TEST_CASE("crypto honors a rate limit pause before completing the same chain", "[app][crypto]") {
    Network network;
    network.responses = portfolioResponses();
    Response limited{"{}",429}; limited.retryAfter="1";
    network.responses.insert(3,limited);
    const auto result=run(network);
    REQUIRE(result.completed==1);
    REQUIRE(network.requests.size()==5);
    CHECK(network.requests[3].url()==network.requests[4].url());
}
