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
    CHECK(start.contains(QStringLiteral(
        "/msa/api-gw/private/history-hub/history-hub-homer/v1/history/")));
    CHECK(start.contains(QStringLiteral("client.get")));
    CHECK(start.count(QStringLiteral("client.get(")) == 3);
    CHECK(start.contains(QStringLiteral("historyWindowDays = 31")));
    CHECK(start.contains(QStringLiteral("setFullYear(rangeFrom.getFullYear() - 2)")));
    CHECK_FALSE(start.contains(QStringLiteral("client.post")));
    CHECK_FALSE(start.contains(QStringLiteral("fetch("), Qt::CaseInsensitive));
    CHECK(start.contains(QStringLiteral("XMLHttpRequest"), Qt::CaseInsensitive));
    CHECK(start.contains(QStringLiteral("history.pushState")));
    CHECK(start.contains(QStringLiteral("x-initiator-service")));
    CHECK(start.contains(QStringLiteral("x-smart-update-request")));
    CHECK_FALSE(start.contains(QStringLiteral("dateFrom")));
    CHECK_FALSE(start.contains(QStringLiteral("document.cookie"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("localStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("sessionStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("password:"), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("oneTimeCode"), Qt::CaseInsensitive));

    const auto projection = vtb_financial_projection_script();
    CHECK(start.contains(QStringLiteral("requestProductTypes")));
    CHECK(projection.contains(QStringLiteral("accountProductId")));
    CHECK(projection.contains(QStringLiteral("cardProductId")));
    CHECK(projection.contains(QStringLiteral("internalTransfer")));
    CHECK(projection.contains(QStringLiteral("balanceMinor")));
}

TEST_CASE("VTB synchronization follows the current portfolio and page history contracts") {
    using wealthtracker::app::VtbProjectionStage;
    using wealthtracker::app::parse_vtb_financial_projection;
    using wealthtracker::app::vtb_financial_projection_script;
    using wealthtracker::app::vtb_sync_start_script;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/login?post-auth=true' }
        globalThis.URL = function(value) {
            const text = String(value || '')
            const marker = '/msa/api-gw/private/history-hub/history-hub-homer/v1/history/'
            const markerIndex = text.indexOf(marker)
            const queryIndex = text.indexOf('?', markerIndex)
            const hostname = text.indexOf('evil.example') >= 0
                ? 'evil.example' : 'online.vtb.ru'
            return { protocol: 'https:', hostname: hostname, port: '',
                     username: '', password: '', origin: 'https://' + hostname,
                     href: text,
                     search: queryIndex >= 0 ? text.slice(queryIndex) : '',
                     pathname: markerIndex >= 0
                         ? text.slice(markerIndex, queryIndex >= 0 ? queryIndex : undefined)
                         : '/login' }
        }
        globalThis.TextEncoder = function() {
            this.encode = function(value) { return { byteLength: String(value).length } }
        }
        globalThis.vtbTimers = []
        globalThis.setTimeout = function(callback, milliseconds) {
            if (milliseconds === 5000) { callback(); return 0 }
            const timer = { callback: callback, milliseconds: milliseconds, active: true }
            globalThis.vtbTimers.push(timer)
            return globalThis.vtbTimers.length
        }
        globalThis.clearTimeout = function(identifier) {
            const timer = globalThis.vtbTimers[Number(identifier) - 1]
            if (timer) timer.active = false
        }
        globalThis.runVtbTimers = function(maximumMilliseconds) {
            const ready = globalThis.vtbTimers.filter(function(timer) {
                return timer.active && timer.milliseconds <= maximumMilliseconds
            })
            for (const timer of ready) {
                timer.active = false
                timer.callback()
            }
        }
        globalThis.vtbRequests = []
        globalThis.vtbPortfolioReads = 0
        globalThis.vtbPortfolioStatus = 200
        globalThis.vtbBalanceStatus = 404
        globalThis.vtbBalanceThrows = false
        globalThis.vtbHistoryStatus = 200
        globalThis.vtbHistoryReads = 0
        globalThis.vtbHistoryReplayReads = 0
        globalThis.vtbHistoryReplayOptions = []
        globalThis.vtbHistoryPaginationEnabled = true
        globalThis.vtbHoldHistoryReplay = false
        globalThis.vtbHeldHistoryResolvers = []
        globalThis.vtbPageHistoryRequests = 0
        globalThis.vtbObservedHistoryMethod = ''
        globalThis.vtbObservedHistoryUrl = ''
        globalThis.XMLHttpRequest = function() { this.listeners = {} }
        XMLHttpRequest.prototype.addEventListener = function(name, callback) {
            this.listeners[name] = callback
        }
        XMLHttpRequest.prototype.open = function(method, url) {
            this.method = method
            this.url = url
        }
        XMLHttpRequest.prototype.send = function() {
            if (String(this.url).indexOf('/history/byProducts') === -1) return
            globalThis.vtbPageHistoryRequests += 1
            this.status = globalThis.vtbHistoryStatus
            this.response = null
            this.responseText = this.status === 200 ? JSON.stringify({ operations: [{
                operationId: 'history-' + globalThis.vtbPageHistoryRequests,
                accountProductId: 'account-1',
                operationDate: '2026-08-14T12:30:00.000Z', debet: true,
                operationAmount: { sum: 1, currency: 'RUB' }
            }] }) : ''
            if (this.listeners.loadend) {
                globalThis.vtbHistoryReads += 1
                globalThis.vtbObservedHistoryMethod = this.method
                globalThis.vtbObservedHistoryUrl = this.url
                this.listeners.loadend()
            }
        }
        globalThis.vtbOriginalOpen = XMLHttpRequest.prototype.open
        globalThis.vtbOriginalSend = XMLHttpRequest.prototype.send
        globalThis.history = { pushState: function(_, __, path) {
            globalThis.vtbOpenedPath = path
        } }
        globalThis.PopStateEvent = function(name) { this.type = name }
        globalThis.dispatchEvent = function() {
            const writeRequest = new XMLHttpRequest()
            writeRequest.open('POST',
                '/msa/api-gw/private/history-hub/history-hub-homer/v1/history/byProducts')
            writeRequest.send()
            const foreignRequest = new XMLHttpRequest()
            foreignRequest.open('GET',
                'https://evil.example/msa/api-gw/private/history-hub/history-hub-homer/v1/history/byProducts')
            foreignRequest.send()
            const request = new XMLHttpRequest()
            request.open('GET',
                '/msa/api-gw/private/history-hub/history-hub-homer/v1/history/byProducts?dateFrom=25.07.2026&dateTo=25.08.2026&product=account-1&page=0&pageSize=2')
            request.send()
        }
        const client = {
            get: function(path, options) {
                globalThis.vtbRequests.push({ path: path, options: options || {} })
                if (path.indexOf('/portfolios/active') !== -1) {
                    globalThis.vtbPortfolioReads += 1
                    if (globalThis.vtbPortfolioStatus !== 200)
                        return Promise.reject({ status: globalThis.vtbPortfolioStatus })
                    if (globalThis.vtbPortfolioReads === 1)
                        return Promise.resolve({ data: { loadStatus: 'LOADING' } })
                    return Promise.resolve({ data: {
                        loadStatus: 'READY', cards: {}, deposits: {}, loans: {},
                        metalAccounts: {}, wallets: {}, accounts: {
                            current: { publicId: 'account-1', name: 'Счёт',
                                       balance: { sum: 100, currency: 'RUB' } }
                        }
                    } })
                }
                if (path.indexOf('/financial/summary') !== -1) {
                    if (globalThis.vtbBalanceThrows)
                        throw { message: 'supplement unavailable' }
                    return Promise.reject({ statusCode: globalThis.vtbBalanceStatus })
                }
                if (path.indexOf('/history/') !== -1) {
                    globalThis.vtbHistoryReplayReads += 1
                    globalThis.vtbHistoryReplayOptions.push(options || {})
                    const firstPage = globalThis.vtbHistoryPaginationEnabled &&
                        globalThis.vtbHistoryReplayReads === 1 &&
                        Number(options.params.page) === 0
                    const response = { data: { operations: [],
                        pagination: { hasMore: firstPage } } }
                    if (globalThis.vtbHoldHistoryReplay)
                        return new Promise(function(resolve) {
                            globalThis.vtbHeldHistoryResolvers.push(function() {
                                resolve(response)
                            })
                        })
                    return Promise.resolve(response)
                }
                return Promise.reject({ status: 500 })
            }
        }
        globalThis.mf_1488_core = {
            get: function() { return Promise.resolve(function() { return { http: client } }) }
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto started = engine.evaluate(vtb_sync_start_script());
    INFO((started.toString() + QStringLiteral(" at line ") +
          started.property(QStringLiteral("lineNumber")).toString())
             .toStdString());
    REQUIRE_FALSE(started.isError());
    for (int index = 0; index < 256; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(2000)"))
                      .isError());
    for (int index = 0; index < 256; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }

    INFO(engine.evaluate(QStringLiteral(
             "JSON.stringify(globalThis.__wealthtrackerVtbSync)"))
             .toString()
             .toStdString());
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbPortfolioReads")).toInt() == 2);
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbHistoryReads")).toInt() == 1);
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbHistoryReplayReads")).toInt() == 25);
    const auto expectedHistoryFrom =
        QDate::currentDate().addYears(-2).toString(QStringLiteral("dd.MM.yyyy"));
    const auto expectedHistoryTo =
        QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy"));
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbHistoryReplayOptions[0].params.dateFrom"))
              .toString() == expectedHistoryFrom);
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbHistoryReplayOptions[24].params.dateTo"))
              .toString() == expectedHistoryTo);
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbHistoryReplayOptions[0].params.product === 'account-1' && "
              "globalThis.vtbHistoryReplayOptions[0].params.page === '0' && "
              "globalThis.vtbHistoryReplayOptions[1].params.page === '1' && "
              "globalThis.vtbHistoryReplayOptions.every(function(options) { "
              "return options.params.dateFrom && options.params.dateTo && "
              "options.headers['x-initiator-service'] === 'web_history_page' })"))
              .toBool());
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbPageHistoryRequests")).toInt() == 3);
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbOpenedPath")).toString() ==
          QStringLiteral("/history"));
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbObservedHistoryMethod")).toString() ==
          QStringLiteral("GET"));
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbObservedHistoryUrl")).toString()
              .contains(QStringLiteral("/history/byProducts")));
    CHECK(engine.evaluate(QStringLiteral(
              "XMLHttpRequest.prototype.open === globalThis.vtbOriginalOpen && "
              "XMLHttpRequest.prototype.send === globalThis.vtbOriginalSend"))
              .toBool());

    const auto evaluated = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto parsed = parse_vtb_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(parsed);
    CHECK(parsed->stage == VtbProjectionStage::Ready);
    REQUIRE(parsed->batch);
    CHECK(parsed->batch->accounts.size() == 1);
    CHECK(parsed->batch->transactions.size() == 1);

    const auto timeoutRestart = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbBalanceStatus = 404; "
        "globalThis.vtbHistoryStatus = 200; globalThis.vtbHistoryReads = 0; "
        "globalThis.vtbHistoryReplayReads = 0; "
        "globalThis.vtbHistoryReplayOptions = []; "
        "globalThis.vtbHistoryPaginationEnabled = false; "
        "globalThis.vtbHoldHistoryReplay = true; "
        "globalThis.vtbHeldHistoryResolvers = [];"));
    REQUIRE_FALSE(timeoutRestart.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(2000)"))
                      .isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbHistoryReplayReads === 1 && "
              "globalThis.vtbHeldHistoryResolvers.length === 1"))
              .toBool());
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(200000)"))
                      .isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'ready' && "
              "globalThis.__wealthtrackerVtbSync.historyComplete === false"))
              .toBool());
    REQUIRE_FALSE(engine.evaluate(QStringLiteral(
        "globalThis.vtbHeldHistoryResolvers[0]()"))
                      .isError());
    for (int index = 0; index < 128; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbHistoryReplayReads")).toInt() == 1);

    const auto restarted = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbBalanceStatus = 401; "
        "globalThis.vtbHistoryReads = 0; globalThis.vtbHoldHistoryReplay = false; "
        "globalThis.vtbHistoryPaginationEnabled = true;"));
    REQUIRE_FALSE(restarted.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(2000)"))
                      .isError());
    for (int index = 0; index < 256; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'authentication'"))
              .toBool());

    const auto partialRestart = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbBalanceStatus = 404; "
        "globalThis.vtbHistoryStatus = 503; globalThis.vtbHistoryReads = 0;"));
    REQUIRE_FALSE(partialRestart.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(2000)"))
                      .isError());
    for (int index = 0; index < 256; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'ready' && "
              "globalThis.__wealthtrackerVtbSync.historyComplete === false"))
              .toBool());
    const auto partialEvaluation = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(partialEvaluation.isError());
    const auto partial = parse_vtb_financial_projection(
        partialEvaluation.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(partial);
    CHECK(partial->stage == VtbProjectionStage::Ready);
    CHECK(partial->diagnostic == QStringLiteral("VTB-H2"));
    REQUIRE(partial->batch);
    CHECK(partial->batch->accounts.size() == 1);
    CHECK(partial->batch->transactions.isEmpty());
    REQUIRE(partial->batch->warnings.size() == 1);
    CHECK(partial->batch->warnings.front().code ==
          QStringLiteral("vtb-history-incomplete"));

    const auto throwingSupplementRestart = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbBalanceThrows = true; "
        "globalThis.vtbHistoryStatus = 200; globalThis.vtbHistoryReads = 0;"));
    REQUIRE_FALSE(throwingSupplementRestart.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("globalThis.runVtbTimers(2000)"))
                      .isError());
    for (int index = 0; index < 256; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'ready' && "
              "globalThis.vtbHistoryReads === 1"))
              .toBool());

    const auto failedPortfolioRestart = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbPortfolioStatus = 503; "
        "globalThis.vtbBalanceThrows = false; globalThis.vtbHistoryStatus = 200; "
        "globalThis.vtbHistoryReads = 0;"));
    REQUIRE_FALSE(failedPortfolioRestart.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'failed' && "
              "globalThis.__wealthtrackerVtbSync.reason === 'VTB-P3'"))
              .toBool());
}

