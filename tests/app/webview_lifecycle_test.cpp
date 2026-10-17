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
import wt.vtb_login;

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

TEST_CASE("Startup restores the next retail connection after cancellation") {
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
    REQUIRE(waitFor([&] { return controller.alfaSyncRunning(); }));
    CHECK_FALSE(controller.vtbSyncRunning());
    controller.lock();
    CHECK_FALSE(controller.alfaSyncRunning());
    CHECK_FALSE(controller.vtbSyncRunning());
    REQUIRE(controller.unlock());
    REQUIRE(waitFor([&] { return controller.vtbSyncRunning(); }));
    CHECK_FALSE(controller.alfaSyncRunning());
    controller.lock();
}

TEST_CASE("VTB startup waits for the page client to restore authentication") {
    using namespace wealthtracker::app;
    bool persistentRejection = false;
    SECTION("A temporary startup rejection recovers") {}
    SECTION("Persistent rejection still requires login") { persistentRejection = true; }
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
    if (persistentRejection) CHECK(failureKind == VtbLoginFailureKind::AuthenticationRequired);
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
        CHECK(failureKind == VtbLoginFailureKind::AuthenticationRequired);
    }
}
