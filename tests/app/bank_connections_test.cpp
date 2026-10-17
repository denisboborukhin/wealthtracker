#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
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
import wt.tbank_login;
import wt.alfa_browser;
import wt.alfa_login;
import wt.vtb_login;
import wt.vtb_browser;
import wt.bank_sync;
import wt.connectors;

using wealthtracker::app::is_alfa_login_url;
using wealthtracker::app::is_tbank_login_url;
using wealthtracker::app::is_tbank_login_navigation_url;
using wealthtracker::app::is_vtb_login_url;

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
    int unexpectedRequests = 0;
    int readCalls = 0;
    bool allRequestsDisableHttp2 = true;
    bool failAccounts = false;
    bool rejectAuthentication = false;
    bool overflowBalances = false;

  protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override {
        REQUIRE(operation == GetOperation);
        allRequestsDisableHttp2 &=
            !request.attribute(QNetworkRequest::Http2AllowedAttribute).toBool();

        QByteArray body;
        if (request.url().path() == QStringLiteral("/api/common/v1/accounts_light_ib")) {
            body = rejectAuthentication
                       ? QByteArrayLiteral(R"({"resultCode":"AUTHENTICATION_FAILED"})")
                       : overflowBalances
                       ? QByteArrayLiteral(
                             R"({"resultCode":"OK","payload":[{"id":"account-a","name":"Black","accountType":"Debit","moneyAmount":{"value":"92233720368547758.07","currency":{"name":"RUB"}}},{"id":"account-b","name":"Savings","accountType":"Deposit","moneyAmount":{"value":"2500.00","currency":{"name":"RUB"}}}]})")
                       : QByteArrayLiteral(
                             R"({"resultCode":"OK","payload":[{"id":"account-a","name":"Black","accountType":"Debit","moneyAmount":{"value":"1500.00","currency":{"name":"RUB"}}},{"id":"account-b","name":"Savings","accountType":"Deposit","moneyAmount":{"value":"2500.00","currency":{"name":"RUB"}}}]})");
        } else {
            ++unexpectedRequests;
            FAIL_CHECK("Retail T-Bank requested an unexpected route");
        }
        return new BufferedNetworkReply(
            request, std::move(body), 0,
            failAccounts ? QNetworkReply::RemoteHostClosedError : QNetworkReply::NoError,
            &readCalls, this);
    }
};

class InvestmentNetworkFixture final : public QNetworkAccessManager {
public:
    int requests = 0;
    int portfolioRequests = 0;
    int unexpectedRequests = 0;
    int delay = 0;
    bool failPortfolio = false;

protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override {
        REQUIRE(operation == PostOperation);
        ++requests;
        const auto body = QJsonDocument::fromJson(outgoingData->readAll()).object();
        QByteArray response;
        auto error = QNetworkReply::NoError;
        if (request.url().path().endsWith(QStringLiteral("/GetAccounts"))) {
            response = R"({"accounts":[{"id":"invest-a","name":"Brokerage","type":"ACCOUNT_TYPE_TINKOFF","status":"ACCOUNT_STATUS_OPEN"}]})";
        } else if (request.url().path().endsWith(QStringLiteral("/GetPortfolio"))) {
            ++portfolioRequests;
            REQUIRE(body.value(QStringLiteral("accountId")).toString() == QStringLiteral("invest-a"));
            response = R"({"totalAmountPortfolio":{"currency":"rub","units":"120","nano":500000000},"positions":[{"instrumentUid":"instrument-a","quantity":{"units":"2"},"currentPrice":{"currency":"rub","units":"60","nano":250000000}}]})";
            if (failPortfolio) error = QNetworkReply::RemoteHostClosedError;
        } else {
            ++unexpectedRequests;
            FAIL_CHECK("T-Invest requested an unexpected route");
            error = QNetworkReply::ContentOperationNotPermittedError;
        }
        return new BufferedNetworkReply(request, response, delay, error, nullptr, this);
    }
};

