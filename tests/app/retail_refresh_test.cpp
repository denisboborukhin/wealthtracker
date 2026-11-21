#include "app/AppController.hpp"
#include "../support/QtTestSupport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>

#include <algorithm>
#include <memory>

namespace {
using wealthtracker::test::SettingsScope;
using wealthtracker::test::waitFor;

class StartupCredentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString&, const QByteArray&) override { return {}; }
    std::expected<void, QString> remove(const QString&) override { return {}; }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        if (key == QStringLiteral("tbank-retail-session"))
            return std::optional<QByteArray>{QByteArrayLiteral("connected-session-123456789")};
        return std::optional<QByteArray>{};
    }
};

class PendingReply final : public QNetworkReply {
public:
    PendingReply(const QNetworkRequest& request, int& aborts, QObject* parent) : QNetworkReply(parent), aborts_(aborts) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }
    void abort() override {
        ++aborts_;
        setError(OperationCanceledError, QStringLiteral("Fixture canceled"));
        setFinished(true);
        emit finished();
    }
    void complete(QByteArray body) {
        body_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return body_.size() + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        if (body_.isEmpty()) return isFinished() ? -1 : 0;
        const auto count = std::min(maximum, qint64(body_.size()));
        std::copy_n(body_.constData(), count, data);
        body_.remove(0, count);
        return count;
    }
private:
    int& aborts_;
    QByteArray body_;
};

class PendingNetwork final : public QNetworkAccessManager {
public:
    int requests = 0;
    int aborts = 0;
    PendingReply* latest = nullptr;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        latest = new PendingReply(request, aborts, this);
        return latest;
    }
};

} // namespace

