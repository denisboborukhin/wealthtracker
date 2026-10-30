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

export module wt.alfa_login;

import wt.alfa_browser;
import wt.connectors;

export namespace wealthtracker::app {

enum class AlfaLoginFailureKind { AuthenticationUnconfirmed, Protocol, Timeout };

struct AlfaLoginFailure {
    AlfaLoginFailureKind kind{AlfaLoginFailureKind::Protocol};
    QString message;
};

enum class AlfaLoadFailureKind { CertificateTrust, Network };

struct AlfaLoadFailure {
    AlfaLoadFailureKind kind{AlfaLoadFailureKind::Network};
    QString message;
    bool certificateHelpRequired = false;
};

[[nodiscard]] QUrl alfa_certificate_instructions_url() {
    return QUrl{QStringLiteral("https://alfa.bank/cert")};
}

[[nodiscard]] bool is_alfa_login_url(const QUrl& url) noexcept {
    const auto host = url.host().toLower();
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (host == QStringLiteral("web.alfabank.ru") ||
            host == QStringLiteral("private.auth.alfabank.ru")) &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

[[nodiscard]] AlfaLoadFailure
classify_alfa_load_failure(const QUrl& failedUrl, const QString& errorString) {
    const auto host = failedUrl.host().toLower();
    const bool knownHost =
        host == QStringLiteral("web.alfabank.ru") ||
        host == QStringLiteral("private.auth.alfabank.ru");
    const auto target = knownHost
        ? host
        : QStringLiteral("официальную страницу Альфа-Банка");
    const auto foldedError = errorString.toCaseFolded();
    const bool certificateFailure =
        foldedError.contains(QStringLiteral("certificate")) ||
        foldedError.contains(QStringLiteral("сертифик")) ||
        foldedError.contains(QStringLiteral("server trust")) ||
        foldedError.contains(QStringLiteral("servertrust")) ||
        foldedError.contains(QStringLiteral("err_cert_")) ||
        foldedError.contains(QStringLiteral("-1202"));

    if (certificateFailure) {
        return {
            AlfaLoadFailureKind::CertificateTrust,
            QStringLiteral(
                "Не удалось проверить сертификат %1. Альфа-Онлайн использует "
                "российские сертификаты Минцифры. Установите их по официальной "
                "инструкции Альфа-Банка, перезапустите WealthTracker и повторите "
                "вход. Проверка TLS не отключена.")
                .arg(target),
            true};
    }

    return {
        AlfaLoadFailureKind::Network,
        QStringLiteral(
            "Не удалось загрузить %1. Проверьте подключение, DNS, VPN или прокси "
            "и нажмите «Повторить».")
            .arg(target),
        false};
}

class AlfaLoginCoordinator final : public QObject {
public:
    using StatusChanged = std::function<void(QString)>;
    using Completed =
        std::function<void(wealthtracker::connectors::NormalizedBatch)>;
    using Failed = std::function<void(AlfaLoginFailure)>;

    AlfaLoginCoordinator(StatusChanged statusChanged, Completed completed, Failed failed,
                         QObject* parent)
        : QObject(parent), status_changed_(std::move(statusChanged)),
          completed_(std::move(completed)), failed_(std::move(failed)) {
        probe_timer_.setInterval(500);
        connect(&probe_timer_, &QTimer::timeout, this, [this] { probePage(); });
    }

    ~AlfaLoginCoordinator() override { stop(false); }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool synchronizing() const noexcept {
        return active_ && mode_ == Mode::Synchronize;
    }
    [[nodiscard]] bool certificateHelpRequired() const noexcept {
        return certificate_help_required_;
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
                        if (!active_) {
                            return;
                        }
                        if (!is_alfa_login_url(information.url())) {
                            fail({AlfaLoginFailureKind::Protocol,
                                  QStringLiteral(
                                      "Альфа-Банк открыл страницу вне официального домена. "
                                      "Вход остановлен.")});
                            return;
                        }
                        if (information.status() == QWebViewLoadingInfo::LoadStatus::Failed) {
                            load_succeeded_ = false;
                            if (information.errorString() == QStringLiteral("Bank page certificate verification failed")) {
                                reportStatus(QStringLiteral("Android не подтвердил сертификат банка. Защищённое соединение остановлено. Проверьте дату, сеть и доверенные сертификаты устройства."));
                                return;
                            }
                            auto failure = classify_alfa_load_failure(
                                information.url(), information.errorString());
                            certificate_help_required_ =
                                failure.certificateHelpRequired;
                            reportStatus(std::move(failure.message));
                            return;
                        }
                        if (information.status() == QWebViewLoadingInfo::LoadStatus::Succeeded) {
                            load_succeeded_ = true;
                            certificate_help_required_ = false;
                        }
                    });
        }
        return web_view_.get();
    }

    void startLogin(const QUrl& loginUrl) {
        begin(Mode::Login, loginUrl,
              QStringLiteral(
                  "Введите телефон и код на официальной странице Альфа-Банка ниже."));
    }

    void synchronize(const QUrl& dashboardUrl) {
        begin(Mode::Synchronize, dashboardUrl,
              QStringLiteral("Альфа-Банк: обновляем остатки по счетам…"));
    }

    void reload() {
        if (!active_ || !web_view_) {
            return;
        }
        ++generation_;
        probe_pending_ = false;
        load_succeeded_ = false;
        dashboard_observed_elapsed_ = -1;
        elapsed_.restart();
        certificate_help_required_ = false;
        reportStatus(QStringLiteral("Повторно загружаем официальную страницу Альфа-Банка…"));
        web_view_->reload();
    }

    void cancel() { stop(true); }

    void disconnectBank() {
#ifdef Q_OS_MACOS
        clearing_browser_data_ = true;
        const QPointer<AlfaLoginCoordinator> guard(this);
        clearBankBrowserData({QStringLiteral("alfabank.ru")}, [guard] {
            if (guard) guard->clearing_browser_data_ = false;
        });
        stop(true);
#elif defined(Q_OS_ANDROID)
        clearing_browser_data_ = true;
        const QPointer<AlfaLoginCoordinator> guard(this);
        clearAndroidBankData(web_view_.get(), {QStringLiteral("alfabank.ru")}, [guard](bool success) {
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
    static constexpr qint64 dashboardStateTimeoutMilliseconds = 30 * 1'000;

    void begin(const Mode mode, const QUrl& url, QString status) {
        if (clearing_browser_data_) {
            fail({AlfaLoginFailureKind::Protocol,
                  QStringLiteral("Удаление данных банка ещё не завершено. Повторите вход.")});
            return;
        }
        stop(false);
        if (!is_alfa_login_url(url)) {
            fail({AlfaLoginFailureKind::Protocol,
                  QStringLiteral("Адрес Альфа-Банка не прошёл проверку безопасности")});
            return;
        }
        ++generation_;
        mode_ = mode;
        active_ = true;
        load_succeeded_ = false;
        certificate_help_required_ = false;
        dashboard_observed_elapsed_ = -1;
        elapsed_.start();
        reportStatus(std::move(status));
        auto* view = static_cast<QWebView*>(webView());
        if (mode == Mode::Login && is_alfa_login_url(view->url())) {
            load_succeeded_ = !view->isLoading();
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
            if (mode_ == Mode::Synchronize &&
                web_view_->url().host() == QStringLiteral("private.auth.alfabank.ru")) {
                fail({AlfaLoginFailureKind::AuthenticationUnconfirmed,
                      QStringLiteral("Альфа-Банк не завершил восстановление доступа. "
                                     "Нажмите «Открыть банк», чтобы проверить страницу банка.")});
                return;
            }
            fail({AlfaLoginFailureKind::Timeout,
                  mode_ == Mode::Login
                      ? QStringLiteral(
                            "Время входа истекло. Начните подключение Альфа-Банка заново.")
                      : QStringLiteral(
                            "Альфа-Банк не завершил обновление за 10 секунд. "
                            "Нажмите «Открыть банк», чтобы проверить страницу банка.")});
            return;
        }
        if (!load_succeeded_ || probe_pending_) {
            return;
        }

        probe_pending_ = true;
        const auto generation = generation_;
        const QPointer<AlfaLoginCoordinator> guard(this);
        web_view_->runJavaScript(alfa_financial_projection_script(),
                                 [guard, generation](const QVariant& value) {
                                     if (!guard) {
                                         return;
                                     }
                                     if (generation != guard->generation_) {
                                         return;
                                     }
                                     guard->probe_pending_ = false;
                                     if (!guard->active_ ||
                                         !value.canConvert<QString>()) {
                                         return;
                                     }
                                     guard->observeProjection(value.toString());
                                 });
    }

    void observeProjection(const QString& value) {
        auto projection =
            parse_alfa_financial_projection(value, QDateTime::currentDateTimeUtc());
        if (!projection) {
            fail({AlfaLoginFailureKind::Protocol, projection.error()});
            return;
        }
        if (projection->stage == AlfaProjectionStage::Authentication) {
            dashboard_observed_elapsed_ = -1;
            return;
        }
        if (projection->stage == AlfaProjectionStage::CertificateRequired) {
            certificate_help_required_ = true;
            load_succeeded_ = false;
            reportStatus(QStringLiteral(
                "Альфа-Онлайн открыл страницу установки российских "
                "сертификатов. Установите сертификаты по официальной "
                "инструкции банка, перезапустите WealthTracker и повторите вход. "
                "Проверка TLS не отключена."));
            return;
        }
        if (projection->stage == AlfaProjectionStage::DashboardLoading) {
            if (dashboard_observed_elapsed_ < 0) {
                dashboard_observed_elapsed_ = elapsed_.elapsed();
                reportStatus(QStringLiteral(
                    "Альфа-Онлайн загружает данные. Доступ к счетам ещё не подтверждён…"));
                return;
            }
            if (mode_ == Mode::Synchronize &&
                elapsed_.elapsed() - dashboard_observed_elapsed_ >
                dashboardStateTimeoutMilliseconds) {
                fail({AlfaLoginFailureKind::Protocol,
                      QStringLiteral(
                          "Не удалось подтвердить доступ к счетам Альфа-Онлайн: "
                          "банк не передал данные в поддерживаемом формате. "
                          "Проверьте страницу банка.")});
            }
            return;
        }
        if (!projection->batch) {
            fail({AlfaLoginFailureKind::Protocol,
                  QStringLiteral("Alfa Online не передал данные счетов")});
            return;
        }
        auto batch = std::move(*projection->batch);
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
        dashboard_observed_elapsed_ = -1;
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

    void fail(AlfaLoginFailure failure) {
        if (failure.kind == AlfaLoginFailureKind::AuthenticationUnconfirmed ||
            (mode_ == Mode::Synchronize && failure.kind == AlfaLoginFailureKind::Timeout)) {
            ++generation_;
            stop(false);
        } else {
            stop(true);
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
    std::uint64_t generation_ = 0;
    qint64 dashboard_observed_elapsed_ = -1;
    Mode mode_{Mode::Login};
    bool probe_pending_ = false;
    bool load_succeeded_ = false;
    bool certificate_help_required_ = false;
    bool clearing_browser_data_ = false;
    bool active_ = false;
};

} // namespace wealthtracker::app