class FailedInvestmentNetworkFixture final : public QNetworkAccessManager {
public:
    int readCalls = 0;

protected:
    QNetworkReply* createRequest(const Operation operation,
                                 const QNetworkRequest& request,
                                 QIODevice*) override {
        REQUIRE(operation == PostOperation);
        return new BufferedNetworkReply(
            request, {}, 0, QNetworkReply::RemoteHostClosedError, &readCalls, this);
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
        "? connectionDialog.retailWebView : null")));
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
    CHECK(coordinator.contains(QByteArrayLiteral("clearBankBrowserData")));
    CHECK(coordinator.contains(QByteArrayLiteral("view->deleteLater()")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral("about:blank")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "TBankBrowserRunner")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "QDesktopServices::openUrl")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral(
        "RedirectionTargetAttribute")));
    CHECK_FALSE(coordinator.contains(QByteArrayLiteral("cookieStore()")));
    const auto stopOffset = coordinator.indexOf(QByteArrayLiteral(
        "void stop(const bool invalidateGeneration, const bool cleanupWebView)"));
    const auto stopProbeOffset = coordinator.indexOf(
        QByteArrayLiteral("probe_timer_.stop();"), stopOffset);
    const auto cleanupOffset = coordinator.indexOf(
        QByteArrayLiteral("if (cleanupWebView)"), stopOffset);
    REQUIRE(stopOffset >= 0);
    REQUIRE(stopProbeOffset >= 0);
    REQUIRE(cleanupOffset >= 0);
    CHECK(stopProbeOffset < cleanupOffset);

    QFile controllerSource(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/AppController.cpp"));
    REQUIRE(controllerSource.open(QIODevice::ReadOnly));
    const auto controller = controllerSource.readAll();
    const auto beginOffset = controller.indexOf(
        QByteArrayLiteral("void AppController::beginTBankLogin()"));
    const auto activeGuardOffset = controller.indexOf(
        QByteArrayLiteral("if (tbank_login_active_)"), beginOffset);
    const auto startOffset = controller.indexOf(
        QByteArrayLiteral("private_->tbank_login->start"), beginOffset);
    REQUIRE(beginOffset >= 0);
    REQUIRE(activeGuardOffset >= 0);
    REQUIRE(startOffset >= 0);
    CHECK(activeGuardOffset < startOffset);
}

TEST_CASE("Alfa login UI projects bounded finance fields without reading cookies") {
    using wealthtracker::app::AlfaProjectionStage;
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
    CHECK_FALSE(script.contains(QStringLiteral("document.cookie"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("localStorage"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("innerHTML"), Qt::CaseInsensitive));
    CHECK_FALSE(script.contains(QStringLiteral("querySelector"), Qt::CaseInsensitive));
    CHECK(script.contains(QStringLiteral("pageUrl.password !== ''")));
    CHECK_FALSE(script.contains(QStringLiteral("password:"), Qt::CaseInsensitive));

    const auto waiting = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"authentication","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(waiting);
    CHECK(waiting->stage == AlfaProjectionStage::Authentication);
    CHECK_FALSE(waiting->batch.has_value());

    const auto ready = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"ready","authenticated":true,"accounts":[{"id":"account-1","name":"Альфа-Карта","kind":"checking","currency":"RUB","maskedNumber":"•••• 1234","balanceMinor":"10000"}],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    REQUIRE(ready);
    CHECK(ready->stage == AlfaProjectionStage::Ready);
    REQUIRE(ready->batch);
    CHECK(ready->batch->accounts.size() == 1);
    CHECK(ready->batch->warnings.isEmpty());
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
    )JS"));
    REQUIRE_FALSE(setup.isError());

    const auto evaluated = engine.evaluate(alfa_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto projection = parse_alfa_financial_projection(
        evaluated.toString(), QDateTime::currentDateTimeUtc());
    REQUIRE(projection);
    CHECK(projection->stage == AlfaProjectionStage::Ready);
    REQUIRE(projection->batch);
    REQUIRE(projection->batch->accounts.size() == 2);
    CHECK(projection->batch->accounts.front().externalId ==
          QStringLiteral("product-inline-1"));
    CHECK(projection->batch->balances.front().balance.minorUnits == 125050);
    CHECK(projection->batch->balances.at(1).balance.minorUnits == 9000);
    CHECK(projection->batch->transactions.isEmpty());
}

TEST_CASE("Alfa projection reports post-login loading instead of waiting silently") {
    using wealthtracker::app::AlfaProjectionStage;
    using wealthtracker::app::parse_alfa_financial_projection;

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
    CHECK_FALSE(operationsLoading);

    const auto operationsFailed = parse_alfa_financial_projection(
        QStringLiteral(
            R"({"version":1,"stage":"operations-failed","authenticated":false,"accounts":[],"transactions":[]})"),
        QDateTime::currentDateTimeUtc());
    CHECK_FALSE(operationsFailed);

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
        "synchronizationTimeoutMilliseconds = 45 * 1'000")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral(
        "alfa_history_capture_script()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral(
        "projection->batch->transactions.isEmpty()")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "Альфа-Онлайн открыл страницу установки российских")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "certificate_help_required_ = true")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral(
        "QTimer::singleShot(0, view")));
    CHECK(coordinatorSource.contains(QByteArrayLiteral("view->deleteLater()")));
    CHECK_FALSE(coordinatorSource.contains(QByteArrayLiteral("about:blank")));
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

