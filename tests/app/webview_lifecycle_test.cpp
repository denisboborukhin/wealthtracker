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
