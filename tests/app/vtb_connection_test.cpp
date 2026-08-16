#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJSEngine>
#include <QRegularExpression>
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
    CHECK(coordinatorSource.contains(QByteArrayLiteral("web_view_.reset()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("cookieStore()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("QDesktopServices")));
}

TEST_CASE("VTB synchronization uses only exact read routes through the bank client") {
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
        "/msa/api-gw/private/history-hub/history-hub-homer/v1/history/byUser")));
    CHECK(start.contains(QStringLiteral("client.get")));
    CHECK(start.count(QStringLiteral("client.get(")) == 3);
    CHECK_FALSE(start.contains(QStringLiteral("client.post")));
    CHECK_FALSE(start.contains(QStringLiteral("fetch("), Qt::CaseInsensitive));
    CHECK_FALSE(start.contains(QStringLiteral("XMLHttpRequest"), Qt::CaseInsensitive));
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

TEST_CASE("VTB synchronization follows the current portfolio and history contracts") {
    using wealthtracker::app::VtbProjectionStage;
    using wealthtracker::app::parse_vtb_financial_projection;
    using wealthtracker::app::vtb_financial_projection_script;
    using wealthtracker::app::vtb_sync_start_script;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://online.vtb.ru/home' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'online.vtb.ru', port: '',
                     username: '', password: '', origin: 'https://online.vtb.ru',
                     pathname: '/home' }
        }
        globalThis.TextEncoder = function() {
            this.encode = function(value) { return { byteLength: String(value).length } }
        }
        globalThis.setTimeout = function(callback) { callback(); return 1 }
        globalThis.crypto = { randomUUID: function() { return 'request-id' } }
        globalThis.vtbRequests = []
        globalThis.vtbPortfolioReads = 0
        globalThis.vtbBalanceStatus = 404
        const client = {
            get: function(path, options) {
                globalThis.vtbRequests.push({ path: path, options: options || {} })
                if (path.indexOf('/portfolios/active') !== -1) {
                    globalThis.vtbPortfolioReads += 1
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
                if (path.indexOf('/financial/summary') !== -1)
                    return Promise.reject({ response: { status: globalThis.vtbBalanceStatus } })
                return Promise.resolve({ data: { operations: [] } })
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
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }

    INFO(engine.evaluate(QStringLiteral(
             "JSON.stringify(globalThis.__wealthtrackerVtbSync)"))
             .toString()
             .toStdString());
    CHECK(engine.evaluate(QStringLiteral("globalThis.vtbPortfolioReads")).toInt() == 2);
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbRequests[3].options.params.dateFrom"))
              .toString()
              .contains(QRegularExpression(QStringLiteral(R"(^\d{2}\.\d{2}\.\d{4}$)"))));
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.vtbRequests[3].options.params.dateTo"))
              .toString()
              .contains(QRegularExpression(QStringLiteral(R"(^\d{2}\.\d{2}\.\d{4}$)"))));

    const auto evaluated = engine.evaluate(vtb_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto parsed = parse_vtb_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(parsed);
    CHECK(parsed->stage == VtbProjectionStage::Ready);
    REQUIRE(parsed->batch);
    CHECK(parsed->batch->accounts.size() == 1);

    const auto restarted = engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerVtbSync; "
        "globalThis.vtbPortfolioReads = 0; globalThis.vtbBalanceStatus = 401;"));
    REQUIRE_FALSE(restarted.isError());
    REQUIRE_FALSE(engine.evaluate(vtb_sync_start_script()).isError());
    for (int index = 0; index < 64; ++index) {
        QCoreApplication::processEvents();
        (void)engine.evaluate(QStringLiteral("0"));
    }
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerVtbSync.status === 'authentication'"))
              .toBool());
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
