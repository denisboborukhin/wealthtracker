#include "app/BrowserSession.hpp"

#include <QGuiApplication>
#include <QDebug>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>

#include <functional>

import wt.alfa_login;
import wt.tbank_login;
import wt.vtb_login;
import wt.connectors;

int runBrowserSessionProbe(QGuiApplication& application, const QString& mode) {
    using namespace wealthtracker::app;
    TBankLoginCoordinator tbank([](QString) {}, [](QByteArray) {}, [](QString) {}, nullptr);
    AlfaLoginCoordinator alfa([](QString) {}, [](wealthtracker::connectors::NormalizedBatch) {},
                              [](AlfaLoginFailure) {}, nullptr);
    VtbLoginCoordinator vtb([](QString) {}, [](wealthtracker::connectors::NormalizedBatch) {},
                            [](VtbLoginFailure) {}, nullptr);
    const QStringList domains{QStringLiteral("wealthtracker-session-a.test"),
                              QStringLiteral("wealthtracker-session-b.test")};
    auto* view = static_cast<QWebView*>(mode == QStringLiteral("write") ? tbank.webView()
                                       : mode == QStringLiteral("read") ? alfa.webView()
                                                                        : vtb.webView());
    if (mode == QStringLiteral("clear-a") || mode == QStringLiteral("clear-all")) {
        clearBankBrowserData(mode == QStringLiteral("clear-a") ? QStringList{domains.front()} : domains,
                             [&application] { application.exit(0); });
    } else {
        int index = 0;
        std::function<void()> load = [&] {
            view->loadHtml(QStringLiteral("<html><body>Session fixture</body></html>"),
                          QUrl{QStringLiteral("https://%1/").arg(domains.at(index))});
        };
        QObject::connect(view, &QWebView::loadingChanged, &application,
                         [&](const QWebViewLoadingInfo& info) {
            if (info.status() != QWebViewLoadingInfo::LoadStatus::Succeeded) return;
            const auto write = mode == QStringLiteral("write");
            const auto expected = mode == QStringLiteral("after-clear") && index == 0 ? 0 : 3;
            const auto script = write
                ? QStringLiteral("document.cookie = 'wt_fixture=1; Max-Age=3600; Secure; SameSite=Strict; Path=/';"
                                 "localStorage.setItem('wt_fixture', '1'); 3")
                : QStringLiteral("(document.cookie.includes('wt_fixture=1') ? 1 : 0) + "
                                 "(localStorage.getItem('wt_fixture') === '1' ? 2 : 0)");
            view->runJavaScript(script, [&, write, expected](const QVariant& value) {
                if (value.toInt() != expected) {
                    qWarning() << "Fixture state mismatch" << mode << index << value.toInt();
                    application.exit(2);
                    return;
                }
                if (++index < domains.size()) { load(); return; }
                if (write) tbank.cancel();
                QTimer::singleShot(1'000, &application, [&application] { application.exit(0); });
            });
        });
        QTimer::singleShot(0, &application, load);
        QTimer::singleShot(8'000, &application, [&application] { application.exit(3); });
        return application.exec();
    }
    QTimer::singleShot(8'000, &application, [&application] { application.exit(3); });
    return application.exec();
}