TEST_CASE("embedded bank login accepts only exact official HTTPS origins") {
    CHECK(is_tbank_login_url(QUrl(QStringLiteral("https://www.tbank.ru/login/"))));
    CHECK_FALSE(is_tbank_login_url(
        QUrl(QStringLiteral("https://id.tbank.ru/auth/authorize"))));
    CHECK(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://www.tbank.ru/login/"))));
    CHECK(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://id.tbank.ru/auth/authorize"))));
    CHECK(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://id.tinkoff.ru/auth/authorize"))));
    CHECK(is_alfa_login_url(QUrl(QStringLiteral("https://web.alfabank.ru/dashboard"))));
    CHECK(is_alfa_login_url(QUrl(QStringLiteral("https://private.auth.alfabank.ru/"))));
    CHECK(is_vtb_login_url(QUrl(QStringLiteral("https://online.vtb.ru/"))));

    CHECK_FALSE(is_tbank_login_url(QUrl(QStringLiteral("https://tbank.ru/login/"))));
    CHECK_FALSE(is_tbank_login_url(
        QUrl(QStringLiteral("https://id.tbank.ru.evil.test/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://id.tinkoff.ru.evil.test/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://login.id.tinkoff.ru/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("http://id.tinkoff.ru/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://sso-forms-prod.t-static.ru/"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://cdn-tinkoff.ru/"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://user@id.tinkoff.ru/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_navigation_url(
        QUrl(QStringLiteral("https://id.tinkoff.ru:444/auth/authorize"))));
    CHECK_FALSE(is_tbank_login_url(
        QUrl(QStringLiteral("https://user@www.tbank.ru/login/"))));
    CHECK_FALSE(is_alfa_login_url(QUrl(QStringLiteral("http://web.alfabank.ru/"))));
    CHECK_FALSE(is_alfa_login_url(QUrl(QStringLiteral("https://web.alfabank.ru:444/"))));
    CHECK_FALSE(is_vtb_login_url(QUrl(QStringLiteral("https://online.vtb.ru.evil.test/"))));
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

TEST_CASE("T-Bank balances synchronize without any history requests") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("connected-session-123456789"));
    auto network = std::make_unique<TBankNetworkFixture>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(std::move(credentials), std::move(network), nullptr);
    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&] { return !controller.tbankSyncRunning(); }));
    CHECK(controller.accounts()->rowCount() == 2);
    CHECK(controller.netWorth() == QStringLiteral("4 000 ₽"));
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(observed->unexpectedRequests == 0);
    CHECK(observed->allRequestsDisableHttp2);
    CHECK(controller.syncStatus().contains(QStringLiteral("Остатки"), Qt::CaseInsensitive));

    observed->failAccounts = true;
    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&] { return !controller.tbankSyncRunning(); }));
    CHECK(controller.accounts()->rowCount() == 2);
    CHECK(controller.netWorth() == QStringLiteral("4 000 ₽"));
    CHECK(controller.tbankConnected());
    observed->failAccounts = false;
    observed->rejectAuthentication = true;
    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&] { return !controller.tbankSyncRunning(); }));
    CHECK_FALSE(controller.tbankConnected());
    CHECK(controller.accounts()->rowCount() == 2);
    CHECK(controller.syncStatus().contains(QStringLiteral("последние успешно загруженные")));
}

