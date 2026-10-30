#include "app/BrowserSession.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QWindow>
#include <QtCore/qnativeinterface.h>

namespace {
QHash<quint64, std::function<void(bool)>> pending;
quint64 nextToken = 0;
}

extern "C" JNIEXPORT void JNICALL
Java_org_wealthtracker_android_BankBrowserStorage_completed(JNIEnv*, jclass, jlong token, jboolean success) {
    QMetaObject::invokeMethod(qApp, [token, success] {
        const auto id = static_cast<quint64>(token);
        if (!pending.contains(id)) return;
        auto callback = pending.take(id);
        callback(success == JNI_TRUE);
    }, Qt::QueuedConnection);
}

namespace wealthtracker::app {
void prepareAndroidBankWindow(QWindow* window) {
    const QPointer<QWindow> guard(window);
    const auto attach = [guard] {
        if (!guard || !guard->isVisible()) return;
        const QJniObject view(reinterpret_cast<jobject>(guard->winId()));
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([view] {
            QJniObject::callStaticMethod<void>("org/wealthtracker/android/BankBrowserClient", "attach",
                "(Landroid/view/View;)V", view.object());
        });
    };
    QObject::connect(window, &QWindow::visibleChanged, window, [attach](bool visible) {
        if (visible) attach();
    });
    attach();
}

void clearAndroidBankData(QWindow* closingWindow, QStringList domains, std::function<void(bool)> completed) {
    auto clear = [domains = std::move(domains), completed = std::move(completed)]() mutable {
    const auto token = ++nextToken;
    pending.insert(token, std::move(completed));
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([domains = std::move(domains), token] {
        QJniEnvironment environment;
        const auto stringClass = environment.findClass("java/lang/String");
        auto array = environment->NewObjectArray(static_cast<jsize>(domains.size()), stringClass, nullptr);
        bool failed = environment->ExceptionCheck();
        if (failed) environment->ExceptionClear();
        if (!failed && array) {
            for (qsizetype index = 0; index < domains.size(); ++index) {
                const auto domain = QJniObject::fromString(domains[index]);
                environment->SetObjectArrayElement(array, static_cast<jsize>(index), domain.object());
            }
            const auto type = environment.findClass("org/wealthtracker/android/BankBrowserStorage");
            const auto method = type ? environment->GetStaticMethodID(type, "clear", "([Ljava/lang/String;J)V") : nullptr;
            failed = environment->ExceptionCheck();
            if (failed) environment->ExceptionClear();
            if (method && !failed) environment->CallStaticVoidMethod(type, method, array, static_cast<jlong>(token));
            failed = failed || !method || environment->ExceptionCheck();
            if (environment->ExceptionCheck()) environment->ExceptionClear();
            environment->DeleteLocalRef(array);
        } else failed = true;
        if (failed) Java_org_wealthtracker_android_BankBrowserStorage_completed(nullptr, nullptr, static_cast<jlong>(token), JNI_FALSE);
    });
    };
    if (closingWindow) QObject::connect(closingWindow, &QObject::destroyed, qApp, std::move(clear));
    else clear();
}

bool androidBankBrowserStorageSupported() {
    return QJniObject::callStaticMethod<jboolean>("org/wealthtracker/android/BankBrowserStorage", "supported", "()Z") == JNI_TRUE;
}
}
