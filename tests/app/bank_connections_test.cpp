#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

import wt.tbank_auth;
import wt.tbank_browser;
import wt.alfa_browser;
import wt.alfa_login;
import wt.bank_sync;
import wt.connectors;

namespace {

class MemoryCredentialBackend final
    : public wealthtracker::app::CredentialBackend {
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

class BufferedNetworkReply final : public QNetworkReply {
  public:
    BufferedNetworkReply(const QNetworkRequest& request, QByteArray body,
                         const int delayMilliseconds, const QNetworkReply::NetworkError error,
                         int* readCounter, QObject* parent)
        : QNetworkReply(parent), body_(std::move(body)), error_(error), read_counter_(readCounter) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(delayMilliseconds, this, [this] { finish(); });
    }

    void abort() override {
        if (isFinished()) {
            return;
        }
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Request cancelled"));
        finish();
    }

    qint64 bytesAvailable() const override {
        return body_.size() - offset_ + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char* data, const qint64 maximumSize) override {
        if (read_counter_ != nullptr) {
            ++*read_counter_;
        }
        if (offset_ >= body_.size()) {
            return -1;
        }
        const auto size = std::min(maximumSize, body_.size() - offset_);
        std::copy_n(body_.constData() + offset_, size, data);
        offset_ += size;
        return size;
    }

  private:
    void finish() {
        if (isFinished()) {
            return;
        }
        if (error_ != QNetworkReply::NoError) {
            setError(error_, QStringLiteral("Scripted network failure"));
        }
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    QByteArray body_;
    QNetworkReply::NetworkError error_;
    int* read_counter_ = nullptr;
    qint64 offset_ = 0;
};

class TBankNetworkFixture final : public QNetworkAccessManager {
  public:
    struct OperationRange {
        QString accountId;
        qint64 start = 0;
        qint64 end = 0;
    };

    int activeOperationRequests = 0;
    int maximumActiveOperationRequests = 0;
    int operationReadCalls = 0;
    bool allRequestsDisableHttp2 = true;
    bool failOperations = false;
    QVector<OperationRange> operationRanges;
    QHash<QString, int> operationPages;

  protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override {
        REQUIRE(operation == GetOperation);
        allRequestsDisableHttp2 &=
            !request.attribute(QNetworkRequest::Http2AllowedAttribute).toBool();

        QByteArray body;
        int delayMilliseconds = 0;
        if (request.url().path() == QStringLiteral("/api/common/v1/accounts_light_ib")) {
            body = QByteArrayLiteral(
                R"({"resultCode":"OK","payload":[{"id":"account-a","name":"Black","accountType":"Debit","moneyAmount":{"value":"1500.00","currency":{"name":"RUB"}}},{"id":"account-b","name":"Savings","accountType":"Deposit","moneyAmount":{"value":"2500.00","currency":{"name":"RUB"}}}]})");
        } else {
            REQUIRE(request.url().path() == QStringLiteral("/api/common/v1/operations"));
            ++activeOperationRequests;
            maximumActiveOperationRequests =
                std::max(maximumActiveOperationRequests, activeOperationRequests);
            delayMilliseconds = 20;
            const auto timestamp = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
            const QUrlQuery query(request.url());
            const auto account = query.queryItemValue(QStringLiteral("account"));
            operationRanges.push_back(
                {account, query.queryItemValue(QStringLiteral("start")).toLongLong(),
                 query.queryItemValue(QStringLiteral("end")).toLongLong()});
            const auto page = operationPages.value(account);
            operationPages.insert(account, page + 1);
            if (page > 0) {
                body = QByteArrayLiteral(
                    R"({"resultCode":"NO_DATA_FOUND","errorMessage":"No operations"})");
            } else if (account == QStringLiteral("account-a")) {
                body =
                    QStringLiteral(
                        R"({"resultCode":"OK","payload":[{"id":"transfer-out","status":"OK","type":"Debit","isInner":true,"accountAmount":{"value":"-500.00","currency":{"name":"RUB"}},"operationTime":{"milliseconds":%1},"description":"Между своими счетами"},{"id":"expense","status":"OK","type":"Debit","accountAmount":{"value":"-200.00","currency":{"name":"RUB"}},"operationTime":{"milliseconds":%1},"description":"Кофе","spendingCategory":{"name":"Кафе"}},{"id":"bad","status":"OK","type":"Debit","accountAmount":{"value":0,"currency":{"name":"RUB"}}}]})")
                        .arg(timestamp)
                        .toUtf8();
            } else {
                REQUIRE(account == QStringLiteral("account-b"));
                body =
                    QStringLiteral(
                        R"({"resultCode":"OK","payload":[{"id":"transfer-in","status":"OK","type":"Credit","isInner":true,"accountAmount":{"value":"500.00","currency":{"name":"RUB"}},"operationTime":{"milliseconds":%1},"description":"Между своими счетами"},{"id":"income","status":"OK","type":"Credit","accountAmount":{"value":"1000.00","currency":{"name":"RUB"}},"operationTime":{"milliseconds":%1},"description":"Доход"}]})")
                        .arg(timestamp)
                        .toUtf8();
            }
        }

        const bool operationRequest =
            request.url().path() == QStringLiteral("/api/common/v1/operations");
        auto* reply = new BufferedNetworkReply(
            request, failOperations && operationRequest ? QByteArray{} : std::move(body),
            delayMilliseconds,
            failOperations && operationRequest ? QNetworkReply::RemoteHostClosedError
                                               : QNetworkReply::NoError,
            operationRequest ? &operationReadCalls : nullptr, this);
        if (operationRequest) {
            connect(reply, &QNetworkReply::finished, this, [this] { --activeOperationRequests; });
        }
        return reply;
    }
};

bool connectionState(QAbstractItemModel* model, const QString& connectorId) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, ConnectionModel::ConnectedRole).toBool();
        }
    }
    return false;
}
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