TEST_CASE("T-Bank network failures do not read a closed TLS reply") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("connected-session-123456789"));
    auto network = std::make_unique<TBankNetworkFixture>();
    network->failAccounts = true;
    auto* observedNetwork = network.get();
    wealthtracker::app::AppController controller(std::move(credentials), std::move(network),
                                                 nullptr);

    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&controller] { return !controller.tbankSyncRunning(); }));

    CHECK(observedNetwork->readCalls == 0);
    CHECK(controller.syncStatus().contains(QStringLiteral("банк закрыл соединение")));
    CHECK(controller.tbankConnected());
    CHECK(controller.accounts()->rowCount() == 0);
}

TEST_CASE("T-Bank authentication failures expire the stored session") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("expired-session-123456789"));
    auto* observedCredentials = credentials.get();
    auto network = std::make_unique<TBankNetworkFixture>();
    network->rejectAuthentication = true;
    wealthtracker::app::AppController controller(std::move(credentials), std::move(network),
                                                 nullptr);

    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&controller] { return !controller.tbankSyncRunning(); }));

    CHECK_FALSE(controller.tbankConnected());
    CHECK_FALSE(observedCredentials->values.contains(
        QStringLiteral("tbank-retail-session")));
}

TEST_CASE("balance overflow never wraps the displayed net worth") {
    const TBankSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    credentials->values.insert(QStringLiteral("tbank-retail-session"),
                               QByteArrayLiteral("connected-session-123456789"));
    auto network = std::make_unique<TBankNetworkFixture>();
    network->overflowBalances = true;
    wealthtracker::app::AppController controller(std::move(credentials),
                                                 std::move(network), nullptr);

    REQUIRE(controller.synchronizeTBank());
    REQUIRE(waitUntil([&controller] { return !controller.tbankSyncRunning(); }));

    CHECK(controller.netWorth() == QStringLiteral("—"));
    CHECK(controller.reportIncome() == QStringLiteral("0 ₽"));
    CHECK(controller.reportSpending() == QStringLiteral("0 ₽"));
    CHECK(controller.savings() == QStringLiteral("0 ₽"));
}

TEST_CASE("T-Invest network failures do not read a closed TLS reply") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto network = std::make_unique<FailedInvestmentNetworkFixture>();
    auto* observedNetwork = network.get();
    wealthtracker::app::AppController controller(
        std::move(credentials), std::move(network), nullptr);

    REQUIRE(controller.saveInvestmentToken(
        QStringLiteral("readonly-token-with-safe-length")));
    controller.synchronizeInvestments();
    REQUIRE(waitUntil([&controller] {
        return controller.syncStatus().contains(QStringLiteral("Ошибка Т-Инвест"));
    }));

    CHECK(observedNetwork->readCalls == 0);
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

