module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include "app/BrowserSession.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
#include <expected>
#include <functional>
#include <memory>
#include <utility>

}

export module wt.tbank_login;

import wt.tbank_auth;
import wt.tbank_browser;

export namespace wealthtracker::app {

[[nodiscard]] bool is_tbank_login_url(const QUrl& url) noexcept {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host() == QStringLiteral("www.tbank.ru") &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

[[nodiscard]] bool is_tbank_login_navigation_url(const QUrl& url) noexcept {
    const auto host = url.host();
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (host == QStringLiteral("www.tbank.ru") ||
            host == QStringLiteral("id.tbank.ru") ||
            host == QStringLiteral("id.tinkoff.ru")) &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty();
}

class TBankLoginCoordinator final : public QObject {
  public:
    using StatusChanged = std::function<void(QString)>;
    using Completed = std::function<void(QByteArray)>;
    using Failed = std::function<void(QString)>;

    TBankLoginCoordinator(StatusChanged statusChanged, Completed completed, Failed failed,
                          QObject* parent)
        : QObject(parent), status_changed_(std::move(statusChanged)),
          completed_(std::move(completed)), failed_(std::move(failed)) {
        authorization_timer_.setSingleShot(true);
        connect(&authorization_timer_, &QTimer::timeout, this, [this] { pollAuthorization(); });
        probe_timer_.setInterval(400);
        connect(&probe_timer_, &QTimer::timeout, this, [this] { probePage(); });
    }

    ~TBankLoginCoordinator() override {
        stop(false, false);
        candidate_session_.fill('\0');
    }

    [[nodiscard]] bool active() const noexcept { return active_; }

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
                        if (active_ &&
                            !is_tbank_login_navigation_url(information.url())) {
                            fail(QStringLiteral(
                                "Т-Банк открыл страницу вне официального домена. "
                                "Вход остановлен."));
                            return;
                        }
                        if (active_ &&
                            information.status() == QWebViewLoadingInfo::LoadStatus::Failed) {
                            if (information.errorString() == QStringLiteral("Bank page certificate verification failed")) {
                                reportStatus(QStringLiteral("Android не подтвердил сертификат банка. Защищённое соединение остановлено. Проверьте дату, сеть и доверенные сертификаты устройства."));
                                return;
                            }
                            reportStatus(QStringLiteral("Не удалось загрузить страницу Т-Банка. "
                                                        "Проверьте сеть и нажмите «Повторить»."));
                        }
                    });
        }
        return web_view_.get();
    }

    void start(const QUrl& loginUrl) {
        if (clearing_browser_data_) {
            fail(QStringLiteral("Удаление данных банка ещё не завершено. Повторите вход."));
            return;
        }
        stop(false, false);
        if (!is_tbank_login_url(loginUrl)) {
            fail(QStringLiteral("Адрес входа Т-Банка не прошёл проверку безопасности"));
            return;
        }
        ++generation_;
        elapsed_.start();
        active_ = true;
        reportStatus(QStringLiteral("Введите телефон и код на официальной странице Т-Банка ниже."));
        auto* view = static_cast<QWebView*>(webView());
        if (!persistentBankBrowserSessions) view->deleteAllCookies();
        view->setUrl(loginUrl);
        probe_timer_.start();
        authorization_timer_.start(1'000);
    }

    void reload() {
        if (!active_ || !web_view_) {
            return;
        }
        reportStatus(QStringLiteral("Повторно загружаем официальную страницу Т-Банка…"));
        web_view_->reload();
    }

    void cancel() { stop(true, true); }

    void disconnectBank() {
#ifdef Q_OS_MACOS
        clearing_browser_data_ = true;
        const QPointer<TBankLoginCoordinator> guard(this);
        clearBankBrowserData({QStringLiteral("tbank.ru"), QStringLiteral("tinkoff.ru")},
                             [guard] { if (guard) guard->clearing_browser_data_ = false; });
        stop(true, true);
#elif defined(Q_OS_ANDROID)
        clearing_browser_data_ = true;
        const QPointer<TBankLoginCoordinator> guard(this);
        clearAndroidBankData(web_view_.get(), {QStringLiteral("tbank.ru"), QStringLiteral("tinkoff.ru")}, [guard](bool success) {
            if (!guard) return;
            guard->clearing_browser_data_ = !success;
            if (!success) guard->reportStatus(QStringLiteral("Не удалось удалить данные банка. Новый вход заблокирован."));
        });
        stop(true, true);
#else
        cancel();
#endif
    }

  private:
    static constexpr qint64 authorizationTimeoutMilliseconds = 10 * 60 * 1'000;
    static constexpr qsizetype maximumAuthResponseBytes = 64 * 1024;

    static QNetworkRequest authRequest(const QUrl& url) {
        QNetworkRequest request(url);
        request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        request.setRawHeader(QByteArrayLiteral("User-Agent"),
                             QByteArrayLiteral("WealthTracker/0.1"));
        request.setTransferTimeout(15'000);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute,
                             maximumAuthResponseBytes);
        return request;
    }

    static std::expected<QByteArray, QString> authResponse(QNetworkReply& reply) {
        const auto status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply.error() != QNetworkReply::NoError || status != 200) {
            return std::unexpected(
                QStringLiteral("Не удалось связаться с Т-Банком (сеть %1, HTTP %2)")
                    .arg(static_cast<int>(reply.error()))
                    .arg(status));
        }
        auto body = reply.readAll();
        if (body.isEmpty() || body.size() > maximumAuthResponseBytes) {
            body.fill('\0');
            return std::unexpected(QStringLiteral("Т-Банк вернул ответ недопустимого размера"));
        }
        return body;
    }

    void reportStatus(QString status) {
        if (status_changed_) {
            status_changed_(std::move(status));
        }
    }

    void probePage() {
        if (!active_ || !web_view_ || probe_pending_) {
            return;
        }
        probe_pending_ = true;
        const auto generation = generation_;
        const QPointer<TBankLoginCoordinator> guard(this);
        web_view_->runJavaScript(tbank_session_probe_script(),
                                 [guard, generation](const QVariant& result) {
                                     if (!guard) {
                                         return;
                                     }
                                     if (generation != guard->generation_) {
                                         return;
                                     }
                                     guard->probe_pending_ = false;
                                     if (!guard->active_ ||
                                         !result.canConvert<QString>()) {
                                         return;
                                     }
                                     guard->observeProjection(result.toString());
                                 });
    }

    void observeProjection(const QString& projection) {
        const auto sessions = parse_tbank_session_projection(projection);
        if (!sessions) {
            fail(sessions.error());
            return;
        }
        if (sessions->isEmpty()) {
            return;
        }
        auto session = sessions->back();
        if (session == candidate_session_) {
            session.fill('\0');
            return;
        }
        candidate_session_.fill('\0');
        candidate_session_ = std::move(session);
        reportStatus(
            QStringLiteral("Официальная страница передала API-сессию. Проверяем доступ CLIENT…"));
        pollAuthorization();
    }

    void pollAuthorization() {
        if (!active_ || authorization_reply_) {
            return;
        }
        if (!elapsed_.isValid() || elapsed_.elapsed() > authorizationTimeoutMilliseconds) {
            fail(QStringLiteral("Время входа истекло. Начните подключение Т-Банка заново."));
            return;
        }
        if (candidate_session_.isEmpty()) {
            authorization_timer_.start(1'000);
            return;
        }
        const auto pingUrl = tbank_ping_url(candidate_session_);
        if (!pingUrl) {
            fail(pingUrl.error());
            return;
        }

        auto polledSession = candidate_session_;
        const auto generation = generation_;
        auto* reply = network_.get(authRequest(*pingUrl));
        authorization_reply_ = reply;
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, generation, polledSession = std::move(polledSession)]() mutable {
                    if (generation != generation_ || authorization_reply_ != reply) {
                        polledSession.fill('\0');
                        reply->deleteLater();
                        return;
                    }
                    authorization_reply_.clear();
                    auto response = authResponse(*reply);
                    reply->deleteLater();
                    if (polledSession != candidate_session_) {
                        polledSession.fill('\0');
                        if (response) {
                            response->fill('\0');
                        }
                        pollAuthorization();
                        return;
                    }
                    if (!response) {
                        polledSession.fill('\0');
                        reportStatus(response.error() +
                                     QStringLiteral(". Повторим проверку автоматически."));
                        authorization_timer_.start(3'000);
                        return;
                    }
                    const auto access = parse_tbank_access_level(*response);
                    response->fill('\0');
                    if (!access) {
                        polledSession.fill('\0');
                        fail(access.error());
                        return;
                    }
                    if (*access == TBankAccessLevel::anonymous) {
                        polledSession.fill('\0');
                        reportStatus(
                            QStringLiteral("Ожидаем завершения входа на странице Т-Банка…"));
                        authorization_timer_.start(2'000);
                        return;
                    }

                    stop(false, true);
                    if (completed_) {
                        completed_(std::move(polledSession));
                    } else {
                        polledSession.fill('\0');
                    }
                });
    }

    void scheduleWebViewCleanup() {
        if (!web_view_) {
            return;
        }
        auto* view = web_view_.release();
        QTimer::singleShot(0, view, [view] {
            view->stop();
            if (!persistentBankBrowserSessions) view->deleteAllCookies();
            view->deleteLater();
        });
    }

    void stop(const bool invalidateGeneration, const bool cleanupWebView) {
        probe_timer_.stop();
        probe_pending_ = false;
        if (invalidateGeneration) {
            ++generation_;
        }
        authorization_timer_.stop();
        if (authorization_reply_) {
            disconnect(authorization_reply_, nullptr, this, nullptr);
            authorization_reply_->abort();
            authorization_reply_->deleteLater();
            authorization_reply_.clear();
        }
        active_ = false;
        candidate_session_.fill('\0');
        candidate_session_.clear();
        elapsed_.invalidate();
        if (cleanupWebView) {
            scheduleWebViewCleanup();
        }
    }

    void fail(QString message) {
        stop(true, true);
        if (failed_) {
            failed_(std::move(message));
        }
    }

    StatusChanged status_changed_;
    Completed completed_;
    Failed failed_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> authorization_reply_;
    QTimer authorization_timer_;
    QElapsedTimer elapsed_;
    std::unique_ptr<QWebView> web_view_;
    QTimer probe_timer_;
    QByteArray candidate_session_;
    std::uint64_t generation_ = 0;
    bool probe_pending_ = false;
    bool clearing_browser_data_ = false;
    bool active_ = false;
};

} // namespace wealthtracker::app
