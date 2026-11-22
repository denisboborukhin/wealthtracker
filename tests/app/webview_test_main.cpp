#include <catch2/catch_session.hpp>

#include <QGuiApplication>
#include <QtWebView/QtWebView>

int runBrowserSessionProbe(QGuiApplication&, const QString&);

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QGuiApplication application(argc, argv);
    const auto probe = qEnvironmentVariable("WT_BROWSER_SESSION_PROBE");
    if (!probe.isEmpty()) return runBrowserSessionProbe(application, probe);
    return Catch::Session().run(argc, argv);
}
