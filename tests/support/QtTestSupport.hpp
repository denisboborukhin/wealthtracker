#pragma once

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

namespace wealthtracker::test {

class SettingsScope final {
public:
    SettingsScope(const QString& organization, const QString& application) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setApplicationName(application);
    }

    ~SettingsScope() {
        QCoreApplication::setOrganizationName(organization_);
        QCoreApplication::setApplicationName(application_);
        QSettings::setDefaultFormat(format_);
    }

    SettingsScope(const SettingsScope&) = delete;
    SettingsScope& operator=(const SettingsScope&) = delete;

    QTemporaryDir directory;

private:
    QString organization_ = QCoreApplication::organizationName();
    QString application_ = QCoreApplication::applicationName();
    QSettings::Format format_ = QSettings::defaultFormat();
};

inline bool waitFor(const std::function<bool()>& ready, const int timeoutMilliseconds) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) loop.quit();
    });
    poll.start(10);
    QTimer::singleShot(timeoutMilliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

inline bool waitUntil(const std::function<bool()>& ready, const int timeoutMilliseconds = 5000) {
    return ready() || waitFor(ready, timeoutMilliseconds);
}

} // namespace wealthtracker::test
