#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJSEngine>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <expected>
#include <memory>
#include <optional>

import wt.connectors;
import wt.vtb_browser;

namespace {

class MemoryCredentialBackend final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }

    std::expected<void, QString> put(const QString& key,
                                     const QByteArray& secret) override {
        values.insert(key, secret);
        return {};
    }

    std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) override {
        const auto value = values.constFind(key);
        if (value == values.cend()) {
            return std::optional<QByteArray>{};
        }
        return std::optional<QByteArray>{*value};
    }

    std::expected<void, QString> remove(const QString& key) override {
        values.remove(key);
        return {};
    }

    QHash<QString, QByteArray> values;
};

QVariant connectionValue(QAbstractItemModel* model, const QString& connectorId,
                         const int role) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, role);
        }
    }
    return {};
}

class VtbSettingsCleanup final {
public:
    VtbSettingsCleanup() { QSettings{}.remove(QStringLiteral("vtb")); }
    ~VtbSettingsCleanup() { QSettings{}.remove(QStringLiteral("vtb")); }
};

} // namespace

TEST_CASE("VTB is an available embedded retail connection") {
    const VtbSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials), nullptr);

    CHECK(connectionValue(controller.bankConnections(), QStringLiteral("vtb-retail"),
                          wealthtracker::app::ConnectionModel::AvailableRole)
              .toBool());
    CHECK(connectionValue(controller.bankConnections(), QStringLiteral("vtb-retail"),
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("встроенный коннектор")));
    controller.describeBankConnection(QStringLiteral("vtb-retail"));
    CHECK(controller.connectionMessage().contains(QStringLiteral("системный WebView")));
    CHECK(controller.connectionMessage().contains(QStringLiteral("не читает cookies")));
    CHECK(controller.connectionMessage().contains(QStringLiteral("жёстких лимитов")));
    CHECK_FALSE(controller.connectionMessage().contains(QStringLiteral("три фиксированных")));

    QFile coordinator(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/vtb_login.cppm"));
    REQUIRE(coordinator.open(QIODevice::ReadOnly));
    const auto coordinatorSource = coordinator.readAll();
    CHECK(coordinatorSource.contains(QByteArrayLiteral("persistentBankBrowserSessions")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("AllowFileAccess, false")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("clearBankBrowserData")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("QTimer::singleShot(0, view")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("view->deleteLater()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("about:blank")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("web_view_.reset()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("cookieStore()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("QDesktopServices")));
}

TEST_CASE("VTB synchronization uses only exact page-owned read routes") {
    using wealthtracker::app::vtb_financial_projection_script;
    using wealthtracker::app::vtb_sync_start_script;

    const auto start = vtb_sync_start_script();
    CHECK(start.contains(QStringLiteral("https:")));
    CHECK(start.contains(QStringLiteral("online.vtb.ru")));
    CHECK(start.contains(QStringLiteral("./HttpClient")));
    CHECK(start.contains(QStringLiteral(
        "/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active")));
    CHECK(start.contains(QStringLiteral(
        "/msa/api-gw/private/portfolio/portfolio-balances/financial/summary")));
    CHECK(start.count(QStringLiteral("client.get(")) == 2);
    CHECK_FALSE(start.contains(QStringLiteral("history-hub")));
    CHECK_FALSE(start.contains(QStringLiteral("XMLHttpRequest")));
    CHECK_FALSE(start.contains(QStringLiteral("history.pushState")));
    CHECK_FALSE(start.contains(QStringLiteral("client.post")));
    CHECK_FALSE(start.contains(QStringLiteral("document.cookie"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("localStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("sessionStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("password:"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("oneTimeCode"), Qt::CaseInsensitive));

    const auto projection = vtb_financial_projection_script();
    CHECK(start.contains(QStringLiteral("requestProductTypes")));
    CHECK(projection.contains(QStringLiteral("balanceMinor")));
}

TEST_CASE("VTB waits for the bank's explicit unauthenticated state without private reads") {
    using namespace wealthtracker::app;
    QJSEngine engine;
    REQUIRE_FALSE(engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = {href:'https://online.vtb.ru/'}
        globalThis.URL = function() {return {protocol:'https:',hostname:'online.vtb.ru',port:'',username:'',password:''}}
        globalThis.TextEncoder = function() {this.encode = value => ({byteLength:String(value).length})}
        globalThis.authorized = false
        globalThis.initializing = false
        globalThis.privateReads = 0
        globalThis.clientLookups = 0
        globalThis.captures = 0
        const auth = {}
        Object.defineProperty(auth, 'isAuthorized', {get: () => {
            if (initializing) throw new Error('service still initializing')
            return authorized
        }})
        Object.defineProperty(auth, 'accessToken', {get: () => {throw new Error('token access forbidden')}})
        const runtime = function() {throw new Error('module initialization forbidden')}
        runtime.c = {11740:{exports:{y1:auth}}}
        globalThis.webpackChunkmf_1488_core = []
        webpackChunkmf_1488_core.push = function(entry) {
            captures++
            Array.prototype.push.call(this,entry)
            entry[2](runtime)
        }
        globalThis.mf_1488_core = {get: () => {
            clientLookups++
            return Promise.resolve(() => ({http:{get:path => {
                privateReads++
                return Promise.resolve({data:path.endsWith('/portfolios/active') ?
                    {portfolio:{accounts:[{publicId:'fixture',balance:{sum:'1.00',currency:'RUB'}}]}} :
                    {balanceResponse:[]}})
            }}}))
        }}
    )JS")).isError());
    QString expected = QStringLiteral("authentication");
    SECTION("bank explicitly reports not authorized") {}
    SECTION("bank service is initializing") {
        engine.evaluate(QStringLiteral("initializing = true"));
        expected = QStringLiteral("waiting");
    }
    const auto run = [&] {
        REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
        for (int i = 0; i < 32; ++i) {
            QCoreApplication::processEvents();
            engine.evaluate(QStringLiteral("0"));
        }
    };
    run();
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.status")).toString() == expected);
    CHECK(engine.evaluate(QStringLiteral("privateReads")).toInt() == 0);
    CHECK(engine.evaluate(QStringLiteral("clientLookups")).toInt() == 0);
    engine.evaluate(QStringLiteral("initializing = false; authorized = true; delete __wealthtrackerVtbSync"));
    run();
    const auto result = parse_vtb_financial_projection(
        engine.evaluate(vtb_financial_projection_script()).toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    REQUIRE(result->batch);
    CHECK(result->batch->accounts.size() == 1);
    CHECK(engine.evaluate(QStringLiteral("privateReads")).toInt() == 2);
    CHECK(engine.evaluate(QStringLiteral("captures")).toInt() == 1);
    CHECK(engine.evaluate(QStringLiteral("webpackChunkmf_1488_core.length")).toInt() == 0);
}

TEST_CASE("VTB balances use the authenticated client without history or navigation") {
    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'online.vtb.ru', port: '',
                     username: '', password: '', origin: 'https://online.vtb.ru' }
        }
        globalThis.TextEncoder = function() {
            this.encode = function(value) { return { byteLength: String(value).length } }
        }
        globalThis.setTimeout = function(callback) { callback(); return 0 }
        globalThis.requests = []
        globalThis.portfolioReads = 0
        globalThis.status = 200
        globalThis.amount = '125.50'
        globalThis.supplementStatus = 404
        globalThis.mf_1488_core = { get: function() {
            return Promise.resolve(function() { return { http: { get: function(path) {
                requests.push(path)
                if (path.endsWith('/portfolios/active')) {
                    portfolioReads++
                    if (status !== 200) return Promise.reject({status:status})
                    if (portfolioReads === 1) return Promise.resolve({data:{loadStatus:'LOADING'}})
                    return Promise.resolve({data:{portfolio:{accounts:[
                        {publicId:'account-a',name:'Account',balance:{sum:amount,currency:'RUB'}},
                        {publicId:'account-b',name:'Savings',balance:{sum:'10.00',currency:'RUB'}}
                    ]}}})
                }
                if (path.endsWith('/financial/summary')) return Promise.reject({status:supplementStatus})
                throw new Error('Unexpected route')
            } } } })
        } }
    )JS"));
    REQUIRE_FALSE(setup.isError());
    auto run = [&] {
        const auto started = engine.evaluate(wealthtracker::app::vtb_sync_start_script());
        REQUIRE_FALSE(started.isError());
        for (int i = 0; i < 32; ++i) {
            QCoreApplication::processEvents();
            engine.evaluate(QStringLiteral("0"));
        }
    };
    run();
    auto result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    REQUIRE(result->batch);
    CHECK(result->batch->accounts.size() == 2);
    CHECK(result->batch->balances.front().balance.minorUnits == 12550);
    CHECK(result->batch->transactions.isEmpty());
    CHECK(result->batch->warnings.isEmpty());
    CHECK(engine.evaluate(QStringLiteral("requests.length")).toInt() == 3);

    // Refresh the existing page client; no history route or page navigation exists in this fixture.
    engine.evaluate(QStringLiteral("delete globalThis.__wealthtrackerVtbSync; amount = '150.75'"));
    run();
    result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    REQUIRE(result->batch);
    CHECK(result->batch->balances.front().balance.minorUnits == 15075);
    CHECK(engine.evaluate(QStringLiteral("requests.length")).toInt() == 5);

    // Access denial must fail closed without claiming the saved session expired.
    engine.evaluate(QStringLiteral("delete globalThis.__wealthtrackerVtbSync; status = 403"));
    run();
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.status")).toString() ==
          QStringLiteral("failed"));
    result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    CHECK_FALSE(result->batch);
    CHECK(result->stage == wealthtracker::app::VtbProjectionStage::FrontendChanged);
    CHECK(result->diagnostic == QStringLiteral("VTB-DENIED"));

    engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; status = 200; supplementStatus = 403"));
    run();
    result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    CHECK_FALSE(result->batch);
    CHECK(result->diagnostic == QStringLiteral("VTB-DENIED"));

    engine.evaluate(QStringLiteral("delete globalThis.__wealthtrackerVtbSync; status = 401"));
    run();
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.status")).toString() ==
          QStringLiteral("authentication"));
    result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    CHECK_FALSE(result->batch);
    CHECK(result->diagnostic == QStringLiteral("VTB-AUTH-P"));

    engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; status = 200; supplementStatus = 401"));
    run();
    result = wealthtracker::app::parse_vtb_financial_projection(
        engine.evaluate(wealthtracker::app::vtb_financial_projection_script()).toString(),
        QDateTime::currentDateTimeUtc());
    REQUIRE(result);
    CHECK_FALSE(result->batch);
    CHECK(result->diagnostic == QStringLiteral("VTB-AUTH-B"));
}

TEST_CASE("VTB balances retain child card masks and prefer the balance summary") {
    using wealthtracker::app::VtbProjectionStage;
    using wealthtracker::app::parse_vtb_financial_projection;
    using wealthtracker::app::vtb_financial_projection_script;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/home' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'online.vtb.ru', port: '',
                     username: '', password: '', origin: 'https://online.vtb.ru',
                     pathname: '/home' }
        }
        globalThis.__wealthtrackerVtbSync = {
            status: 'ready',
            portfolio: {
                cards: {
                    parent: {
                        publicId: 'card-account', name: 'Мультикарта',
                        amount: { sum: 1500.25, currency: 'RUR' },
                        cards: [{ publicId: 'physical-card', number: '2200123412341234' }]
                    }
                },
                accounts: [{
                    publicId: 'current-account', name: 'Текущий счёт',
                    balance: { sum: 100, currency: 'RUB' },
                    number: '40817810000000001234'
                }],
                deposits: [], loans: [], metalAccounts: [], wallets: []
            },
            balances: { balanceResponse: [{ data: {
                publicId: 'current-account',
                balance: { sum: 125.5, currency: 'RUB' }
            } }] }
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    INFO(evaluated.toString().toStdString());
    const auto parsed = parse_vtb_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(parsed);
    CHECK(parsed->diagnostic.isEmpty());
    CHECK(parsed->stage == VtbProjectionStage::Ready);
    REQUIRE(parsed->batch);
    CHECK(parsed->batch->warnings.isEmpty());
    REQUIRE(parsed->batch->accounts.size() == 2);
    CHECK(parsed->batch->balances.at(1).balance.minorUnits == 12550);
    CHECK(parsed->batch->balances.front().balance.minorUnits == 150025);
    CHECK(parsed->batch->accounts.front().maskedNumber == QStringLiteral("•••• 1234"));
    CHECK(parsed->batch->transactions.isEmpty());
}

TEST_CASE("VTB projection reports authentication, loading, and frontend drift") {
    using wealthtracker::app::VtbProjectionStage;
    using wealthtracker::app::parse_vtb_financial_projection;

    const auto observedAt = QDateTime::currentDateTimeUtc();
    const auto authentication = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"authentication","authenticated":false,"accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(authentication);
    CHECK(authentication->stage == VtbProjectionStage::Authentication);

    const auto loading = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"loading","authenticated":false,"accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(loading);
    CHECK(loading->stage == VtbProjectionStage::Loading);

    const auto drift = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":false,"diagnostic":"VTB-P1","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(drift);
    CHECK(drift->stage == VtbProjectionStage::FrontendChanged);
    CHECK(drift->diagnostic == QStringLiteral("VTB-P1"));

    const auto portfolioRequest = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":false,"diagnostic":"VTB-P3","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(portfolioRequest);
    CHECK(portfolioRequest->diagnostic == QStringLiteral("VTB-P3"));

    const auto unsafeDiagnostic = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":false,"diagnostic":"account-40817810000000000001","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE_FALSE(unsafeDiagnostic);

    const auto unknown = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"unknown","authenticated":false,"accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE_FALSE(unknown);
}

TEST_CASE("VTB frontend availability never proves authentication") {
    using namespace wealthtracker::app;
    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'online.vtb.ru', port: '',
                     username: '', password: '' }
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());
    auto projected = [&] {
        const auto value = engine.evaluate(vtb_financial_projection_script());
        REQUIRE_FALSE(value.isError());
        const auto result = parse_vtb_financial_projection(value.toString(), QDateTime::currentDateTimeUtc());
        REQUIRE(result);
        CHECK_FALSE(result->batch);
        CHECK_FALSE(value.toString().contains(QStringLiteral("\"authenticated\":true")));
        return result->stage;
    };

    // A public login page can load without exposing the authenticated module.
    CHECK(projected() == VtbProjectionStage::Authentication);
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    CHECK(projected() == VtbProjectionStage::Loading);
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.status")).toString() == QStringLiteral("waiting"));
    CHECK(projected() == VtbProjectionStage::Loading);

    engine.evaluate(QStringLiteral(R"JS(
        delete globalThis.__wealthtrackerVtbSync
        globalThis.mf_1488_core = { get: function() {
            return Promise.resolve(function() { return {http: {get: function() {
                return new Promise(function() {})
            }}} })
        }}
    )JS"));
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.requestStage")).toString() == QStringLiteral("portfolio"));
    CHECK(projected() == VtbProjectionStage::Loading);

    for (const auto& state : {QStringLiteral("running"), QStringLiteral("waiting")}) {
        engine.globalObject().setProperty(QStringLiteral("fixtureStatus"), state);
        engine.evaluate(QStringLiteral("__wealthtrackerVtbSync = {status: fixtureStatus, requestStage: 'portfolio'}"));
        CHECK(projected() == VtbProjectionStage::Loading);
    }
    engine.evaluate(QStringLiteral("__wealthtrackerVtbSync = {status: 'failed', reason: 'VTB-M1'}"));
    CHECK(projected() == VtbProjectionStage::FrontendChanged);
    engine.evaluate(QStringLiteral("__wealthtrackerVtbSync = {status: 'ready', portfolio: {accounts: []}}"));
    CHECK(projected() == VtbProjectionStage::FrontendChanged);
    engine.evaluate(QStringLiteral("__wealthtrackerVtbSync = {status: 'authentication', reason: 'VTB-AUTH-P'}"));
    CHECK(projected() == VtbProjectionStage::Authentication);
}

TEST_CASE("VTB ready projections require authenticated nonempty accounts") {
    using wealthtracker::app::parse_vtb_financial_projection;
    const auto observedAt = QDateTime::currentDateTimeUtc();
    for (const auto& value : {
             QStringLiteral(R"({"version":1,"stage":"ready","authenticated":false,"accounts":[{"id":"a","name":"Account","kind":"checking","currency":"RUB","balanceMinor":"100"}]})"),
             QStringLiteral(R"({"version":1,"stage":"ready","authenticated":true,"accounts":[]})"),
             QStringLiteral(R"({"version":1,"stage":"loading","authenticated":true,"accounts":[]})"),
             QStringLiteral(R"({"version":1,"stage":"frontend-changed","authenticated":true,"diagnostic":"VTB-P1","accounts":[]})")}) {
        CHECK_FALSE(parse_vtb_financial_projection(value, observedAt));
    }
}