bool waitUntil(const std::function<bool()>& predicate,
               const int timeoutMilliseconds = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    }
    return predicate();
}

class ZenSettingsCleanup final {
public:
    ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
    ~ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
};

class TBankSettingsCleanup final {
  public:
    TBankSettingsCleanup() { QSettings{}.remove(QStringLiteral("tbank")); }
    ~TBankSettingsCleanup() { QSettings{}.remove(QStringLiteral("tbank")); }
};

}  // namespace

TEST_CASE("new installations start with empty data and optional demo mode") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(controller.categories()->rowCount() == 0);
    CHECK(controller.holdings()->rowCount() == 0);
    CHECK(controller.netWorth() == QStringLiteral("0 ₽"));
    const auto today = QDate::currentDate();
    CHECK(controller.reportingFrom() ==
          today.addMonths(-1).addDays(1).toString(QStringLiteral("dd.MM.yyyy")));
    CHECK(controller.reportingTo() ==
          today.toString(QStringLiteral("dd.MM.yyyy")));

    controller.loadDemoData();
    CHECK(controller.demoMode());
    CHECK(controller.accounts()->rowCount() > 0);

    controller.clearDemoData();
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
}

TEST_CASE("T-Bank Alfa and VTB are built in while Sber remains an adapter slot") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    const auto tbankAvailable = connectionValue(
        controller.bankConnections(), QStringLiteral("tbank-retail"),
        wealthtracker::app::ConnectionModel::AvailableRole).toBool();
    CHECK(tbankAvailable);
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("tbank-retail")));
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("встроенный коннектор")));
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::StatusRole)
              .toString()
              .contains(QStringLiteral("вход")));
    controller.describeBankConnection(QStringLiteral("tbank-retail"));
    CHECK(controller.connectionMessage().contains(
        QStringLiteral("идентификатор сессии")));
    CHECK(controller.connectionMessage().contains(
        QStringLiteral("C++-коннектор")));

    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("alfa-retail"),
                          wealthtracker::app::ConnectionModel::AvailableRole)
              .toBool());
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("alfa-retail"),
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("встроенный коннектор")));
    controller.describeBankConnection(QStringLiteral("alfa-retail"));
    CHECK(controller.connectionMessage().contains(QStringLiteral("системный WebView")));
    CHECK(controller.connectionMessage().contains(QStringLiteral("не читает её cookies")));

    const auto sber = QStringLiteral("sber-retail");
    CHECK_FALSE(connectionValue(controller.bankConnections(), sber,
                                wealthtracker::app::ConnectionModel::AvailableRole)
                    .toBool());
    CHECK(connectionValue(controller.bankConnections(), sber,
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("Телефон + код")));
    CHECK(connectionValue(controller.bankConnections(), sber,
                          wealthtracker::app::ConnectionModel::StatusRole)
              .toString()
              .contains(QStringLiteral("адаптер")));
}

TEST_CASE("T-Bank login UI embeds a coordinator-owned native WebView") {
    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral("page.app.beginTBankLogin()")));
    CHECK(source.contains(QByteArrayLiteral("WindowContainer {")));
    CHECK(source.contains(QByteArrayLiteral(
        "page.app.tbankWebView")));
    CHECK(source.contains(QByteArrayLiteral(
        "window: connectionDialog.retailWebView")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("sessionid")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("runJavaScript(")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("отдельном Chrome")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("QtWebEngine")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("WebEngineView")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("WebEngineProfile")));

    QFile coordinatorSource(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/tbank_login.cppm"));
    REQUIRE(coordinatorSource.open(QIODevice::ReadOnly));
    const auto coordinator = coordinatorSource.readAll();
    CHECK(coordinator.contains(QByteArrayLiteral(
        "QNetworkRequest::CookieLoadControlAttribute")));
    CHECK(coordinator.contains(QByteArrayLiteral(
        "QNetworkRequest::CookieSaveControlAttribute")));
    CHECK(coordinator.contains(QByteArrayLiteral(
        "QNetworkRequest::Manual")));
    CHECK(coordinator.contains(QByteArrayLiteral(
        "parse_tbank_session_projection")));
    CHECK(coordinator.contains(QByteArrayLiteral("runJavaScript(")));
    CHECK(coordinator.contains(QByteArrayLiteral("LocalStorageEnabled")));
    CHECK(coordinator.contains(QByteArrayLiteral("deleteAllCookies()")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "TBankBrowserRunner")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "QDesktopServices::openUrl")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "RedirectionTargetAttribute")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral("cookieStore()")));
}

