#include "app/AppController.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

namespace {

void configureWebEnginePrivacy() {
    // Keep embedded login pages from probing public STUN servers over UDP.
    // Preserve an explicit policy supplied by the person launching the app.
    constexpr auto chromiumFlagsVariable = "QTWEBENGINE_CHROMIUM_FLAGS";
    auto chromiumFlags = qEnvironmentVariable(chromiumFlagsVariable);
    if (chromiumFlags.contains(
            QStringLiteral("--force-webrtc-ip-handling-policy="))) {
        return;
    }

    if (!chromiumFlags.isEmpty()) {
        chromiumFlags += QLatin1Char(' ');
    }
    chromiumFlags += QStringLiteral(
        "--force-webrtc-ip-handling-policy=disable_non_proxied_udp");
    qputenv(chromiumFlagsVariable, chromiumFlags.toUtf8());
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    configureWebEnginePrivacy();
    QtWebEngineQuick::initialize();
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setOrganizationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    wealthtracker::app::AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
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
        QTimer::singleShot(750, &application,
                           [window, screenshotPath, &application] {
            const QImage image = window->grabWindow();
            application.exit(!image.isNull() && image.save(screenshotPath) ? 0 : 2);
        });
    }
    return application.exec();
}