TEST_CASE("T-Bank loads accounts after T-ID navigation without closing the login window") {
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    auto network = std::make_unique<PendingNetwork>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(std::make_unique<StartupCredentials>(),
                                                std::move(network), nullptr);
    controller.beginTBankLogin();
    REQUIRE(controller.tbankLoginActive());
    auto* view = static_cast<QWebView*>(controller.tbankWebView());
    REQUIRE(view);
    const auto page = QStringLiteral(R"HTML(<html><script>
      performance.getEntriesByType = () => [{
        name: 'https://www.tbank.ru/api/common/v1/ping?sessionid=synthetic-login-session'
      }]
    </script>Bank navigation fixture</html>)HTML");
    int loaded = 0;
    QObject::connect(view, &QWebView::loadingChanged, view, [&](const QWebViewLoadingInfo& info) {
        if (info.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) ++loaded;
    });
    view->loadHtml(page, QUrl(QStringLiteral("https://id.tbank.ru/")));
    REQUIRE(waitFor([&] { return loaded == 1; }, 3'000));
    CHECK_FALSE(waitFor([&] { return observed->requests > 0; }, 1'100));
    view->loadHtml(page, controller.tbankLoginUrl());
    REQUIRE(waitFor([&] { return loaded == 2 && observed->requests == 1; }, 3'000));
    CHECK(controller.tbankLoginActive());
    REQUIRE(observed->latest->url().path() == QStringLiteral("/api/common/v1/ping"));
    observed->latest->complete(QByteArrayLiteral(R"({"resultCode":"OK","payload":{"accessLevel":"CLIENT"}})"));
    REQUIRE(waitFor([&] { return observed->requests == 2 && controller.tbankSyncRunning(); }, 3'000));
    REQUIRE(observed->latest->url().path() == QStringLiteral("/api/common/v1/accounts_light_ib"));
    observed->latest->complete(QByteArrayLiteral(
        R"({"resultCode":"OK","payload":[{"id":"fixture-account","name":"Account","accountType":"Debit",)"
        R"("moneyAmount":{"value":"123.45","currency":{"name":"RUB"}}}]})"));
    CHECK_FALSE(controller.tbankLoginActive());
    CHECK_FALSE(controller.tbankSyncRunning());
    CHECK(controller.tbankConnected());
    CHECK(controller.accounts()->rowCount() == 1);
}

TEST_CASE("Retail refresh retries after a busy or locked timer deadline") {
    bool privacyScreen = false;
    SECTION("Another bank is logging in") {}
    SECTION("The privacy screen is locked") { privacyScreen = true; }

    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    REQUIRE(settings.directory.isValid());
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);

    // Finish the startup attempt independently of the periodic refresh queue.
    REQUIRE(waitFor([&] { return controller.vtbSyncRunning(); }, 3'000));
    auto* view = static_cast<QWebView*>(controller.vtbWebView());
    REQUIRE(view);
    view->setUrl(QUrl{QStringLiteral("about:blank")});
    REQUIRE(waitFor([&] { return !controller.vtbSyncRunning(); }, 3'000));
    REQUIRE(controller.vtbConnected());

    if (privacyScreen) {
        controller.lock();
    } else {
        controller.beginAlfaLogin();
        REQUIRE(controller.alfaLoginActive());
        auto* alfa = static_cast<QWebView*>(controller.alfaWebView());
        REQUIRE(alfa);
        alfa->loadHtml(QStringLiteral("<html><body>Login fixture</body></html>"),
                       controller.alfaLoginUrl());
    }

    QSettings{}.setValue(QStringLiteral("vtb/lastSyncAt"),
                         QDateTime::currentDateTimeUtc().addSecs(-7 * 60 * 60)
                             .toString(Qt::ISODateWithMs));
    controller.setVtbAutoUpdate(true);
    const bool startedWhileBlocked = waitFor([&] { return controller.vtbSyncRunning(); }, 11'500);
    CHECK_FALSE(startedWhileBlocked);
    if (privacyScreen) {
        REQUIRE(controller.unlock());
    } else {
        REQUIRE(controller.alfaLoginActive());
        controller.cancelAlfaLogin();
    }
    const bool retried = waitFor([&] { return controller.vtbSyncRunning(); }, 12'000);
    controller.lock();
    CHECK(retried);
    CHECK(controller.vtbAutoUpdate());
}

TEST_CASE("VTB rejected startup retains its connection and opens the same bank page") {
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    REQUIRE(waitFor([&] { return controller.vtbSyncRunning(); }, 3'000));
    auto* view = static_cast<QWebView*>(controller.vtbWebView());
    REQUIRE(view);
    view->loadHtml(QStringLiteral(R"HTML(<html><script>
      globalThis.fixtureDocument = true;
      globalThis.mf_1488_core = { get: () => Promise.resolve(() => ({
        http: { get: () => Promise.reject({status: 401}) }
      })) };
    </script></html>)HTML"), controller.vtbLoginUrl());
    REQUIRE(waitFor([&] { return !controller.vtbSyncRunning(); }, 15'000));
    CHECK(controller.vtbConnected());
    CHECK(QSettings{}.value(QStringLiteral("vtb/connected")).toBool());
    CHECK(controller.connectionStates()["vtb-retail"].toMap()["state"] == "error");
    CHECK_FALSE(controller.vtbLoginStatus().contains(QStringLiteral("истекла")));
    REQUIRE(controller.vtbWebView() == view);
    controller.beginVtbLogin();
    REQUIRE(controller.vtbLoginActive());
    REQUIRE(controller.vtbWebView() == view);
    bool inspected = false;
    view->runJavaScript(QStringLiteral("globalThis.fixtureDocument === true"),
                        [&](const QVariant& value) { inspected = value.toBool(); });
    CHECK(waitFor([&] { return inspected; }, 3'000));
    controller.cancelVtbLogin();
    CHECK(controller.vtbConnected());
}

TEST_CASE("Alfa unfinished authentication retains its connection and bank page") {
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    QSettings{}.setValue(QStringLiteral("alfa/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    REQUIRE(waitFor([&] { return controller.alfaSyncRunning(); }, 3'000));
    auto* view = static_cast<QWebView*>(controller.alfaWebView());
    REQUIRE(view);
    view->loadHtml(QStringLiteral(
        "<html><script>globalThis.fixtureDocument = true</script>Authentication fixture</html>"),
        QUrl{QStringLiteral("https://private.auth.alfabank.ru/")});
    REQUIRE(waitFor([&] { return !controller.alfaSyncRunning(); }, 11'500));
    CHECK(controller.alfaConnected());
    CHECK(QSettings{}.value(QStringLiteral("alfa/connected")).toBool());
    CHECK(controller.connectionStates()["alfa-retail"].toMap()["state"] == "error");
    CHECK(controller.accounts()->rowCount() == 0);
    REQUIRE(controller.alfaWebView() == view);
    controller.beginAlfaLogin();
    REQUIRE(controller.alfaLoginActive());
    REQUIRE(controller.alfaWebView() == view);
    bool inspected = false;
    view->runJavaScript(QStringLiteral("globalThis.fixtureDocument === true"),
                        [&](const QVariant& value) { inspected = value.toBool(); });
    CHECK(waitFor([&] { return inspected; }, 3'000));
    controller.cancelAlfaLogin();
    CHECK(controller.alfaConnected());
}

TEST_CASE("Retail refresh rejections update only the requested bank status") {
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    REQUIRE(settings.directory.isValid());
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    int alfaSignals = 0;
    int vtbSignals = 0;
    int tbankSignals = 0;
    QObject::connect(&controller, &wealthtracker::app::AppController::alfaConnectionChanged,
                     [&] { ++alfaSignals; });
    QObject::connect(&controller, &wealthtracker::app::AppController::vtbConnectionChanged,
                     [&] { ++vtbSignals; });
    QObject::connect(&controller, &wealthtracker::app::AppController::tbankConnectionChanged,
                     [&] { ++tbankSignals; });
    REQUIRE_FALSE(controller.synchronizeVtb());
    CHECK(controller.vtbLoginStatus() == controller.syncStatus());
    REQUIRE(vtbSignals == 1);
    const auto vtbStatus = controller.vtbLoginStatus();
    REQUIRE_FALSE(controller.synchronizeAlfa());
    CHECK(controller.alfaLoginStatus() == controller.syncStatus());
    CHECK(controller.alfaLoginStatus().contains(QStringLiteral("Альфа")));
    CHECK(controller.vtbLoginStatus() == vtbStatus);
    CHECK(alfaSignals == 1);
    CHECK(vtbSignals == 1);
    REQUIRE_FALSE(controller.synchronizeTBank());
    CHECK(controller.tbankLoginStatus() == controller.syncStatus());
    CHECK(tbankSignals == 1);
    CHECK(alfaSignals == 1);
    CHECK(vtbSignals == 1);
}

TEST_CASE("Retail bank pages can open during their own startup refresh without navigation") {
    bool alfa = false;
    SECTION("Alfa") { alfa = true; }
    SECTION("VTB") {}
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    QSettings{}.setValue(alfa ? QStringLiteral("alfa/connected") : QStringLiteral("vtb/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    REQUIRE(waitFor([&] { return alfa ? controller.alfaSyncRunning() : controller.vtbSyncRunning(); }, 3'000));
    auto* view = static_cast<QWebView*>(alfa ? controller.alfaWebView() : controller.vtbWebView());
    REQUIRE(view);
    bool loaded = false;
    QObject::connect(view, &QWebView::loadingChanged, &controller,
        [&](const QWebViewLoadingInfo& info) {
            if (info.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) loaded = true;
        });
    view->loadHtml(QStringLiteral("<html><script>globalThis.fixtureDocument = true</script>Verification fixture</html>"),
        alfa ? QUrl{QStringLiteral("https://private.auth.alfabank.ru/")}
             : controller.vtbLoginUrl());
    REQUIRE(waitFor([&] { return loaded; }, 3'000));
    if (alfa) controller.beginAlfaLogin();
    else controller.beginVtbLogin();
    REQUIRE((alfa ? controller.alfaLoginActive() : controller.vtbLoginActive()));
    CHECK_FALSE((alfa ? controller.alfaSyncRunning() : controller.vtbSyncRunning()));
    REQUIRE((alfa ? controller.alfaWebView() : controller.vtbWebView()) == view);
    CHECK((alfa ? controller.alfaConnected() : controller.vtbConnected()));
    bool inspected = false;
    view->runJavaScript(QStringLiteral("globalThis.fixtureDocument === true"),
        [&](const QVariant& value) { inspected = value.toBool(); });
    CHECK(waitFor([&] { return inspected; }, 3'000));
    if (alfa) controller.cancelAlfaLogin();
    else controller.cancelVtbLogin();
}

TEST_CASE("Retail startup refreshes overlap and stop after ten seconds while login remains interactive") {
    bool openAlfa = false;
    SECTION("All three background refreshes time out") {}
    SECTION("Interactive Alfa remains open while other refreshes time out") { openAlfa = true; }
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    QSettings{}.setValue(QStringLiteral("alfa/connected"), true);
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    QElapsedTimer elapsed;
    elapsed.start();
    wealthtracker::app::AppController controller(std::make_unique<StartupCredentials>(),
        std::make_unique<PendingNetwork>(), nullptr);
    REQUIRE(waitFor([&] {
        return controller.tbankSyncRunning() && controller.alfaSyncRunning() && controller.vtbSyncRunning();
    }, 2'000));
    auto* alfa = static_cast<QWebView*>(controller.alfaWebView());
    auto* vtb = static_cast<QWebView*>(controller.vtbWebView());
    REQUIRE(alfa);
    REQUIRE(vtb);
    alfa->loadHtml(QStringLiteral("<html>Verification fixture</html>"),
        QUrl{QStringLiteral("https://private.auth.alfabank.ru/")});
    vtb->loadHtml(QStringLiteral("<html><script>globalThis.mf_1488_core = {get: () => new Promise(() => {})}</script></html>"),
        controller.vtbLoginUrl());
    if (openAlfa) {
        controller.beginAlfaLogin();
        REQUIRE(controller.alfaLoginActive());
        CHECK_FALSE(controller.alfaSyncRunning());
        controller.beginVtbLogin();
        CHECK_FALSE(controller.vtbLoginActive());
    }
    REQUIRE(waitFor([&] {
        return !controller.tbankSyncRunning() && !controller.alfaSyncRunning() && !controller.vtbSyncRunning();
    }, 11'500));
    CHECK(elapsed.elapsed() >= 9'000);
    CHECK(elapsed.elapsed() < 11'500);
    CHECK(controller.tbankConnected());
    CHECK(controller.alfaConnected());
    CHECK(controller.vtbConnected());
    CHECK(controller.alfaWebView() == alfa);
    CHECK(controller.vtbWebView() == vtb);
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.alfaLoginActive() == openAlfa);
    controller.lock();
}

TEST_CASE("Manual retail refreshes overlap even when T-Bank starts last") {
    SettingsScope settings(QStringLiteral("WealthTrackerSessionTests"), QStringLiteral("RetailRefresh"));
    QSettings{}.setValue(QStringLiteral("alfa/connected"), true);
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    wealthtracker::app::AppController controller(std::make_unique<StartupCredentials>(),
        std::make_unique<PendingNetwork>(), nullptr);
    REQUIRE(controller.synchronizeAlfa());
    REQUIRE(controller.synchronizeVtb());
    REQUIRE(controller.synchronizeTBank());
    CHECK(controller.alfaSyncRunning());
    CHECK(controller.vtbSyncRunning());
    CHECK(controller.tbankSyncRunning());
    controller.lock();
    CHECK_FALSE(controller.alfaSyncRunning());
    CHECK_FALSE(controller.vtbSyncRunning());
    CHECK_FALSE(controller.tbankSyncRunning());
}

TEST_CASE("Demo mode suspends queued retail restoration and cancels active refresh") {
    SettingsScope settings("DemoIsolationTests", "QueuedAndActive");
    auto network = std::make_unique<PendingNetwork>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(std::make_unique<StartupCredentials>(), std::move(network), nullptr);
    controller.loadDemoData();
    const auto rows = controller.accounts()->rowCount();
    REQUIRE(controller.demoMode());
    CHECK_FALSE(waitFor([&] { return controller.tbankSyncRunning(); }, 150));
    CHECK(observed->requests == 0);
    controller.clearDemoData();
    REQUIRE(waitFor([&] { return controller.tbankSyncRunning(); }, 1000));
    REQUIRE(observed->requests > 0);
    controller.loadDemoData();
    CHECK(controller.demoMode());
    CHECK_FALSE(controller.tbankSyncRunning());
    CHECK(observed->aborts > 0);
    CHECK_FALSE(waitFor([&] { return !controller.demoMode(); }, 150));
    CHECK(controller.accounts()->rowCount() == rows);
    CHECK(controller.netWorth() == QStringLiteral("4 826 450 ₽"));
    CHECK_FALSE(controller.prepareTBankConnection());
    CHECK_FALSE(controller.prepareAlfaConnection());
    CHECK_FALSE(controller.prepareVtbConnection());
    CHECK_FALSE(controller.prepareOzonConnection());
    CHECK_FALSE(controller.synchronizeTBank());
    CHECK_FALSE(controller.synchronizeAlfa());
    CHECK_FALSE(controller.synchronizeVtb());
    CHECK_FALSE(controller.synchronizeOzon());
    CHECK_FALSE(controller.startZenPlugin("Fixture", {}));
    const auto requests = observed->requests;
    controller.synchronizeInvestments();
    controller.synchronizeBank("alfa-api");
    CHECK(observed->requests == requests);
    CHECK(controller.demoMode());
    controller.clearDemoData();
    REQUIRE(waitFor([&] { return controller.tbankSyncRunning(); }, 1000));
    CHECK(observed->requests > requests);
    controller.lock();
}

TEST_CASE("Demo mode suspends overdue bank timers and restores their schedule on exit") {
    SettingsScope settings("DemoIsolationTests", "OverdueTimer");
    auto network = std::make_unique<PendingNetwork>();
    auto* observed = network.get();
    wealthtracker::app::AppController controller(std::make_unique<StartupCredentials>(), std::move(network), nullptr);
    controller.loadDemoData();
    QSettings{}.setValue("tbank/lastSyncAt", QDateTime::currentDateTimeUtc().addSecs(-7 * 60 * 60).toString(Qt::ISODateWithMs));
    controller.setTBankAutoUpdate(true);
    CHECK_FALSE(waitFor([&] { return controller.tbankSyncRunning() || !controller.demoMode(); }, 10'500));
    CHECK(observed->requests == 0);
    controller.clearDemoData();
    REQUIRE(waitFor([&] { return controller.tbankSyncRunning(); }, 1000));
    CHECK(observed->requests > 0);
    controller.lock();
}
