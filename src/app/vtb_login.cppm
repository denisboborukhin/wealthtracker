module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include "app/BrowserSession.hpp"

#include <QDateTime>
#include <QDebug>
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

export module wt.vtb_login;

import wt.connectors;
import wt.vtb_browser;

export namespace wealthtracker::app {

[[nodiscard]] bool is_vtb_login_url(const QUrl& url) noexcept {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host() == QStringLiteral("online.vtb.ru") &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

enum class VtbLoginFailureKind { AuthenticationUnconfirmed, Protocol, Timeout };

struct VtbLoginFailure {
    VtbLoginFailureKind kind{VtbLoginFailureKind::Protocol};
    QString message;
};

class VtbLoginCoordinator final : public QObject {
public:
    using StatusChanged = std::function<void(QString)>;
    using Completed =
        std::function<void(wealthtracker::connectors::NormalizedBatch)>;
    using Failed = std::function<void(VtbLoginFailure)>;

    VtbLoginCoordinator(StatusChanged statusChanged, Completed completed, Failed failed,
                        QObject* parent)
        : QObject(parent), status_changed_(std::move(statusChanged)),
          completed_(std::move(completed)), failed_(std::move(failed)) {
        probe_timer_.setInterval(600);
        connect(&probe_timer_, &QTimer::timeout, this, [this] { probePage(); });
    }

    ~VtbLoginCoordinator() override { stop(false); }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool synchronizing() const noexcept {
        return active_ && mode_ == Mode::Synchronize;
    }
    [[nodiscard]] QWindow* existingWebView() const noexcept { return web_view_.get(); }

    [[nodiscard]] QWindow* webView() {
        if (!web_view_) {
            web_view_ = std::make_unique<QWebView>();
            session_ready_ = false;
            auto* settings = web_view_->settings();
            settings->setAttribute(QWebViewSettings::WebAttribute::LocalStorageEnabled,
                                   bankBrowserLocalStorageEnabled);
            settings->setAttribute(QWebViewSettings::WebAttribute::AllowFileAccess, false);
            settings->setAttribute(QWebViewSettings::WebAttribute::LocalContentCanAccessFileUrls,
                                   false);
            connect(web_view_.get(), &QWebView::loadingChanged, this,
                    [this](const QWebViewLoadingInfo& information) {
                        if (!active_) {
                            return;
                        }
                        if (!is_vtb_login_url(information.url())) {
                            fail({VtbLoginFailureKind::Protocol,
                                  QStringLiteral(
                                      "ВТБ открыл страницу вне официального домена. "
                                      "Вход остановлен.")});
                            return;
                        }
                        if (information.status() == QWebViewLoadingInfo::LoadStatus::Failed) {
                            load_succeeded_ = false;
                            if (information.errorString() == QStringLiteral("Bank page certificate verification failed")) {
                                reportStatus(QStringLiteral("Android не подтвердил сертификат банка. Защищённое соединение остановлено. Проверьте дату, сеть и доверенные сертификаты устройства."));
                                return;
                            }
                            const auto host = information.url().host().toLower();
                            const auto target = host == QStringLiteral("online.vtb.ru")
                                ? host : QStringLiteral("официальную страницу ВТБ");
                            reportStatus(QStringLiteral(
                                "Не удалось загрузить %1. Проверьте подключение, DNS, VPN "
                                "или прокси и нажмите «Повторить».").arg(target));
                            return;
                        }
                        if (information.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) {
                            load_succeeded_ = true;
                        }
                    });
        }
        return web_view_.get();
    }

    void startLogin(const QUrl& loginUrl) {
        begin(Mode::Login, loginUrl,
              QStringLiteral(
                  "Введите телефон и код на официальной странице ВТБ ниже."));
    }

    void synchronize(const QUrl& dashboardUrl) {
        begin(Mode::Synchronize, dashboardUrl,
              QStringLiteral("ВТБ: обновляем остатки по счетам…"));
    }

    void reload() {
        if (!active_ || !web_view_) {
            return;
        }
        loading_observed_elapsed_ = -1;
        load_succeeded_ = false;
        reportStatus(QStringLiteral("Повторно загружаем официальную страницу ВТБ…"));
        web_view_->reload();
    }

    void cancel() { stop(true, !persistentBankBrowserSessions && mode_ == Mode::Login); }

    void disconnectBank() {
#ifdef Q_OS_MACOS
        clearing_browser_data_ = true;
        const QPointer<VtbLoginCoordinator> guard(this);
        clearBankBrowserData({QStringLiteral("vtb.ru")}, [guard] {
            if (guard) guard->clearing_browser_data_ = false;
        });
        stop(true);
#elif defined(Q_OS_ANDROID)
        clearing_browser_data_ = true;
        const QPointer<VtbLoginCoordinator> guard(this);
        clearAndroidBankData(web_view_.get(), {QStringLiteral("vtb.ru")}, [guard](bool success) {
            if (!guard) return;
            guard->clearing_browser_data_ = !success;
            if (!success) guard->reportStatus(QStringLiteral("Не удалось удалить данные банка. Новый вход заблокирован."));
        });
        stop(true, false);
#else
        stop(true, true);
#endif
    }

private:
    enum class Mode { Login, Synchronize };
    static constexpr qint64 loginTimeoutMilliseconds = 10 * 60 * 1'000;
    static constexpr qint64 synchronizationTimeoutMilliseconds = 10 * 1'000;
    static constexpr qint64 loadingStateTimeoutMilliseconds = 30 * 1'000;
    static constexpr qint64 sessionRestorationTimeoutMilliseconds = 10 * 1'000;

    [[nodiscard]] static QString compatibilityMessage(QString message,
                                                      const QString& diagnostic) {
        if (!diagnostic.isEmpty()) {
            message += QStringLiteral(" Код совместимости: %1.").arg(diagnostic);
        }
        return message;
    }

    void begin(const Mode mode, const QUrl& url, QString status) {
        if (clearing_browser_data_) {
            fail({VtbLoginFailureKind::Protocol,
                  QStringLiteral("Удаление данных банка ещё не завершено. Повторите вход.")});
            return;
        }
        stop(false);
        if (!is_vtb_login_url(url)) {
            fail({VtbLoginFailureKind::Protocol,
                  QStringLiteral("Адрес ВТБ не прошёл проверку безопасности")});
            return;
        }
        ++generation_;
        mode_ = mode;
        active_ = true;
        load_succeeded_ = false;
        loading_observed_elapsed_ = -1;
        elapsed_.start();
        reportStatus(std::move(status));
        auto* view = static_cast<QWebView*>(webView());
        restoring_session_ = mode == Mode::Synchronize && !session_ready_;
        if (is_vtb_login_url(view->url())) {
            const auto generation = generation_;
            const QPointer<VtbLoginCoordinator> guard(this);
            view->runJavaScript(QStringLiteral(
                "delete globalThis.__wealthtrackerVtbSync; true"),
                [guard, generation](const QVariant& result) {
                    if (guard && guard->active_ && guard->generation_ == generation) {
                        guard->load_succeeded_ = result.toBool();
                    }
                });
        } else if (view->url() == url) {
            view->reload();
        } else {
            view->setUrl(url);
        }
        probe_timer_.start();
    }

    void reportStatus(QString status) {
        if (status_changed_) {
            status_changed_(std::move(status));
        }
    }

    void probePage() {
        if (!active_ || !web_view_) {
            return;
        }
        const auto timeout = mode_ == Mode::Login ? loginTimeoutMilliseconds
                                                  : synchronizationTimeoutMilliseconds;
        if (!elapsed_.isValid() || elapsed_.elapsed() >= timeout) {
            fail({mode_ == Mode::Synchronize && authentication_observed_elapsed_ >= 0
                      ? VtbLoginFailureKind::AuthenticationUnconfirmed : VtbLoginFailureKind::Timeout,
                  mode_ == Mode::Login
                      ? QStringLiteral("Время входа истекло. Начните подключение ВТБ заново.")
                      : QStringLiteral("ВТБ не завершил обновление за 10 секунд. "
                                       "Нажмите «Открыть ВТБ», чтобы проверить страницу банка.")});
            return;
        }
        if (probe_pending_ || !load_succeeded_) {
            return;
        }

        probe_pending_ = true;
        const auto generation = generation_;
        const QPointer<VtbLoginCoordinator> guard(this);
        web_view_->runJavaScript(vtb_sync_start_script(),
                                 [guard, generation](const QVariant& value) {
                                     if (!guard) {
                                         return;
                                     }
                                     if (generation != guard->generation_) {
                                         return;
                                     }
                                     if (!guard->active_) {
                                         return;
                                     }
                                     if (value.toString() == QStringLiteral("wrong-origin")) {
                                         guard->probe_pending_ = false;
                                         guard->fail({
                                             VtbLoginFailureKind::Protocol,
                                             QStringLiteral(
                                                 "ВТБ открыл страницу вне официального домена. "
                                                 "Синхронизация остановлена.")});
                                         return;
                                     }
                                     guard->web_view_->runJavaScript(
                                         vtb_financial_projection_script(),
                                         [guard, generation](const QVariant& projection) {
                                             if (!guard) {
                                                 return;
                                             }
                                             if (generation != guard->generation_) {
                                                 return;
                                             }
                                             guard->probe_pending_ = false;
                                             if (!guard->active_ ||
                                                 !projection.canConvert<QString>()) {
                                                 return;
                                             }
                                             guard->observeProjection(projection.toString());
                                         });
                                 });
    }

    void observeProjection(const QString& value) {
        auto projection =
            parse_vtb_financial_projection(value, QDateTime::currentDateTimeUtc());
        if (!projection) {
            fail({VtbLoginFailureKind::Protocol, projection.error()});
            return;
        }
        if (projection->stage == VtbProjectionStage::Authentication) {
            session_ready_ = false;
            if (loading_observed_elapsed_ >= 0 && mode_ == Mode::Login) {
                reportStatus(QStringLiteral("Введите телефон и код на официальной странице ВТБ ниже."));
            }
            loading_observed_elapsed_ = -1;
            if (mode_ == Mode::Synchronize) {
                if (restoring_session_) {
                    if (authentication_observed_elapsed_ < 0) {
                        authentication_observed_elapsed_ = elapsed_.elapsed();
                        reportStatus(QStringLiteral("ВТБ: восстанавливаем сохранённую сессию…"));
                        return;
                    }
                    if (elapsed_.elapsed() - authentication_observed_elapsed_ <
                        sessionRestorationTimeoutMilliseconds) {
                        return;
                    }
                }
                qInfo("VTB authentication unconfirmed: diagnostic=%s elapsed_ms=%lld",
                      qPrintable(projection->diagnostic),
                      static_cast<long long>(elapsed_.elapsed()));
                fail({VtbLoginFailureKind::AuthenticationUnconfirmed,
                      QStringLiteral("ВТБ не подтвердил доступ к счетам. "
                                     "Нажмите «Открыть ВТБ», чтобы проверить страницу банка.")});
            }
            return;
        }
        if (projection->stage == VtbProjectionStage::Loading) {
            if (loading_observed_elapsed_ < 0) {
                loading_observed_elapsed_ = elapsed_.elapsed();
                reportStatus(QStringLiteral(
                    "Проверяем доступ к счетам ВТБ Онлайн…"));
                return;
            }
            if (mode_ == Mode::Synchronize &&
                elapsed_.elapsed() - loading_observed_elapsed_ >
                loadingStateTimeoutMilliseconds) {
                fail({VtbLoginFailureKind::Protocol,
                      QStringLiteral(
                          "Не удалось подтвердить доступ к счетам ВТБ Онлайн. "
                          "Проверьте страницу банка и повторите вход.")});
            }
            return;
        }
        if (projection->stage == VtbProjectionStage::FrontendChanged) {
            if (projection->diagnostic == QStringLiteral("VTB-DENIED")) {
                fail({VtbLoginFailureKind::Protocol,
                      QStringLiteral("ВТБ отклонил запрос данных (HTTP 403). "
                                     "Проверьте доступ в ВТБ Онлайн.")});
                return;
            }
            fail({VtbLoginFailureKind::Protocol,
                  compatibilityMessage(
                      QStringLiteral(
                          "ВТБ Онлайн не передал счета и остатки в ожидаемом "
                          "формате. Возможно, интерфейс банка изменился."),
                      projection->diagnostic)});
            return;
        }
        if (!projection->batch) {
            fail({VtbLoginFailureKind::Protocol,
                  QStringLiteral("ВТБ Онлайн не передал данные счетов")});
            return;
        }
        auto batch = std::move(*projection->batch);
        session_ready_ = true;
        stop(false);
        if (completed_) {
            completed_(std::move(batch));
        }
    }

    void stop(const bool invalidateGeneration, const bool clearCookies = false) {
        if (invalidateGeneration) {
            ++generation_;
        }
        probe_timer_.stop();
        probe_pending_ = false;
        active_ = false;
        load_succeeded_ = false;
        loading_observed_elapsed_ = -1;
        authentication_observed_elapsed_ = -1;
        restoring_session_ = false;
        elapsed_.invalidate();
        if (web_view_ && (invalidateGeneration || clearCookies)) {
            auto* view = web_view_.release();
            QTimer::singleShot(0, view, [view, clearCookies] {
                view->stop();
                if (clearCookies) view->deleteAllCookies();
                view->deleteLater();
            });
        }
    }

    void fail(VtbLoginFailure failure) {
        if (failure.kind == VtbLoginFailureKind::AuthenticationUnconfirmed ||
            (mode_ == Mode::Synchronize && failure.kind == VtbLoginFailureKind::Timeout)) {
            ++generation_;
            stop(false);
        } else {
            stop(true, !persistentBankBrowserSessions && mode_ == Mode::Login);
        }
        if (failed_) {
            failed_(std::move(failure));
        }
    }

    StatusChanged status_changed_;
    Completed completed_;
    Failed failed_;
    std::unique_ptr<QWebView> web_view_;
    QTimer probe_timer_;
    QElapsedTimer elapsed_;
    Mode mode_{Mode::Login};
    std::uint64_t generation_ = 0;
    qint64 loading_observed_elapsed_ = -1;
    qint64 authentication_observed_elapsed_ = -1;
    bool session_ready_ = false;
    bool restoring_session_ = false;
    bool clearing_browser_data_ = false;
    bool active_ = false;
    bool load_succeeded_ = false;
    bool probe_pending_ = false;
};

} // namespace wealthtracker::app