TEST_CASE("Alfa login UI projects bounded finance fields without reading cookies") {
    using wealthtracker::app::AlfaProjectionStage;
    using wealthtracker::app::alfa_history_capture_script;
    using wealthtracker::app::alfa_financial_projection_script;
    using wealthtracker::app::parse_alfa_financial_projection;

    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral("page.app.beginAlfaLogin()")));
    CHECK(source.contains(QByteArrayLiteral("page.app.alfaWebView")));
    CHECK(source.contains(QByteArrayLiteral(
        "Т-Банк, Альфа-Банк и ВТБ используют встроенные локальные коннекторы")));
    CHECK_FALSE(source.contains(QByteArrayLiteral(
        "Для Альфа-Банка, Сбера и ВТБ нужен")));

    const auto script = alfa_financial_projection_script();
    CHECK(script.contains(QStringLiteral("web.alfabank.ru")));
    CHECK(script.contains(QStringLiteral("globalThis.initialState")));
    CHECK(script.contains(QStringLiteral("window.runApp(")));
    CHECK(script.contains(QStringLiteral("document?.scripts")));
    CHECK(script.contains(QStringLiteral("layout.allProducts")));
    CHECK(script.contains(QStringLiteral("layout.capitalAccounts")));
    CHECK(script.contains(QStringLiteral("certificate-required")));
    CHECK(script.contains(QStringLiteral("alfa.bank")));
    CHECK(script.contains(QStringLiteral("balanceMinor")));
    CHECK(script.contains(QStringLiteral("internalTransfer")));
    CHECK_FALSE(script.contains(QStringLiteral("document.cookie"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("localStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("innerHTML"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("querySelector"), Qt::CaseInsensitive));
    CHECK(script.contains(QStringLiteral("pageUrl.password !== ''")));
    CHECK_FALSE(script.contains(QStringLiteral("password:"), Qt::CaseInsensitive));

    const auto historyScript = alfa_history_capture_script();
    CHECK(historyScript.contains(QStringLiteral(
        "/api/v1/operations-history/operations")));
    CHECK(historyScript.contains(QStringLiteral("XMLHttpRequest")));
    CHECK(historyScript.contains(QStringLiteral("history.pushState")));
    CHECK(historyScript.contains(QStringLiteral("8 * 1024 * 1024")));
    CHECK_FALSE(historyScript.contains(QStringLiteral("document.cookie"),
                                       Qt::CaseInsensitive));
    CHECK_FALSE(historyScript.contains(QStringLiteral("localStorage"),
                                       Qt::CaseInsensitive));

    const auto waiting = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"authentication","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(waiting);
    CHECK(waiting->stage == AlfaProjectionStage::Authentication);
    CHECK_FALSE(waiting->batch.has_value());

    const auto ready = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"ready","authenticated":true,"historyCaptured":true,"historyComplete":false,"accounts":[{"id":"account-1","name":"Альфа-Карта","kind":"checking","currency":"RUB","maskedNumber":"•••• 1234","balanceMinor":"10000"}],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(ready);
    CHECK(ready->stage == AlfaProjectionStage::Ready);
    CHECK(ready->historyCaptured);
    CHECK_FALSE(ready->historyComplete);
    REQUIRE(ready->batch);
    CHECK(ready->batch->accounts.size() == 1);
    REQUIRE(ready->batch->warnings.size() == 1);
    CHECK(ready->batch->warnings.front().code ==
          QStringLiteral("alfa-history-incomplete"));
}

TEST_CASE("Alfa projection reads the current inline runApp state after hydration") {
    using wealthtracker::app::AlfaProjectionStage;
    using wealthtracker::app::alfa_financial_projection_script;
    using wealthtracker::app::parse_alfa_financial_projection;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'web.alfabank.ru', port: '',
                     username: '', password: '', origin: 'https://web.alfabank.ru',
                     pathname: '/dashboard' }
        }
        globalThis.location = { href: 'https://web.alfabank.ru/dashboard' }
        globalThis.document = { scripts: [{
            src: '',
            textContent: 'window.runApp(' + JSON.stringify({ appState: { layoutData: {
                allProducts: [{
                    id: 'product-inline-1', sectionId: 'deposits', type: 'product',
                    content: {
                        deeplink: '/accounts/product-inline-1',
                        main: {
                            firstLine: { items: [{ type: 'textItem',
                                content: { value: 'Savings' } }] },
                            thirdLine: { items: [{ type: 'amountItem', content: {
                                value: { value: 125050, minorUnits: 100,
                                         currency: 'RUR' }
                            } }] }
                        },
                        rightAddon: { content: { primary: {
                            maskedNumber: '•••• 1234'
                        } } }
                    },
                    properties: { action: { id: 'account-number-1' } }
                }, {
                    id: 'product-inline-2', sectionId: 'accounts', type: 'product',
                    content: { main: {
                        firstLine: { items: [{ type: 'textItem',
                            content: { value: 'Current' } }] },
                        thirdLine: { items: [{ type: 'amountItem', content: {
                            value: { value: 9000, minorUnits: 100, currency: 'RUR' }
                        } }] }
                    } },
                    properties: { action: { id: 'account-number-2' } }
                }]
            } } }) + ')'
        }] }
        globalThis.__wealthtrackerAlfaHistory = { status: 'ready', complete: true, payload: {
            operations: [{
                id: 'operation-inline-1', operationId: 'operation-reference-1',
                dateTime: '2026-08-11T08:30:00Z', direction: 'EXPENSE',
                amount: { value: 5000, minorUnits: 100, currency: 'RUR' },
                title: 'Coffee', category: { name: 'Cafes' },
                actions: [{ accountNumber: 'account-number-1' }]
            }, {
                operationId: 'top-up-reference-1',
                operationDate: '2026-08-12T09:15:00Z',
                operationType: 'INCOME', productId: 'product-inline-2',
                transactionAmount: { sum: 1500.25, currency: 'RUR' },
                title: 'Пополнение'
            }, {
                operationId: 'purchase-reference-2',
                transactionDate: '2026-08-13T10:30:00Z',
                operationDirection: 'EXPENSE', cardId: 'product-inline-1',
                operationAmount: { sum: 199.5, currency: 'RUR' },
                title: 'Покупка'
            }, {
                id: 'cash-withdrawal-reference-1',
                dateTime: '2026-08-14T11:45:00Z', direction: 'EXPENSE',
                amount: { value: 100000, minorUnits: 100, currency: 'RUR' },
                title: 'Снятие наличных', cashout: true,
                sender: { accountNumber: 'account-number-2' },
                terminal: { name: 'Банкомат Альфа-Банка' }
            }]
        } }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(alfa_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto projection = parse_alfa_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(projection);
    CHECK(projection->stage == AlfaProjectionStage::Ready);
    CHECK(projection->historyCaptured);
    CHECK(engine.evaluate(QStringLiteral(
              "globalThis.__wealthtrackerAlfaHistory.status === 'consumed' && "
              "globalThis.__wealthtrackerAlfaHistory.payload === undefined"))
              .toBool());
    REQUIRE(projection->batch);
    REQUIRE(projection->batch->accounts.size() == 2);
    CHECK(projection->batch->accounts.front().externalId ==
          QStringLiteral("product-inline-1"));
    CHECK(projection->batch->balances.front().balance.minorUnits == 125050);
    REQUIRE(projection->batch->transactions.size() == 4);
    CHECK(projection->batch->transactions.front().amount.minorUnits == -5000);
    CHECK(projection->batch->transactions.front().accountExternalId ==
          QStringLiteral("product-inline-1"));
    CHECK(projection->batch->transactions.front().bankCategory ==
          QStringLiteral("Cafes"));
    CHECK(projection->batch->transactions.at(1).amount.minorUnits == 150025);
    CHECK(projection->batch->transactions.at(1).accountExternalId ==
          QStringLiteral("product-inline-2"));
    CHECK(projection->batch->transactions.at(2).amount.minorUnits == -19950);
    CHECK(projection->batch->transactions.at(3).amount.minorUnits == -100000);
    CHECK(projection->batch->transactions.at(3).accountExternalId ==
          QStringLiteral("product-inline-2"));
    CHECK(projection->batch->transactions.at(3).bankCategory ==
          QStringLiteral("Наличные"));
}

TEST_CASE("Alfa history capture requests and exhausts a bounded two-year range") {
    using wealthtracker::app::alfa_history_capture_script;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://web.alfabank.ru/dashboard' }
        globalThis.URL = function(value) {
            const text = String(value)
            this.protocol = 'https:'
            this.hostname = text.includes('evil.example') ? 'evil.example'
                                                           : 'web.alfabank.ru'
            this.port = ''
            this.username = ''
            this.password = ''
            this.origin = 'https://' + this.hostname
            this.pathname = text.includes('/api/v1/operations-history/operations')
                ? '/api/v1/operations-history/operations' : '/dashboard'
        }
        globalThis.XMLHttpRequest = function() { this.listeners = {} }
        XMLHttpRequest.prototype.addEventListener = function(name, callback) {
            this.listeners[name] = callback
        }
        XMLHttpRequest.prototype.open = function() {}
        XMLHttpRequest.prototype.setRequestHeader = function(name, value) {
            this.requestHeader = name + ':' + value
        }
        XMLHttpRequest.prototype.send = function(body) {
            this.nativeBody = body
            if (typeof body !== 'string') return
            globalThis.sentAlfaBodies.push(body)
            const payload = JSON.parse(body)
            if (payload.page === 1) {
                this.status = 200
                this.responseText = '{"operations":[{"id":"last-page"}]}'
                this.listeners.loadend()
            }
        }
        globalThis.sentAlfaBodies = []
        globalThis.pendingTimers = []
        globalThis.setTimeout = function(callback) {
            globalThis.pendingTimers.push(callback)
            return globalThis.pendingTimers.length
        }
        globalThis.originalOpen = XMLHttpRequest.prototype.open
        globalThis.originalSend = XMLHttpRequest.prototype.send
        globalThis.history = { pushState: function(_, __, path) {
            globalThis.openedPath = path
        } }
        globalThis.PopStateEvent = function() {}
        globalThis.dispatchEvent = function() {}
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto installed = engine.evaluate(alfa_history_capture_script());
    REQUIRE_FALSE(installed.isError());
    CHECK(installed.toString() == QStringLiteral("waiting"));
    CHECK(engine.evaluate(QStringLiteral("globalThis.openedPath")).toString() ==
          QStringLiteral("/history"));

    const auto ignored = engine.evaluate(QStringLiteral(R"JS((() => {
        const request = new XMLHttpRequest()
        request.addEventListener = function(_, callback) { this.callback = callback }
        request.open('GET', '/api/v1/operations-history/operations')
        request.send()
        return request.callback === undefined &&
               globalThis.__wealthtrackerAlfaHistory.status === 'waiting'
    })())JS"));
    REQUIRE_FALSE(ignored.isError());
    CHECK(ignored.toBool());

    const auto completed = engine.evaluate(QStringLiteral(R"JS((() => {
        const request = new XMLHttpRequest()
        request.open('POST', '/api/v1/operations-history/operations')
        request.status = 200
        request.responseText = JSON.stringify({ operations: Array.from(
            { length: 100 }, function(_, index) { return { id: 'first-' + index } }) })
        request.send(JSON.stringify({
            page: 0,
            pageSize: 6,
            filters: { size: 6, dateFrom: '25.07.2026', dateTo: '25.08.2026' },
            request: { pagination: { size: '6' } }
        }))
        request.listeners.loadend()
        while (globalThis.pendingTimers.length)
            globalThis.pendingTimers.shift()()
        const firstBody = JSON.parse(globalThis.sentAlfaBodies[0])
        const secondBody = JSON.parse(globalThis.sentAlfaBodies[1])
        return globalThis.__wealthtrackerAlfaHistory.status === 'ready' &&
               globalThis.__wealthtrackerAlfaHistory.payload.responses.length === 2 &&
               globalThis.__wealthtrackerAlfaHistory.complete === true &&
               globalThis.sentAlfaBodies.length === 2 &&
               firstBody.page === 0 && secondBody.page === 1 &&
               firstBody.pageSize === 100 && firstBody.filters.size === 6 &&
               firstBody.request.pagination.size === '100' &&
               firstBody.filters.dateFrom !== '25.07.2026' &&
               /^\d{2}\.\d{2}\.\d{4}$/.test(firstBody.filters.dateFrom) &&
               /^\d{2}\.\d{2}\.\d{4}$/.test(firstBody.filters.dateTo) &&
               XMLHttpRequest.prototype.open === globalThis.originalOpen &&
               XMLHttpRequest.prototype.send === globalThis.originalSend
    })())JS"));
    REQUIRE_FALSE(completed.isError());
    INFO(completed.toString().toStdString());
    INFO(engine.evaluate(QStringLiteral(
             "JSON.stringify(globalThis.__wealthtrackerAlfaHistory)"))
             .toString()
             .toStdString());
    INFO(engine.evaluate(QStringLiteral(
             "JSON.stringify(globalThis.sentAlfaBodies)"))
             .toString()
             .toStdString());
    CHECK(completed.toBool());
    CHECK(engine.evaluate(QStringLiteral(
              "JSON.parse(globalThis.sentAlfaBodies[0]).filters.dateFrom"))
              .toString() ==
          QDate::currentDate().addYears(-2).toString(QStringLiteral("dd.MM.yyyy")));
    CHECK(engine.evaluate(QStringLiteral(
              "JSON.parse(globalThis.sentAlfaBodies[0]).filters.dateTo"))
              .toString() ==
          QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy")));

    REQUIRE_FALSE(engine.evaluate(QStringLiteral(
        "delete globalThis.__wealthtrackerAlfaHistory; "
        "globalThis.sentAlfaBodies = []; globalThis.pendingTimers = []"))
                      .isError());
    const auto restarted = engine.evaluate(alfa_history_capture_script());
    REQUIRE_FALSE(restarted.isError());
    REQUIRE(restarted.toString() == QStringLiteral("waiting"));
    const auto missingRange = engine.evaluate(QStringLiteral(R"JS((() => {
        const request = new XMLHttpRequest()
        request.open('POST', '/api/v1/operations-history/operations')
        request.status = 200
        request.responseText = '{"operations":[{"id":"one-page"}]}'
        request.send('{"page":0,"pageSize":100}')
        request.listeners.loadend()
        while (globalThis.pendingTimers.length)
            globalThis.pendingTimers.shift()()
        return globalThis.__wealthtrackerAlfaHistory.status === 'ready' &&
               globalThis.__wealthtrackerAlfaHistory.complete === false
    })())JS"));
    REQUIRE_FALSE(missingRange.isError());
    CHECK(missingRange.toBool());
}

