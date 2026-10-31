#include <QGuiApplication>
#include "app/BrowserSession.hpp"
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariantMap>
#include <QThread>
#include <QTimer>
#include <QMetaObject>
#include <QtWebView/QtWebView>
#include <QtWebView/QWebView>

int main(int argc, char** argv) {
    QtWebView::initialize();
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    QWebView view;
    const auto children = view.findChildren<QWindow*>(QString{}, Qt::FindDirectChildrenOnly);
    const bool wrapper = app.arguments().contains("--wrapper");
    if (children.size() != 1) return 2;
    QWindow* display = wrapper ? &view : children.front();
    view.loadHtml(QStringLiteral("<!doctype html><html><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<body style='background:#d8f5d0;font:24px sans-serif;padding:20px'>"
        "<h1>Native view works</h1><label>Synthetic input<input placeholder='Type here'></label></body></html>"));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"bankWindow", QVariant::fromValue(display)}});
    engine.loadFromModule("ViewProbe", "Main");
    if (engine.rootObjects().isEmpty()) return 3;
    if (app.arguments().contains("--callback-probe") ||
        app.arguments().contains("--guarded-callback-probe")) {
        QTimer ticks;
        int tickCount = 0;
        int callbackCount = 0;
        int initialTicks = 0;
        bool allOwnerThread = true;
        bool allValuesValid = true;
        bool canceledCallback = false;
        std::function<void()> evaluate;
        QObject::connect(&ticks, &QTimer::timeout, &app, [&] { ++tickCount; });
        ticks.start(100);
        evaluate = [&] {
            wealthtracker::app::runBankJavaScript(&view, &view,
                QStringLiteral("JSON.stringify({stage:'synthetic',value:42})"),
                [&](const QVariant& result) {
                    allOwnerThread &= QThread::currentThread() == view.thread();
                    allValuesValid &= result.toString() == QStringLiteral("{\"stage\":\"synthetic\",\"value\":42}");
                    if (++callbackCount < 3) {
                        if (callbackCount == 1) evaluate();
                        else QTimer::singleShot(150, &view, evaluate);
                        return;
                    }
                    auto* receiver = new QObject(&app);
                    wealthtracker::app::runBankJavaScript(&view, receiver, QStringLiteral("42"),
                        [&](const QVariant&) { canceledCallback = true; });
                    delete receiver;
                    QTimer::singleShot(250, &app, [&] {
                        const bool progress = tickCount > initialTicks;
                        qInfo("WT_CALLBACK_PROBE guarded_owner_thread=%d callbacks=%d ticks_progress=%d canceled_callback=%d values_valid=%d",
                              allOwnerThread, callbackCount, progress, canceledCallback, allValuesValid);
                        app.exit(allOwnerThread && callbackCount == 3 && progress && !canceledCallback && allValuesValid ? 0 : 6);
                    });
                });
        };
        QTimer::singleShot(1'000, &app, [&] {
            initialTicks = tickCount;
            if (app.arguments().contains("--guarded-callback-probe")) {
                evaluate();
                return;
            }
            view.runJavaScript(QStringLiteral("JSON.stringify({stage:'synthetic',value:42})"),
                [&](const QVariant& result) {
                    const bool ownerThread = QThread::currentThread() == view.thread();
                    const bool validResult = result.toString() == QStringLiteral("{\"stage\":\"synthetic\",\"value\":42}");
                    qInfo("WT_CALLBACK_PROBE raw_owner_thread=%d valid_result=%d", ownerThread, validResult);
                    QMetaObject::invokeMethod(&app, [&, ownerThread, validResult] {
                        qInfo("WT_CALLBACK_PROBE owner_ticks=%d", tickCount);
                        QTimer::singleShot(100, &app, [&, ownerThread, validResult] {
                            app.exit(ownerThread && validResult && tickCount >= 5 ? 0 : 4);
                        });
                    }, Qt::QueuedConnection);
                });
        });
        QTimer::singleShot(10'000, &app, [&] { app.exit(5); });
        return app.exec();
    }
    return app.exec();
}
