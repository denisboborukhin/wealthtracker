#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
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