TEST_CASE("Alfa projection reports post-login loading instead of waiting silently") {
    using wealthtracker::app::AlfaProjectionStage;
    using wealthtracker::app::parse_alfa_financial_projection;
    using wealthtracker::app::should_start_alfa_history_capture;

    CHECK(should_start_alfa_history_capture(false, 6));
    CHECK_FALSE(should_start_alfa_history_capture(true, 6));

    const auto loading = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"dashboard-loading","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(loading);
    CHECK(loading->stage == AlfaProjectionStage::DashboardLoading);
    CHECK_FALSE(loading->batch);

    const auto certificateRequired = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"certificate-required","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(certificateRequired);
    CHECK(certificateRequired->stage == AlfaProjectionStage::CertificateRequired);

    const auto operationsLoading = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"operations-loading","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(operationsLoading);
    CHECK(operationsLoading->stage == AlfaProjectionStage::OperationsLoading);

    const auto operationsFailed = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"operations-failed","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(operationsFailed);
    CHECK(operationsFailed->stage == AlfaProjectionStage::OperationsFailed);

    const auto unknown = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"unknown","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE_FALSE(unknown);

    QFile coordinator(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/alfa_login.cppm"));
    REQUIRE(coordinator.open(QIODevice::ReadOnly));
    const auto coordinatorSource = coordinator.readAll();
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "Вход выполнен. Получаем счета из Альфа-Онлайн")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "dashboardStateTimeoutMilliseconds = 30 * 1'000")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "operationsStateTimeoutMilliseconds = 150 * 1'000")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "alfa_history_capture_script()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral(
        "projection->batch->transactions.isEmpty()")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "Альфа-Онлайн открыл страницу установки российских")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "certificate_help_required_ = true")));
}

