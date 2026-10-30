#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariantMap>
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
    return app.exec();
}
