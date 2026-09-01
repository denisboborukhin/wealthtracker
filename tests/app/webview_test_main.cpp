#include <catch2/catch_session.hpp>

#include <QGuiApplication>
#include <QtWebView/QtWebView>

int main(int argc, char* argv[]) {
    QtWebView::initialize();
    QGuiApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
