module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include "app/BrowserSession.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWindow>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>
#include <QtWebView/QWebViewSettings>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

}

export module wt.ozon_login;

import wt.connectors;
import wt.ozon_browser;

export namespace wealthtracker::app {

[[nodiscard]] bool is_ozon_login_url(const QUrl& url) noexcept {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host() == QStringLiteral("finance.ozon.ru") &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

[[nodiscard]] bool is_ozon_login_navigation_url(const QUrl& url) noexcept {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (url.host() == QStringLiteral("finance.ozon.ru") ||
            url.host() == QStringLiteral("sso.ozon.ru")) &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

enum class OzonLoginFailureKind { AuthenticationUnconfirmed, Protocol, Timeout };

struct OzonLoginFailure {
    OzonLoginFailureKind kind{OzonLoginFailureKind::Protocol};
    QString message;
};

class OzonLoginCoordinator final : public QObject {
public:
    using StatusChanged = std::function<void(QString)>;
    using Completed = std::function<void(wealthtracker::connectors::NormalizedBatch)>;
    using Failed = std::function<void(OzonLoginFailure)>;

    OzonLoginCoordinator(StatusChanged statusChanged, Completed completed, Failed failed,
                         QObject* parent)
        : QObject(parent), status_changed_(std::move(statusChanged)),
          completed_(std::move(completed)), failed_(std::move(failed)) {
        probe_timer_.setInterval(600);
        connect(&probe_timer_, &QTimer::timeout, this, [this] { probePage(); });
    }

    ~OzonLoginCoordinator() override { stop(true); }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool synchronizing() const noexcept {
        return active_ && mode_ == Mode::Synchronize;
    }
    [[nodiscard]] QWindow* existingWebView() const noexcept { return web_view_.get(); }

    [[nodiscard]] QWindow* webView() {
        if (!web_view_) {
            web_view_ = std::make_unique<QWebView>();
            auto* settings = web_view_->settings();
            settings->setAttribute(QWebViewSettings::WebAttribute::LocalStorageEnabled,
                                   bankBrowserLocalStorageEnabled);
            settings->setAttribute(QWebViewSettings::WebAttribute::AllowFileAccess, false);
            settings->setAttribute(QWebViewSettings::WebAttribute::LocalContentCanAccessFileUrls,
                                   false);
            connect(web_view_.get(), &QWebView::loadingChanged, this,
                    [this](const QWebViewLoadingInfo& information) {
                        if (!active_) return;
                        if (!is_ozon_login_navigation_url(information.url())) {
                            const auto target = information.url().scheme() + QStringLiteral("://")
                                + information.url().host().left(253);
                            fail({OzonLoginFailureKind::Protocol,
                                  QStringLiteral("Ozon Банк открыл страницу вне официального домена. "
                                                 "Вход остановлен (%1).").arg(target)});
                            return;
                        }
                        if (information.status() == QWebViewLoadingInfo::LoadStatus::Failed) {
                            load_succeeded_ = false;
                            if (information.errorString() == QStringLiteral("Bank page certificate verification failed")) {
                                reportStatus(QStringLiteral("Не удалось подтвердить сертификат Ozon Банка. "
                                                            "Защищённое соединение остановлено."));
                            } else {
                                reportStatus(QStringLiteral("Не удалось загрузить страницу Ozon Банка. "
                                                            "Проверьте сеть и нажмите «Повторить»."));
                            }
                        } else if (information.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) {
                            load_succeeded_ = true;
                        }
                    });
        }
        return web_view_.get();
    }

    void startLogin(const QUrl& loginUrl) {
        begin(Mode::Login, loginUrl,
              QStringLiteral("Войдите в Ozon Банк на официальной странице ниже."));
    }

    void synchronize(const QUrl& dashboardUrl) {
        begin(Mode::Synchronize, dashboardUrl,
              QStringLiteral("Ozon Банк: обновляем остатки по счетам…"));
    }

    void reload() {
        if (!active_ || !web_view_) return;
        ++generation_;
        probe_pending_ = false;
        load_succeeded_ = false;
        authentication_observed_ = false;
        waiting_observed_ = false;
        cancelPageRead();
        reportStatus(QStringLiteral("Повторно загружаем официальную страницу Ozon Банка…"));
        web_view_->reload();
    }

    void cancel() { stop(true, !persistentBankBrowserSessions && mode_ == Mode::Login); }

    void disconnectBank() {
#ifdef Q_OS_MACOS
        clearing_browser_data_ = true;
        const QPointer<OzonLoginCoordinator> guard(this);
        // WebKit groups the finance and SSO origins by registrable site.
        clearBankBrowserData({QStringLiteral("ozon.ru")}, [guard] {
            if (guard) guard->clearing_browser_data_ = false;
        });
        stop(true);
#elif defined(Q_OS_ANDROID)
        clearing_browser_data_ = true;
        const QPointer<OzonLoginCoordinator> guard(this);
        clearAndroidBankData(web_view_.get(), {QStringLiteral("finance.ozon.ru"), QStringLiteral("sso.ozon.ru")}, [guard](bool success) {
            if (!guard) return;
            guard->clearing_browser_data_ = !success;
            if (!success) guard->reportStatus(QStringLiteral("Не удалось удалить данные Ozon Банка. "
                                                             "Новый вход заблокирован."));
        });
        stop(true);
#else
        stop(true, true);
#endif
    }

private:
    enum class Mode { Login, Synchronize };
    static constexpr qint64 loginTimeoutMilliseconds = 10 * 60 * 1'000;
    static constexpr qint64 synchronizationTimeoutMilliseconds = 10 * 1'000;

    [[nodiscard]] static const QString& cancelScript() {
        static const QString script = QStringLiteral(R"JS((() => {
  const url = new URL(globalThis.location.href)
  if (url.protocol !== 'https:' || url.hostname !== 'finance.ozon.ru' ||
      (url.port !== '' && url.port !== '443') || url.username || url.password) return false
  const state = globalThis.__wealthtrackerOzonSync
  if (state) {
    state.cancelled = true
    state.controller?.abort()
  }
  delete globalThis.__wealthtrackerOzonSync
  return true
})())JS");
        return script;
    }

    void cancelPageRead() {
        if (web_view_ && is_ozon_login_url(web_view_->url())) {
            runBankJavaScript(web_view_.get(), this, cancelScript(), [](const QVariant&) {});
        }
    }

    void begin(const Mode mode, const QUrl& url, QString status) {
        if (clearing_browser_data_) {
            fail({OzonLoginFailureKind::Protocol,
                  QStringLiteral("Удаление данных Ozon Банка ещё не завершено. Повторите вход.")});
            return;
        }
        stop(false);
        if (!is_ozon_login_url(url)) {
            fail({OzonLoginFailureKind::Protocol,
                  QStringLiteral("Адрес Ozon Банка не прошёл проверку безопасности")});
            return;
        }
        ++generation_;
        mode_ = mode;
        active_ = true;
        elapsed_.start();
        reportStatus(std::move(status));
        auto* view = static_cast<QWebView*>(webView());
        if (is_ozon_login_url(view->url())) {
            const auto generation = generation_;
            const QPointer<OzonLoginCoordinator> guard(this);
            runBankJavaScript(view, this, cancelScript(), [guard, generation](const QVariant& result) {
                if (guard && guard->active_ && guard->generation_ == generation) {
                    guard->load_succeeded_ = result.toBool();
                }
            });
        } else {
            if (!persistentBankBrowserSessions) view->deleteAllCookies();
            view->setUrl(url);
        }
        probe_timer_.start();
    }

    void reportStatus(QString status) {
        if (status_changed_) status_changed_(std::move(status));
    }

    void probePage() {
        if (!active_ || !web_view_) return;
        const auto timeout = mode_ == Mode::Login ? loginTimeoutMilliseconds
                                                  : synchronizationTimeoutMilliseconds;
        if (!elapsed_.isValid() || elapsed_.elapsed() >= timeout) {
            fail({mode_ == Mode::Synchronize && authentication_observed_
                      ? OzonLoginFailureKind::AuthenticationUnconfirmed : OzonLoginFailureKind::Timeout,
                  mode_ == Mode::Login
                      ? QStringLiteral("Время входа истекло. Начните подключение Ozon Банка заново.")
                      : QStringLiteral("Ozon Банк не завершил обновление за 10 секунд. "
                                       "Откройте страницу банка, чтобы проверить доступ.")});
            return;
        }
        if (probe_pending_ || !load_succeeded_) return;
        if (is_ozon_login_navigation_url(web_view_->url()) && !is_ozon_login_url(web_view_->url())) {
            if (!authentication_observed_ || waiting_observed_) {
                reportStatus(QStringLiteral("Завершите вход на официальной странице Ozon ID."));
            }
            authentication_observed_ = true;
            waiting_observed_ = false;
            return;
        }
        probe_pending_ = true;
        const auto generation = generation_;
        const QPointer<OzonLoginCoordinator> guard(this);
        runBankJavaScript(web_view_.get(), this, ozon_sync_start_script(),
                          [guard, generation](const QVariant& value) {
            if (!guard || !guard->active_ || guard->generation_ != generation) return;
            if (value.toString() == QStringLiteral("wrong-origin")) {
                guard->probe_pending_ = false;
                if (guard->web_view_ && is_ozon_login_navigation_url(guard->web_view_->url()) &&
                    !is_ozon_login_url(guard->web_view_->url())) return;
                guard->fail({OzonLoginFailureKind::Protocol,
                             QStringLiteral("Ozon Банк открыл страницу вне официального домена. "
                                            "Синхронизация остановлена.")});
                return;
            }
            runBankJavaScript(guard->web_view_.get(), guard, ozon_financial_projection_script(),
                              [guard, generation](const QVariant& projection) {
                if (!guard || guard->generation_ != generation) return;
                guard->probe_pending_ = false;
                if (guard->active_ && projection.canConvert<QString>()) {
                    guard->observeProjection(projection.toString());
                }
            });
        });
    }

    void observeProjection(const QString& value) {
        auto projection = parse_ozon_financial_projection(value, QDateTime::currentDateTimeUtc());
        if (!projection) {
            fail({OzonLoginFailureKind::Protocol, projection.error()});
            return;
        }
        if (projection->stage == OzonProjectionStage::AuthenticationRequired) {
            if (!authentication_observed_ || waiting_observed_) {
                reportStatus(mode_ == Mode::Login
                    ? QStringLiteral("Войдите в Ozon Банк на официальной странице ниже.")
                    : QStringLiteral("Ozon Банк: ожидаем подтверждения доступа к счетам…"));
            }
            authentication_observed_ = true;
            waiting_observed_ = false;
            return;
        }
        if (projection->stage == OzonProjectionStage::Waiting) {
            if (!waiting_observed_ || projection->reasonCode != waiting_reason_) {
                reportStatus(projection->reasonCode.startsWith(QStringLiteral("OZON-CREDIT-"))
                    ? QStringLiteral("Проверяем рассрочку Ozon Банка (%1)…").arg(projection->reasonCode)
                    : projection->reasonCode == QStringLiteral("OZON-READ")
                    ? QStringLiteral("Проверяем ответ счетов Ozon Банка (OZON-READ)…")
                    : projection->reasonCode == QStringLiteral("OZON-MODULE")
                        ? QStringLiteral("Ожидаем модуль счетов Ozon Банка (OZON-MODULE)…")
                        : QStringLiteral("Проверяем доступ к счетам Ozon Банка…"));
            }
            waiting_reason_ = projection->reasonCode;
            waiting_observed_ = true;
            return;
        }
        if (projection->stage != OzonProjectionStage::Ready || !projection->batch) {
            fail({OzonLoginFailureKind::Protocol,
                  QStringLiteral("Ozon Банк не передал проверенные данные счетов. "
                                 "Возможно, интерфейс банка изменился (%1).")
                      .arg(projection->reasonCode.isEmpty() ? QStringLiteral("OZON-SCHEMA") : projection->reasonCode)});
            return;
        }
        auto batch = std::move(*projection->batch);
        stop(false);
        if (completed_) completed_(std::move(batch));
    }

    void stop(const bool invalidateGeneration, const bool clearCookies = false) {
        if (invalidateGeneration) ++generation_;
        probe_timer_.stop();
        probe_pending_ = false;
        active_ = false;
        load_succeeded_ = false;
        authentication_observed_ = false;
        waiting_observed_ = false;
        elapsed_.invalidate();
        cancelPageRead();
        if (web_view_ && (invalidateGeneration || clearCookies)) {
            auto* view = web_view_.release();
            QTimer::singleShot(0, view, [view, clearCookies] {
                view->stop();
                if (clearCookies) view->deleteAllCookies();
                view->deleteLater();
            });
        }
    }

    void fail(OzonLoginFailure failure) {
        if (failure.kind == OzonLoginFailureKind::AuthenticationUnconfirmed ||
            (mode_ == Mode::Synchronize && failure.kind == OzonLoginFailureKind::Timeout)) {
            ++generation_;
            stop(false);
        } else {
            stop(true, !persistentBankBrowserSessions && mode_ == Mode::Login);
        }
        if (failed_) failed_(std::move(failure));
    }

    StatusChanged status_changed_;
    Completed completed_;
    Failed failed_;
    std::unique_ptr<QWebView> web_view_;
    QTimer probe_timer_;
    QElapsedTimer elapsed_;
    Mode mode_{Mode::Login};
    std::uint64_t generation_ = 0;
    bool clearing_browser_data_ = false;
    bool active_ = false;
    bool load_succeeded_ = false;
    bool probe_pending_ = false;
    bool authentication_observed_ = false;
    bool waiting_observed_ = false;
    QString waiting_reason_;
};

} // namespace wealthtracker::app