TEST_CASE("Alfa projection recognizes the bank certificate landing page") {
    using wealthtracker::app::AlfaProjectionStage;
    using wealthtracker::app::alfa_financial_projection_script;
    using wealthtracker::app::parse_alfa_financial_projection;

    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://web.alfabank.ru/' }
        globalThis.URL = function(value) {
            const text = String(value)
            const certificate = text.includes('alfa.bank/cert')
            this.protocol = 'https:'
            this.hostname = certificate ? 'alfa.bank' : 'web.alfabank.ru'
            this.port = ''
            this.username = ''
            this.password = ''
            this.origin = 'https://' + this.hostname
            this.pathname = certificate ? '/cert' : '/'
        }
        globalThis.document = {
            scripts: [],
            forms: [],
            links: [{ href: 'https://alfa.bank/cert' },
                    { href: 'https://web.alfabank.ru/' }]
        }
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(alfa_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto projection = parse_alfa_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(projection);
    CHECK(projection->stage == AlfaProjectionStage::CertificateRequired);
    CHECK_FALSE(projection->batch);
}

TEST_CASE("Alfa WebView failures distinguish certificate trust without leaking URLs") {
    using wealthtracker::app::AlfaLoadFailureKind;
    using wealthtracker::app::classify_alfa_load_failure;

    const auto certificateFailure = classify_alfa_load_failure(
        QUrl{QStringLiteral(
            "https://web.alfabank.ru/login?authorization=secret-value")},
        QStringLiteral("The certificate for this server is invalid (-1202)"));
    CHECK(certificateFailure.kind == AlfaLoadFailureKind::CertificateTrust);
    CHECK(certificateFailure.certificateHelpRequired);
    CHECK(certificateFailure.message.contains(QStringLiteral("web.alfabank.ru")));
    CHECK(certificateFailure.message.contains(QStringLiteral("сертификат"),
                                              Qt::CaseInsensitive));
    CHECK_FALSE(certificateFailure.message.contains(QStringLiteral("authorization")));
    CHECK_FALSE(certificateFailure.message.contains(QStringLiteral("secret-value")));

    const auto networkFailure = classify_alfa_load_failure(
        QUrl{QStringLiteral("https://private.auth.alfabank.ru/start?phone=secret")},
        QStringLiteral("The Internet connection appears to be offline"));
    CHECK(networkFailure.kind == AlfaLoadFailureKind::Network);
    CHECK_FALSE(networkFailure.certificateHelpRequired);
    CHECK(networkFailure.message.contains(
        QStringLiteral("private.auth.alfabank.ru")));
    CHECK_FALSE(networkFailure.message.contains(QStringLiteral("phone")));
    CHECK_FALSE(networkFailure.message.contains(QStringLiteral("secret")));

    const auto untrustedHost = classify_alfa_load_failure(
        QUrl{QStringLiteral("https://attacker.example/capture?code=secret")},
        QStringLiteral("invalid certificate"));
    CHECK_FALSE(untrustedHost.message.contains(QStringLiteral("attacker.example")));
    CHECK_FALSE(untrustedHost.message.contains(QStringLiteral("code")));
    CHECK_FALSE(untrustedHost.message.contains(QStringLiteral("secret")));
}

TEST_CASE("Alfa certificate help uses only the official bank instructions") {
    using wealthtracker::app::alfa_certificate_instructions_url;

    const auto helpUrl = alfa_certificate_instructions_url();
    CHECK(helpUrl.scheme() == QStringLiteral("https"));
    CHECK(helpUrl.host() == QStringLiteral("alfa.bank"));
    CHECK(helpUrl.path() == QStringLiteral("/cert"));
    CHECK(helpUrl.query().isEmpty());

    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral("alfaCertificateHelpRequired")));
    CHECK(source.contains(
        QByteArrayLiteral("page.app.openAlfaCertificateInstructions()")));
}

