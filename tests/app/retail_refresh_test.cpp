#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>

#include <functional>
#include <memory>

namespace {

struct SettingsScope {
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QSettings::Format format = QSettings::defaultFormat();
    QTemporaryDir directory;

    SettingsScope() {
        QCoreApplication::setOrganizationName(QStringLiteral("WealthTrackerSessionTests"));
        QCoreApplication::setApplicationName(QStringLiteral("RetailRefresh"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    }

    ~SettingsScope() {
        QSettings::setDefaultFormat(format);
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setApplicationName(application);
    }
};

bool waitFor(const std::function<bool()>& ready, const int timeoutMilliseconds) {
    QEventLoop loop;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (ready()) loop.quit();
    });
    timer.start(10);
    QTimer::singleShot(timeoutMilliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

} // namespace

TEST_CASE("Retail refresh retries after a busy or locked timer deadline") {
    bool privacyScreen = false;
    SECTION("Another bank is logging in") {}
    SECTION("The privacy screen is locked") { privacyScreen = true; }

    SettingsScope settings;
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
    SettingsScope settings;
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
    SettingsScope settings;
    QSettings{}.setValue(QStringLiteral("alfa/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    REQUIRE(waitFor([&] { return controller.alfaSyncRunning(); }, 3'000));
    auto* view = static_cast<QWebView*>(controller.alfaWebView());
    REQUIRE(view);
    view->loadHtml(QStringLiteral(
        "<html><script>globalThis.fixtureDocument = true</script>Authentication fixture</html>"),
        QUrl{QStringLiteral("https://private.auth.alfabank.ru/")});
    REQUIRE(waitFor([&] { return !controller.alfaSyncRunning(); }, 48'000));
    CHECK(controller.alfaConnected());
    CHECK(QSettings{}.value(QStringLiteral("alfa/connected")).toBool());
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
    SettingsScope settings;
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
    SettingsScope settings;
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