TEST_CASE("VTB projection marks size truncation as partial history") {
    using wealthtracker::app::parse_vtb_financial_projection;
    using wealthtracker::app::vtb_financial_projection_script;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'online.vtb.ru', port: '',
                     username: '', password: '', origin: 'https://online.vtb.ru',
                     pathname: '/' }
        }
        globalThis.TextEncoder = function() {
            this.encode = function(value) { return { byteLength: String(value).length } }
        }
        const operations = []
        for (let index = 0; index < 20000; ++index) {
            operations.push({
                operationId: 'operation-' + index,
                accountProductId: 'account-1',
                operationDate: '2026-08-14T12:30:00.000Z',
                debet: true,
                operationAmount: { sum: 1, currency: 'RUB' },
                description: 'x'.repeat(256),
                merchant: { title: 'y'.repeat(256) },
                category: { name: 'z'.repeat(256) }
            })
        }
        globalThis.__wealthtrackerVtbSync = {
            status: 'ready', historyComplete: true,
            portfolio: { data: { accounts: [{
                publicId: 'account-1', name: 'Account',
                balance: { sum: 100, currency: 'RUB' }
            }] } },
            balances: {}, history: { operations: operations }
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto parsed = parse_vtb_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(parsed);
    CHECK(parsed->diagnostic == QStringLiteral("VTB-H2"));
    REQUIRE(parsed->batch);
    CHECK(parsed->batch->transactions.size() < 20000);
    REQUIRE(parsed->batch->warnings.size() == 1);
    CHECK(parsed->batch->warnings.front().code ==
          QStringLiteral("vtb-history-incomplete"));
}