TEST_CASE("T-Bank API session identifiers reject unsafe values") {
    using wealthtracker::connectors::isValidTBankRetailSessionId;
    CHECK(isValidTBankRetailSessionId("observed-session-123456"));
    CHECK_FALSE(isValidTBankRetailSessionId("session;injected-value"));
    CHECK_FALSE(isValidTBankRetailSessionId("session?query-injected"));
    CHECK_FALSE(isValidTBankRetailSessionId("session&query-injected"));
    CHECK_FALSE(isValidTBankRetailSessionId("session\nheader-value"));
    CHECK_FALSE(isValidTBankRetailSessionId("too-short"));
}

TEST_CASE("partial bank refresh retains distinct history inside the two-year window") {
    using wealthtracker::app::retainPreviousTransactionsForPartialHistory;
    using wealthtracker::connectors::ExternalAccount;
    using wealthtracker::connectors::Money;
    using wealthtracker::connectors::NormalizedBatch;
    using wealthtracker::connectors::NormalizedTransaction;

    NormalizedBatch current;
    ExternalAccount account;
    account.externalId = QStringLiteral("account-1");
    current.accounts.push_back(account);

    NormalizedTransaction refreshed;
    refreshed.externalId = QStringLiteral("shared");
    refreshed.accountExternalId = account.externalId;
    refreshed.postingDate = QDate{2026, 8, 20};
    refreshed.amount = Money{-2'000, QStringLiteral("RUB")};
    current.transactions.push_back(refreshed);

    NormalizedBatch previous;
    auto duplicate = refreshed;
    duplicate.amount = Money{-1'000, QStringLiteral("RUB")};
    previous.transactions.push_back(duplicate);
    auto retained = refreshed;
    retained.externalId = QStringLiteral("older");
    retained.postingDate = QDate{2025, 2, 10};
    previous.transactions.push_back(retained);
    auto expired = refreshed;
    expired.externalId = QStringLiteral("expired");
    expired.postingDate = QDate{2023, 8, 20};
    previous.transactions.push_back(expired);
    auto removedAccount = refreshed;
    removedAccount.externalId = QStringLiteral("removed-account");
    removedAccount.accountExternalId = QStringLiteral("account-2");
    previous.transactions.push_back(removedAccount);

    CHECK(retainPreviousTransactionsForPartialHistory(
              current, previous, QDate{2024, 8, 26}) == 1);
    REQUIRE(current.transactions.size() == 2);
    CHECK(current.transactions.front().amount.minorUnits == -2'000);
    CHECK(current.transactions.back().externalId == QStringLiteral("older"));
}

TEST_CASE("T-Bank synchronization publishes balances and excludes internal transfers") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("connected-session-123456789"));
    auto network = std::make_unique<TBankNetworkFixture>();
    auto* observedNetwork = network.get();
    wealthtracker::app::AppController controller(std::move(credentials), std::move(network),
                                                 nullptr);

    REQUIRE(controller.tbankConnected());
    const auto earliestSyncEnd = QDateTime::currentDateTimeUtc();
    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&controller] { return !controller.tbankSyncRunning(); }));
    const auto latestSyncEnd = QDateTime::currentDateTimeUtc();

    CHECK(observedNetwork->allRequestsDisableHttp2);
    CHECK(observedNetwork->maximumActiveOperationRequests == 1);
    REQUIRE(observedNetwork->operationRanges.size() == 48);
    QHash<QString, QVector<TBankNetworkFixture::OperationRange>> rangesByAccount;
    for (const auto& range : observedNetwork->operationRanges) {
        rangesByAccount[range.accountId].push_back(range);
    }
    REQUIRE(rangesByAccount.size() == 2);
    REQUIRE(rangesByAccount.value(QStringLiteral("account-a")).size() == 24);
    REQUIRE(rangesByAccount.value(QStringLiteral("account-b")).size() == 24);
    const auto accountARanges = rangesByAccount.value(QStringLiteral("account-a"));
    const auto accountBRanges = rangesByAccount.value(QStringLiteral("account-b"));
    for (qsizetype index = 0; index < accountARanges.size(); ++index) {
        CHECK(accountARanges.at(index).start == accountBRanges.at(index).start);
        CHECK(accountARanges.at(index).end == accountBRanges.at(index).end);
        if (index > 0) {
            CHECK(accountARanges.at(index).start == accountARanges.at(index - 1).end + 1);
        }
    }
    const auto historyStartMilliseconds = accountARanges.front().start;
    const auto historyEndMilliseconds = accountARanges.back().end;
    const auto historyEnd =
        QDateTime::fromMSecsSinceEpoch(historyEndMilliseconds, QTimeZone::UTC);
    CHECK(QDateTime::fromMSecsSinceEpoch(historyStartMilliseconds, QTimeZone::UTC) ==
          QDateTime(historyEnd.date().addYears(-2), QTime(0, 0), QTimeZone::UTC));
    CHECK(historyEnd >= earliestSyncEnd);
    CHECK(historyEnd <= latestSyncEnd);
    CHECK(controller.accounts()->rowCount() == 2);
    CHECK(controller.transactions()->rowCount() == 4);
    CHECK(controller.reportIncome() == QStringLiteral("1 000 ₽"));
    CHECK(controller.reportSpending() == QStringLiteral("200 ₽"));
    CHECK(controller.savings() == QStringLiteral("800 ₽"));
    CHECK(controller.categories()->rowCount() == 1);
    CHECK(controller.categories()
              ->data(controller.categories()->index(0, 0),
                     wealthtracker::app::CategoryModel::NameRole)
              .toString() == QStringLiteral("Кафе"));

    int transferRows = 0;
    for (int row = 0; row < controller.transactions()->rowCount(); ++row) {
        if (controller.transactions()
                ->data(controller.transactions()->index(row, 0),
                       wealthtracker::app::TransactionModel::CategoryRole)
                .toString() == QStringLiteral("Внутренний перевод")) {
            CHECK(controller.transactions()
                      ->data(controller.transactions()->index(row, 0),
                             wealthtracker::app::TransactionModel::TransferRole)
                      .toBool());
            ++transferRows;
        }
    }
    CHECK(transferRows == 2);
    CHECK(controller.syncStatus().contains(QStringLiteral("пропущено записей 1")));

    CHECK_FALSE(controller.setReportingPeriod(
        QStringLiteral("31.02.2026"), QStringLiteral("01.03.2026")));
    CHECK_FALSE(controller.reportingPeriodError().isEmpty());
    CHECK(controller.transactions()->rowCount() == 4);

    REQUIRE(controller.setReportingPeriod(
        QStringLiteral("01.01.2020"), QStringLiteral("31.01.2020")));
    CHECK(controller.reportingPeriodError().isEmpty());
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(controller.reportIncome() == QStringLiteral("0 ₽"));
    CHECK(controller.reportSpending() == QStringLiteral("0 ₽"));
    CHECK(controller.savings() == QStringLiteral("0 ₽"));

    controller.resetReportingPeriod();
    CHECK(controller.transactions()->rowCount() == 4);
    CHECK(controller.reportIncome() == QStringLiteral("1 000 ₽"));
}

