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

    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral("page.app.beginVtbLogin()")));
    CHECK(source.contains(QByteArrayLiteral("page.app.vtbWebView")));
    CHECK(source.contains(QByteArrayLiteral("page.app.synchronizeVtb()")));

    QFile coordinator(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/vtb_login.cppm"));
    REQUIRE(coordinator.open(QIODevice::ReadOnly));
    const auto coordinatorSource = coordinator.readAll();
    CHECK(coordinatorSource.contains(QByteArrayLiteral("LocalStorageEnabled, false")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("AllowFileAccess, false")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("deleteAllCookies()")));
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

    engine.evaluate(QStringLiteral("delete globalThis.__wealthtrackerVtbSync; status = 401"));
    run();
    CHECK(engine.evaluate(QStringLiteral("__wealthtrackerVtbSync.status")).toString() ==
          QStringLiteral("authentication"));
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
            R"({"version":1,"stage":"loading","authenticated":true,"accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(loading);
    CHECK(loading->stage == VtbProjectionStage::Loading);

    const auto drift = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":true,"diagnostic":"VTB-P1","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(drift);
    CHECK(drift->stage == VtbProjectionStage::FrontendChanged);
    CHECK(drift->diagnostic == QStringLiteral("VTB-P1"));

    const auto portfolioRequest = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":true,"diagnostic":"VTB-P3","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(portfolioRequest);
    CHECK(portfolioRequest->diagnostic == QStringLiteral("VTB-P3"));

    const auto unsafeDiagnostic = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"frontend-changed","authenticated":true,"diagnostic":"account-40817810000000000001","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE_FALSE(unsafeDiagnostic);

    const auto unknown = parse_vtb_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"unknown","authenticated":false,"accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE_FALSE(unknown);
}
