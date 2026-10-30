#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <QUrl>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>

import wt.connectors;
import wt.alfa_login;
import wt.vtb_login;

TEST_CASE("An unauthenticated bank page never reports successful login") {
    QStringList statuses;
    bool completed = false;
    bool failed = false;
    const auto status = [&](QString value) { statuses.push_back(std::move(value)); };
    const auto done = [&](wealthtracker::connectors::NormalizedBatch) { completed = true; };
    const auto failure = [&](auto) { failed = true; };
    const auto exercise = [&](auto& coordinator, const QUrl& origin) {
        auto* view = static_cast<QWebView*>(coordinator.webView());
        QEventLoop loaded;
        QObject::connect(view, &QWebView::loadingChanged, &loaded,
                         [&](const QWebViewLoadingInfo& info) {
            if (info.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) loaded.quit();
        });
        view->loadHtml(QStringLiteral("<html><body><input type='tel'></body></html>"), origin);
        QTimer::singleShot(3'000, &loaded, &QEventLoop::quit);
        loaded.exec();
        REQUIRE(view->url() == origin);
        coordinator.startLogin(origin);
        QEventLoop probed;
        QTimer::singleShot(1'200, &probed, &QEventLoop::quit);
        probed.exec();
        CHECK(coordinator.active());
        CHECK_FALSE(completed);
        CHECK_FALSE(failed);
        for (const auto& message : statuses)
            CHECK_FALSE(message.contains(QStringLiteral("Вход выполнен")));
        coordinator.cancel();
    };
    SECTION("Alfa") {
        wealthtracker::app::AlfaLoginCoordinator coordinator(status, done, failure, nullptr);
        exercise(coordinator, QUrl{QStringLiteral("https://web.alfabank.ru/")});
    }
    SECTION("VTB") {
        wealthtracker::app::VtbLoginCoordinator coordinator(status, done, failure, nullptr);
        exercise(coordinator, QUrl{QStringLiteral("https://online.vtb.ru/")});
    }
}

TEST_CASE("Alfa authentication redirect can restore the dashboard without a new login") {
    using namespace wealthtracker::app;
    bool failed = false;
    bool completed = false;
    AlfaLoginCoordinator coordinator([](QString) {},
        [&](wealthtracker::connectors::NormalizedBatch batch) {
            completed = batch.accounts.size() == 1;
        }, [&](AlfaLoginFailure) { failed = true; }, nullptr);
    const QUrl dashboard{QStringLiteral("https://web.alfabank.ru/dashboard")};
    coordinator.synchronize(dashboard);
    auto* view = static_cast<QWebView*>(coordinator.existingWebView());
    REQUIRE(view);
    QEventLoop authentication;
    QObject::connect(view, &QWebView::loadingChanged, &authentication,
                     [&](const QWebViewLoadingInfo& info) {
        if (info.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) authentication.quit();
    });
    view->loadHtml(QStringLiteral("<html>Restoring session</html>"),
                   QUrl{QStringLiteral("https://private.auth.alfabank.ru/")});
    QTimer::singleShot(3'000, &authentication, &QEventLoop::quit);
    authentication.exec();
    REQUIRE_FALSE(failed);
    REQUIRE(coordinator.active());
    REQUIRE(coordinator.existingWebView() == view);
    view->loadHtml(QStringLiteral(R"HTML(<html><script>
      globalThis.initialState = { layoutData: { accounts: [
        { id: 'fixture-account', amount: { value: '1.00', currency: 'RUB' } }
      ] } };
    </script></html>)HTML"), dashboard);
    QEventLoop finished;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &finished, [&] {
        if (completed || failed) finished.quit();
    });
    timer.start(10);
    QTimer::singleShot(3'000, &finished, &QEventLoop::quit);
    finished.exec();
    CHECK(completed);
    CHECK_FALSE(failed);
}

TEST_CASE("VTB off-origin navigation defers native WebView destruction") {
    QEventLoop failureLoop;
    bool failed = false;
    wealthtracker::app::VtbLoginCoordinator coordinator(
        [](QString) {}, [](wealthtracker::connectors::NormalizedBatch) {},
        [&failureLoop, &failed](wealthtracker::app::VtbLoginFailure) {
            failed = true;
            failureLoop.quit();
        },
        nullptr);

    coordinator.startLogin(QUrl{QStringLiteral("https://online.vtb.ru/")});
    QPointer<QWebView> view =
        static_cast<QWebView*>(coordinator.existingWebView());
    REQUIRE(view);

    view->setUrl(QUrl{QStringLiteral("about:blank")});
    QTimer::singleShot(3'000, &failureLoop, &QEventLoop::quit);
    failureLoop.exec();

    REQUIRE(failed);
    CHECK(coordinator.existingWebView() == nullptr);
    CHECK(view);

    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK_FALSE(view);
}

TEST_CASE("Browser sessions survive process restart and clear only the selected site") {
    const auto probe = [](const QString& mode) {
        INFO(mode.toStdString());
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WT_BROWSER_SESSION_PROBE"), mode);
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), QStringList{});
        REQUIRE(process.waitForFinished(15'000));
        INFO(process.readAllStandardError().toStdString());
        REQUIRE(process.exitStatus() == QProcess::NormalExit);
        REQUIRE(process.exitCode() == 0);
    };
    probe(QStringLiteral("clear-all"));
    probe(QStringLiteral("write"));
    probe(QStringLiteral("read"));
    probe(QStringLiteral("clear-a"));
    probe(QStringLiteral("after-clear"));
    probe(QStringLiteral("clear-all"));
}

TEST_CASE("Startup starts retail browser refreshes together and cancels both on lock") {
    struct SettingsScope {
        QString organization = QCoreApplication::organizationName();
        QString application = QCoreApplication::applicationName();
        QSettings::Format format = QSettings::defaultFormat();
        QTemporaryDir directory;
        SettingsScope() {
            QCoreApplication::setOrganizationName(QStringLiteral("WealthTrackerSessionTests"));
            QCoreApplication::setApplicationName(QStringLiteral("StartupRestore"));
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        }
        ~SettingsScope() {
            QSettings::setDefaultFormat(format);
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
        }
    } settings;
    QSettings{}.setValue(QStringLiteral("alfa/connected"), true);
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    wealthtracker::app::AppController controller(
        std::unique_ptr<wealthtracker::app::CredentialBackend>{}, nullptr);
    const auto waitFor = [](const std::function<bool()>& ready) {
        QEventLoop loop;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
            if (ready()) loop.quit();
        });
        timer.start(10);
        QTimer::singleShot(3'000, &loop, &QEventLoop::quit);
        loop.exec();
        return ready();
    };
    REQUIRE(waitFor([&] { return controller.alfaSyncRunning() && controller.vtbSyncRunning(); }));
    controller.lock();
    CHECK_FALSE(controller.alfaSyncRunning());
    CHECK_FALSE(controller.vtbSyncRunning());
    REQUIRE(controller.unlock());
    CHECK(controller.alfaConnected());
    CHECK(controller.vtbConnected());
    CHECK_FALSE(controller.alfaSyncRunning());
    CHECK_FALSE(controller.vtbSyncRunning());
    controller.lock();
}

TEST_CASE("VTB startup waits for the page client to restore authentication") {
    using namespace wealthtracker::app;
    bool persistentRejection = false;
    SECTION("A temporary startup rejection recovers") {}
    SECTION("Persistent rejection preserves the page for inspection") { persistentRejection = true; }
    QEventLoop finished;
    bool completed = false;
    bool failed = false;
    VtbLoginFailureKind failureKind = VtbLoginFailureKind::Protocol;
    VtbLoginCoordinator coordinator(
        [](QString) {},
        [&](wealthtracker::connectors::NormalizedBatch batch) {
            completed = batch.balances.size() == 1;
            finished.quit();
        },
        [&](VtbLoginFailure failure) { failed = true; failureKind = failure.kind; finished.quit(); }, nullptr);
    auto* view = static_cast<QWebView*>(coordinator.webView());
    QEventLoop loading;
    QObject::connect(view, &QWebView::loadingChanged, &loading,
                     [&loading](const QWebViewLoadingInfo& info) {
        if (info.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) loading.quit();
    });
    const QUrl origin{QStringLiteral("https://online.vtb.ru/")};
    view->loadHtml(QStringLiteral(R"HTML(<html><script>
      globalThis.fixtureReads = 0;
      globalThis.fixtureForceRejection = %1;
      globalThis.mf_1488_core = { get: () => Promise.resolve(() => ({http: {get: path => {
        if (path.endsWith('/portfolios/active')) {
          if (++globalThis.fixtureReads === 1 || globalThis.fixtureForceRejection) return Promise.reject({status: 401});
          return Promise.resolve({data: {portfolio: {accounts: [
            {publicId: 'fixture-account', balance: {sum: '1.00', currency: 'RUB'}}
          ]}}});
        }
        return Promise.resolve({data: {balanceResponse: []}});
      }}})) };
    </script></html>)HTML").arg(persistentRejection ? QStringLiteral("true") : QStringLiteral("false")), origin);
    QTimer::singleShot(3'000, &loading, &QEventLoop::quit);
    loading.exec();
    REQUIRE(view->url() == origin);
    coordinator.synchronize(origin);
    QTimer::singleShot(15'000, &finished, &QEventLoop::quit);
    finished.exec();
    CHECK(failed == persistentRejection);
    CHECK(completed != persistentRejection);
    CHECK(failureKind != VtbLoginFailureKind::Timeout);
    if (persistentRejection) {
        CHECK(failureKind == VtbLoginFailureKind::AuthenticationUnconfirmed);
        REQUIRE(coordinator.existingWebView() == view);
    }
    CHECK_FALSE(coordinator.active());
    if (!persistentRejection && completed) {
        QEventLoop changed;
        view->runJavaScript(QStringLiteral("globalThis.fixtureForceRejection = true; true"),
                            [&changed](const QVariant&) { changed.quit(); });
        QTimer::singleShot(3'000, &changed, &QEventLoop::quit);
        changed.exec();
        completed = false;
        failed = false;
        coordinator.synchronize(origin);
        QTimer::singleShot(3'000, &finished, &QEventLoop::quit);
        finished.exec();
        CHECK(failed);
        CHECK_FALSE(completed);
        CHECK(failureKind == VtbLoginFailureKind::AuthenticationUnconfirmed);
        REQUIRE(coordinator.existingWebView() == view);
    }

    // Opening the bank after a failed refresh must preserve this exact document.
    QEventLoop changed;
    view->runJavaScript(QStringLiteral(
        "globalThis.fixtureForceRejection = false; globalThis.fixtureReads = 10; true"),
        [&changed](const QVariant&) { changed.quit(); });
    QTimer::singleShot(3'000, &changed, &QEventLoop::quit);
    changed.exec();
    completed = false;
    failed = false;
    coordinator.startLogin(origin);
    QTimer::singleShot(3'000, &finished, &QEventLoop::quit);
    finished.exec();
    CHECK(completed);
    CHECK_FALSE(failed);
    CHECK(coordinator.existingWebView() == view);
}
