#include "app/AppController.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QVariantMap>
#include <QtWebView/QtWebView>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setOrganizationName(QStringLiteral("WealthTracker"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

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
        QTimer::singleShot(750, &application,
                           [window, screenshotPath, &application] {
            const QImage image = window->grabWindow();
            application.exit(!image.isNull() && image.save(screenshotPath) ? 0 : 2);
        });
    }
    return application.exec();
}