TEST_CASE("VTB projection maps child cards and bank transfer markers") {
    using wealthtracker::app::VtbProjectionStage;
    using wealthtracker::app::parse_vtb_financial_projection;
    using wealthtracker::app::vtb_financial_projection_script;
    using wealthtracker::connectors::NormalizedTransactionKind;

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
            } }] },
            history: { operations: [{
                operationId: 'purchase-1', cardProductId: 'physical-card',
                operationDate: '2026-08-14T12:30:00.000Z', debet: true,
                operationAmount: { sum: 259.9, currency: 'RUB' },
                merchant: { title: 'Coffee', mcc: '5814' },
                parentCategory: { id: 7, name: 'Кафе' }
            }, {
                operationId: 'transfer-1', accountProductId: 'current-account',
                operationDate: '2026-08-13T09:00:00.000Z', debet: false,
                operationAmount: { sum: 500, currency: 'RUB' },
                operationName: 'Между своими счетами', operationCode: 3154,
                parentCategory: { id: 101, name: 'Переводы' }
            }] }
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    INFO(evaluated.toString().toStdString());
    const auto parsed = parse_vtb_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(parsed);
    CHECK(parsed->stage == VtbProjectionStage::Ready);
    REQUIRE(parsed->batch);
    REQUIRE(parsed->batch->accounts.size() == 2);
    CHECK(parsed->batch->balances.at(1).balance.minorUnits == 12550);
    REQUIRE(parsed->batch->transactions.size() == 2);
    CHECK(parsed->batch->transactions.front().accountExternalId ==
          QStringLiteral("card-account"));
    CHECK(parsed->batch->transactions.front().amount.minorUnits == -25990);
    CHECK(parsed->batch->transactions.back().kind ==
          NormalizedTransactionKind::InternalTransfer);
    CHECK(parsed->batch->transactions.back().bankCategory ==
          QStringLiteral("Внутренний перевод"));
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
            R"({"version":1,"stage":"frontend-changed","authenticated":true,"diagnostic":"VTB-H1","accounts":[],"transactions":[]})"),
        observedAt);
    REQUIRE(drift);
    CHECK(drift->stage == VtbProjectionStage::FrontendChanged);
    CHECK(drift->diagnostic == QStringLiteral("VTB-H1"));

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
