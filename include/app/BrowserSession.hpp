#pragma once

#include <QStringList>
#include <functional>

class QWindow;

namespace wealthtracker::app {

// Persistent sessions require scoped, completed website-data removal.
#if defined(Q_OS_MACOS) || defined(Q_OS_ANDROID)
inline constexpr bool persistentBankBrowserSessions = true;
#else
inline constexpr bool persistentBankBrowserSessions = false;
#endif

#ifdef Q_OS_MACOS
void clearBankBrowserData(QStringList domains, std::function<void()> completed);
#endif

#ifdef Q_OS_ANDROID
// Modern WebView site deletion covers cookies and JavaScript-readable storage.
inline constexpr bool bankBrowserLocalStorageEnabled = true;
bool androidBankBrowserStorageSupported();
void clearAndroidBankData(QWindow* closingWindow, QStringList domains, std::function<void(bool)> completed);
void prepareAndroidBankWindow(QWindow* window);
#else
inline constexpr bool bankBrowserLocalStorageEnabled = persistentBankBrowserSessions;
#endif

} // namespace wealthtracker::app
