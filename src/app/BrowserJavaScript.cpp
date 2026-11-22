#include "app/BrowserSession.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QVariant>
#include <QtWebView/QWebView>
#include <utility>

namespace wealthtracker::app {

void runBankJavaScript(QWebView* view, QObject* receiver, const QString& script,
                       std::function<void(const QVariant&)> completed) {
    Q_ASSERT(receiver->thread() == qApp->thread());
    const QPointer<QObject> guard(receiver);
    view->runJavaScript(script, [guard, completed = std::move(completed)](const QVariant& value) {
        QMetaObject::invokeMethod(qApp, [guard, completed, value] {
            if (guard) completed(value);
        }, Qt::QueuedConnection);
    });
}

} // namespace wealthtracker::app
