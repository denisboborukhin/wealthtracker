#include "app/AppController.hpp"

#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>
#include <QtWebView/QtWebView>

#include <openssl/ssl.h>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QGuiApplication application(argc, argv);
    if (OPENSSL_init_ssl(0, nullptr) != 1 ||
        !QSslSocket::setActiveBackend(QStringLiteral("openssl"))) {
        qCritical("The bundled OpenSSL TLS backend is unavailable");
        return 3;
    }
    QGuiApplication::setApplicationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setOrganizationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

#if defined(Q_OS_MACOS)
    // The cached Keychain bundle has one writer for the application's lifetime.
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataDirectory)) {
        qCritical("Unable to open the WealthTracker data directory");
        return 4;
    }
    QLockFile instanceLock(dataDirectory + QStringLiteral("/desktop.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        qCritical("Unable to start WealthTracker. Close any other running instance and try again");
        return 4;
    }
#endif

    wealthtracker::app::AppController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties(QVariantMap{
        {QStringLiteral("app"), QVariant::fromValue<QObject*>(&controller)}});
    engine.loadFromModule(QStringLiteral("WealthTracker"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    bool pageOk = false;
    const int page = qEnvironmentVariableIntValue("WEALTHTRACKER_PAGE", &pageOk);
    if (window && pageOk) {
        window->setProperty("currentPage", page);
    }

    const auto screenshotPath = qEnvironmentVariable("WEALTHTRACKER_SCREENSHOT");
    if (window && !screenshotPath.isEmpty()) {
        bool demoOk = false;
        const int demo = qEnvironmentVariableIntValue(
            "WEALTHTRACKER_SCREENSHOT_DEMO", &demoOk);
        if (demoOk && demo == 1) {
            controller.loadDemoData();
        }

        bool widthOk = false;
        const int width = qEnvironmentVariableIntValue(
            "WEALTHTRACKER_SCREENSHOT_WIDTH", &widthOk);
        if (widthOk && width >= window->minimumWidth() && width <= 4096) {
            window->setWidth(width);
        }

        bool heightOk = false;
        const int height = qEnvironmentVariableIntValue(
            "WEALTHTRACKER_SCREENSHOT_HEIGHT", &heightOk);
        if (heightOk && height >= window->minimumHeight() && height <= 4096) {
            window->setHeight(height);
        }

        QTimer::singleShot(750, &application,
                           [window, screenshotPath, &application] {
            const QImage image = window->grabWindow();
            application.exit(!image.isNull() && image.save(screenshotPath) ? 0 : 2);
        });
    }
    return application.exec();
}