TEST_CASE("T-Bank network failures do not read a closed TLS reply") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("connected-session-123456789"));
    auto network = std::make_unique<TBankNetworkFixture>();
    network->failOperations = true;
    auto* observedNetwork = network.get();
    wealthtracker::app::AppController controller(std::move(credentials), std::move(network),
                                                 nullptr);

    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&controller] { return !controller.tbankSyncRunning(); }));

    CHECK(observedNetwork->operationReadCalls == 0);
    CHECK(controller.syncStatus().contains(QStringLiteral("банк закрыл соединение")));
    CHECK(controller.accounts()->rowCount() == 0);
}

TEST_CASE("T-Bank WebView projection is bounded before API validation") {
    using wealthtracker::app::TBankAccessLevel;
    using wealthtracker::app::parse_tbank_access_level;
    using wealthtracker::app::parse_tbank_session_projection;
    using wealthtracker::app::tbank_session_probe_script;
    using wealthtracker::app::tbank_ping_url;

    const QByteArray sessionId{
        "0123456789abcdef0123456789abcdef.auth-session-host"};
    const auto sessions = parse_tbank_session_projection(
        QStringLiteral("[\"%1\"]").arg(QString::fromLatin1(sessionId)));
    REQUIRE(sessions);
    REQUIRE(sessions->size() == 1);
    CHECK(sessions->front() == sessionId);

    const auto empty = parse_tbank_session_projection(QStringLiteral("[]"));
    REQUIRE(empty);
    CHECK(empty->isEmpty());

    const auto probe = tbank_session_probe_script();
    CHECK(probe.contains(QStringLiteral(
        "performance.getEntriesByType('resource')")));
    CHECK(probe.contains(QStringLiteral("www.tbank.ru")));
    CHECK(probe.contains(QStringLiteral("/api/common/v1/")));
    CHECK_FALSE(probe.contains(QStringLiteral("document.")));
    CHECK_FALSE(probe.contains(QStringLiteral("cookie"),
                               Qt::CaseInsensitive));
    CHECK_FALSE(probe.contains(QStringLiteral("localStorage")));

    const auto pingUrl = tbank_ping_url(sessionId);
    REQUIRE(pingUrl);
    CHECK(pingUrl->path() == QStringLiteral("/api/common/v1/ping"));
    CHECK(QUrlQuery(*pingUrl).queryItemValue(QStringLiteral("sessionid"))
          == QString::fromLatin1(sessionId));

    const auto anonymous = parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"ANONYMOUS\"}}"));
    REQUIRE(anonymous);
    CHECK(*anonymous == TBankAccessLevel::anonymous);
    const auto client = parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"CLIENT\"}}"));
    REQUIRE(client);
    CHECK(*client == TBankAccessLevel::client);

    CHECK_FALSE(parse_tbank_session_projection(QStringLiteral(
        "[\"bad;session\"]")));
    CHECK_FALSE(parse_tbank_session_projection(QStringLiteral("{}")));
    CHECK_FALSE(parse_tbank_session_projection(QStringLiteral("[1]")));
    CHECK_FALSE(parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"ADMIN\"}}")));
    CHECK_FALSE(parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"ERROR\"}")));
}

