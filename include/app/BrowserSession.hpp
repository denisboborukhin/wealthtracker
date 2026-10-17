#pragma once

#include <QStringList>
#include <functional>

namespace wealthtracker::app {

// Persistent sessions require scoped, completed website-data removal.
#ifdef Q_OS_MACOS
inline constexpr bool persistentBankBrowserSessions = true;
void clearBankBrowserData(QStringList domains,
                          std::function<void()> completed);
#else
inline constexpr bool persistentBankBrowserSessions = false;
#endif

} // namespace wealthtracker::app