TEST_CASE("T-Invest synchronizes portfolio without requesting operation history") {
    auto network = std::make_unique<InvestmentNetworkFixture>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(
        std::make_unique<MemoryCredentialBackend>(), std::move(network), nullptr);
    REQUIRE(controller.saveInvestmentToken(QStringLiteral("readonly-token-with-safe-length")));
    controller.synchronizeInvestments();
    REQUIRE(waitUntil([&] { return controller.accounts()->rowCount() == 1; }));
    CHECK(controller.portfolioValue() == QStringLiteral("120,50 ₽"));
    CHECK(controller.netWorth() == QStringLiteral("120,50 ₽"));
    REQUIRE(controller.holdings()->rowCount() == 1);
    CHECK(controller.holdings()->data(controller.holdings()->index(0, 0),
        wealthtracker::app::HoldingModel::ValueRole).toString() == QStringLiteral("120,50 ₽"));
    CHECK(observed->portfolioRequests == 1);
    CHECK(observed->unexpectedRequests == 0);
    CHECK(controller.reportSpending() == QStringLiteral("0 ₽"));

    SECTION("Failed refresh preserves the last complete snapshot") {
        observed->failPortfolio = true;
        controller.synchronizeInvestments();
        REQUIRE(waitUntil([&] { return controller.syncStatus().contains(QStringLiteral("Ошибка Т-Инвест")); }));
        CHECK(controller.portfolioValue() == QStringLiteral("120,50 ₽"));
        CHECK(controller.holdings()->rowCount() == 1);
    }
    SECTION("Removing connection clears the portfolio") {
        controller.removeInvestmentToken();
        CHECK(controller.accounts()->rowCount() == 0);
        CHECK(controller.holdings()->rowCount() == 0);
        CHECK(controller.portfolioValue() == QStringLiteral("0 ₽"));
    }
}

TEST_CASE("T-Invest cancellation prevents late publication and permits a fresh sync") {
    auto network = std::make_unique<InvestmentNetworkFixture>();
    auto* observed = network.get();
    observed->delay = 10;
    wealthtracker::app::AppController controller(
        std::make_unique<MemoryCredentialBackend>(), std::move(network), nullptr);
    REQUIRE(controller.saveInvestmentToken(QStringLiteral("readonly-token-with-safe-length")));
    controller.synchronizeInvestments();
    SECTION("Privacy lock") {
        controller.lock();
        controller.synchronizeInvestments();
        CHECK(observed->requests == 1);
        REQUIRE(controller.unlock());
    }
    SECTION("Token replacement") {
        REQUIRE(controller.saveInvestmentToken(QStringLiteral("replacement-readonly-token-with-safe-length")));
    }
    SECTION("Disconnect") {
        controller.removeInvestmentToken();
        REQUIRE(controller.saveInvestmentToken(QStringLiteral("replacement-readonly-token-with-safe-length")));
    }
    CHECK(controller.accounts()->rowCount() == 0);
    observed->delay = 0;
    controller.synchronizeInvestments();
    REQUIRE(waitUntil([&] { return controller.accounts()->rowCount() == 1; }));
    CHECK(observed->portfolioRequests == 1);
}

TEST_CASE("Retail browser projections enforce authentication and UTF-8 byte limits") {
    const auto verify = [](const auto& parse) {
        const auto now = QDateTime::currentDateTimeUtc();
        CHECK_FALSE(parse(QStringLiteral("not-json"), now));
        CHECK_FALSE(parse(QStringLiteral(
            R"({"version":2,"stage":"ready","authenticated":true,"accounts":[]})"), now));
        CHECK_FALSE(parse(QStringLiteral(
            R"({"version":1,"stage":"ready","authenticated":false,"accounts":[]})"), now));
        CHECK_FALSE(parse(QStringLiteral(
            R"({"version":1,"stage":"authentication","authenticated":true,"accounts":[]})"), now));
        CHECK_FALSE(parse(QStringLiteral(
            R"({"version":1,"stage":"ready","authenticated":true,"accounts":[{"id":"a","currency":"RUB","balanceMinor":100}]})"), now));
        const auto oversized = QStringLiteral(
            R"({"version":1,"stage":"ready","authenticated":true,"accounts":[{"id":"a","currency":"RUB","balanceMinor":"100","name":")")
            + QString(5 * 1024 * 1024, QChar(0x0410)) + QStringLiteral("\"}]}");
        REQUIRE(oversized.size() < 8 * 1024 * 1024);
        REQUIRE(oversized.toUtf8().size() > 8 * 1024 * 1024);
        CHECK_FALSE(parse(oversized, now));
    };
    verify(wealthtracker::app::parse_alfa_financial_projection);
    verify(wealthtracker::app::parse_vtb_financial_projection);
}