TEST_CASE("official bank tokens have independent credential state") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    REQUIRE(controller.saveBankToken(QStringLiteral("alfa-api"),
                                     QStringLiteral("alfa-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("tbank-business"),
                                     QStringLiteral("tbank-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("modulbank"),
                                     QStringLiteral("sandboxtoken")));

    CHECK(stored->values.value(QStringLiteral("alfa-b2c-readonly")) ==
          QByteArrayLiteral("alfa-token-valid"));
    CHECK(stored->values.value(QStringLiteral("tbank-business-readonly")) ==
          QByteArrayLiteral("tbank-token-valid"));
    CHECK(stored->values.value(QStringLiteral("modulbank-readonly")) ==
          QByteArrayLiteral("sandboxtoken"));
    CHECK_FALSE(controller.syncStatus().contains(
        QStringLiteral("sandboxtoken")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("tbank-business")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));

    controller.removeBankToken(QStringLiteral("alfa-api"));
    CHECK_FALSE(
        stored->values.contains(QStringLiteral("alfa-b2c-readonly")));
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));
}

TEST_CASE("application runs a selected local bundle through the embedded host") {
    if (QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty()) {
        SKIP("Node.js is not installed");
    }
    const ZenSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);
    REQUIRE(controller.setReportingPeriod(
        QStringLiteral("01.01.2020"), QStringLiteral("31.12.2030")));
    const auto baselineCredentialCount = stored->values.size();
    const auto fixture = QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR
        "/tests/fixtures/zen_plugin_fixture.cjs");

    REQUIRE(controller.inspectZenPluginBundle(
        QUrl::fromLocalFile(fixture)));
    CHECK(controller.zenPluginId()
          == QStringLiteral("zen_plugin_fixture"));
    controller.setZenPluginAutoUpdate(true);
    CHECK(controller.zenPluginAutoUpdate());
    CHECK(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));
    REQUIRE(controller.startZenPlugin(
        QStringLiteral("Fixture Bank"),
        {{QStringLiteral("login"), QStringLiteral("fixture")},
         {QStringLiteral("password"),
          QStringLiteral("session-only-secret")},
         {QStringLiteral("startDate"),
          QStringLiteral("2026-07-01T00:00:00.000Z")}}));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginPrompt().isEmpty();
    }));
    REQUIRE(controller.answerZenPluginPrompt(
        QStringLiteral("123456")));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginRunning();
    }));
    CHECK(controller.accounts()->rowCount() == 1);
    CHECK(controller.transactions()->rowCount() == 1);
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.syncStatus().contains(
        QStringLiteral("Fixture Bank")));
    CHECK(stored->values.size() == baselineCredentialCount + 1);
    CHECK(controller.zenPluginLastSync() !=
          QStringLiteral("Нет успешного обновления"));
    CHECK_FALSE(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));

    const auto savedPreferences =
        QSettings{}.value(QStringLiteral("zenPlugin/preferences"))
            .toByteArray();
    CHECK(savedPreferences.contains("fixture"));
    CHECK(savedPreferences.contains("startDate"));
    CHECK_FALSE(savedPreferences.contains("session-only-secret"));
    CHECK_FALSE(savedPreferences.contains("password"));
    CHECK_FALSE(controller.synchronizeZenPlugin());
    CHECK(controller.syncStatus().contains(QStringLiteral("Пароль")));

    controller.removeZenPlugin();
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(stored->values.size() == baselineCredentialCount);
    CHECK(controller.zenPluginId().isEmpty());
    CHECK_FALSE(controller.zenPluginAutoUpdate());
}

TEST_CASE("bank connection state rejects malformed and unknown tokens") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("modulbank"), QStringLiteral("short")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("alfa-api"), QStringLiteral("token with spaces")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("unknown-bank"), QStringLiteral("valid-token")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("modulbank-readonly")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("alfa-b2c-readonly")));
}
