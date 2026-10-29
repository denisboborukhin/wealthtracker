module;

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QTimer>

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

export module wt.bank_sync;

import wt.connectors;

export namespace wealthtracker::app {

struct BankRuntime {
    wealthtracker::connectors::ILiveConnector* connector = nullptr;
    int pageSize = 50;
    std::function<wealthtracker::connectors::ConnectorResult<
        wealthtracker::connectors::NormalizedBatch>(const QByteArray&, const QDateTime&)>
        parseAccounts;
    std::function<
        wealthtracker::connectors::ConnectorResult<wealthtracker::connectors::NormalizedBatch>(
            const QByteArray&, const QString&, const QByteArray&, int)>
        parsePage;
    qsizetype maximumResponseBytes = 20 * 1024 * 1024;
    int maximumConcurrentRequests = 4;
    int historyLookbackYears = 1;
    bool synchronizeHistory = true;
    int maximumDurationMilliseconds = 0;
};

class BankSyncCoordinator final : public QObject {
  public:
    using LoadCredential = std::function<std::expected<QByteArray, QString>()>;
    using Completed = std::function<void(wealthtracker::connectors::NormalizedBatch)>;
    using Failed = std::function<void(wealthtracker::connectors::ConnectorError)>;

    BankSyncCoordinator(BankRuntime runtime, LoadCredential loadCredential, Completed completed,
                        Failed failed, QObject* parent)
        : QObject(parent), runtime_(std::move(runtime)),
          load_credential_(std::move(loadCredential)), completed_(std::move(completed)),
          failed_(std::move(failed)) {
        deadline_.setSingleShot(true);
        connect(&deadline_, &QTimer::timeout, this, [this] {
            fail(networkFailure(QStringLiteral("Синхронизация банка"), QNetworkReply::TimeoutError));
            finishIfReady();
        });
    }

    void start(QNetworkReply* discoveryReply) {
        if (started_ || discoveryReply == nullptr ||
            (runtime_.synchronizeHistory && runtime_.connector == nullptr) ||
            !runtime_.parseAccounts || (runtime_.synchronizeHistory && !runtime_.parsePage) || runtime_.pageSize <= 0 ||
            runtime_.maximumResponseBytes <= 0 || runtime_.maximumConcurrentRequests <= 0 ||
            runtime_.maximumDurationMilliseconds < 0 ||
            runtime_.maximumConcurrentRequests > maximumSupportedConcurrentRequests ||
            runtime_.historyLookbackYears <= 0 ||
            runtime_.historyLookbackYears > maximumSupportedHistoryLookbackYears) {
            fail({wealthtracker::connectors::ErrorCode::InvalidInput,
                  QStringLiteral("Некорректная конфигурация синхронизации"),
                  {},
                  false});
            finishIfReady();
            return;
        }
        started_ = true;
        if (runtime_.maximumDurationMilliseconds > 0) deadline_.start(runtime_.maximumDurationMilliseconds);
        history_to_ = QDateTime::currentDateTimeUtc();
        history_from_ = QDateTime(history_to_.date().addYears(-runtime_.historyLookbackYears),
                                  QTime(0, 0), QTimeZone::UTC);
        active_replies_.push_back(discoveryReply);
        QPointer<QNetworkReply> reply = discoveryReply;
        const auto response = collectResponse(discoveryReply);
        connect(discoveryReply, &QNetworkReply::finished, this, [this, reply, response] {
            if (!reply) {
                fail(networkFailure(QStringLiteral("Запрос счетов был отменён")));
                finishIfReady();
                return;
            }
            auto error = responseError(reply, *response);
            active_replies_.removeAll(reply);
            reply->deleteLater();
            if (error) {
                fail(std::move(*error));
                finishIfReady();
                return;
            }
            auto parsed = runtime_.parseAccounts(*response, QDateTime::currentDateTimeUtc());
            if (!parsed) {
                fail(parsed.error());
                finishIfReady();
                return;
            }
            batch_.accounts = std::move(parsed->accounts);
            batch_.balances = std::move(parsed->balances);
            batch_.warnings = std::move(parsed->warnings);
            if (batch_.accounts.isEmpty()) {
                fail({wealthtracker::connectors::ErrorCode::RemoteFailure,
                      QStringLiteral("Банк не вернул доступных счетов"),
                      {},
                      false});
                finishIfReady();
                return;
            }
            if (batch_.accounts.size() > maximumAccountsPerRun ||
                batch_.balances.size() > maximumAccountsPerRun ||
                batch_.warnings.size() > maximumWarningsPerRun) {
                fail(safetyLimit(QStringLiteral("Список счетов превысил безопасный лимит")));
                finishIfReady();
                return;
            }
            if (!runtime_.synchronizeHistory) {
                finishIfReady();
                return;
            }
            for (const auto& account : batch_.accounts) {
                pending_requests_.push_back({account.externalId, {}});
            }
            pumpRequests();
            finishIfReady();
        });
    }

    void cancel() {
        if (finished_) {
            return;
        }
        finished_ = true;
        deadline_.stop();
        for (const auto& reply : std::as_const(active_replies_)) {
            if (reply) {
                disconnect(reply, nullptr, this, nullptr);
                reply->abort();
                reply->deleteLater();
            }
        }
        active_replies_.clear();
        deleteLater();
    }

  private:
    struct PendingPage {
        QString accountId;
        QByteArray cursor;
    };

    static constexpr int maximumSupportedConcurrentRequests = 4;
    static constexpr int maximumSupportedHistoryLookbackYears = 2;
    static constexpr qsizetype maximumAccountsPerRun = 256;
    static constexpr int maximumPagesPerAccount = 250;
    static constexpr int maximumPagesPerRun = 1'000;
    static constexpr qsizetype maximumTransactionsPerRun = 250'000;
    static constexpr qsizetype maximumWarningsPerRun = 10'000;

    static wealthtracker::connectors::ConnectorError
    networkFailure(QString message,
                   const QNetworkReply::NetworkError code = QNetworkReply::OperationCanceledError) {
        switch (code) {
        case QNetworkReply::TimeoutError:
            message += QStringLiteral(": превышено время ожидания");
            break;
        case QNetworkReply::RemoteHostClosedError:
            message += QStringLiteral(": банк закрыл соединение; повторите запрос");
            break;
        case QNetworkReply::SslHandshakeFailedError:
            message += QStringLiteral(": не удалось проверить TLS-соединение");
            break;
        case QNetworkReply::HostNotFoundError:
            message += QStringLiteral(": адрес банка не найден");
            break;
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::TemporaryNetworkFailureError:
            message += QStringLiteral(": сеть временно недоступна");
            break;
        default:
            break;
        }
        return {wealthtracker::connectors::ErrorCode::NetworkFailure, std::move(message),
                QStringLiteral("network error %1").arg(static_cast<int>(code)), true};
    }

    static wealthtracker::connectors::ConnectorError safetyLimit(QString message) {
        return {
            wealthtracker::connectors::ErrorCode::SecurityViolation, std::move(message), {}, false};
    }

    std::shared_ptr<QByteArray> collectResponse(QNetworkReply* reply) {
        auto response = std::make_shared<QByteArray>();
        reply->setReadBufferSize(runtime_.maximumResponseBytes + 1);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, response] {
            if (reply->error() != QNetworkReply::NoError) {
                return;
            }
            *response += reply->read(runtime_.maximumResponseBytes + 1 - response->size());
            if (response->size() > runtime_.maximumResponseBytes) {
                fail(safetyLimit(QStringLiteral("Ответ банка превышает безопасный лимит")));
                finishIfReady();
            }
        });
        return response;
    }

    std::optional<wealthtracker::connectors::ConnectorError>
    responseError(QNetworkReply* reply, QByteArray& response) const {
        using wealthtracker::connectors::ConnectorError;
        using wealthtracker::connectors::ErrorCode;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            return ConnectorError{ErrorCode::AuthenticationFailed,
                                  QStringLiteral("Сессия или токен отклонены банком. Войдите снова."),
                                  {}, false};
        }
        if (status == 429) {
            return ConnectorError{ErrorCode::RateLimited,
                                  QStringLiteral("Банк ограничил частоту запросов. Повторите позже."),
                                  {}, true};
        }
        if (status >= 300) {
            return ConnectorError{ErrorCode::RemoteFailure,
                                  QStringLiteral("Банк вернул ошибку HTTP %1").arg(status),
                                  {}, status >= 500};
        }
        if (reply->error() != QNetworkReply::NoError) {
            return networkFailure(QStringLiteral("Не удалось получить данные банка"),
                                  reply->error());
        }
        if (status < 200) {
            return ConnectorError{ErrorCode::RemoteFailure,
                                  QStringLiteral("Банк не подтвердил успешный ответ"), {}, false};
        }
        response += reply->read(runtime_.maximumResponseBytes + 1 - response.size());
        if (response.size() > runtime_.maximumResponseBytes) {
            return safetyLimit(QStringLiteral("Ответ банка превышает безопасный лимит"));
        }
        return std::nullopt;
    }

    void requestPage(const QString& accountId, const QByteArray& cursor) {
        if (first_error_) {
            return;
        }
        const auto requestKey = accountId + QLatin1Char('\n') + QString::fromLatin1(cursor.toHex());
        if (requested_pages_.contains(requestKey)) {
            fail(safetyLimit(
                QStringLiteral("Банк повторил курсор страницы; синхронизация остановлена")));
            return;
        }
        const auto pages = pages_per_account_.value(accountId);
        if (pages >= maximumPagesPerAccount || requested_pages_.size() >= maximumPagesPerRun) {
            fail(safetyLimit(QStringLiteral("История превысила безопасный лимит страниц")));
            return;
        }
        requested_pages_.insert(requestKey);
        pages_per_account_.insert(accountId, pages + 1);

        auto credential = load_credential_();
        if (!credential) {
            fail({wealthtracker::connectors::ErrorCode::AuthenticationFailed,
                  credential.error(),
                  {},
                  false});
            return;
        }
        const wealthtracker::connectors::LiveSyncRequest syncRequest{
            .accountExternalId = accountId,
            .cursor = cursor,
            .from = history_from_,
            .to = history_to_,
            .pageSize = runtime_.pageSize,
        };
        auto requests = runtime_.connector->synchronize(syncRequest, *credential);
        credential->fill('\0');
        if (!requests) {
            fail(requests.error());
            return;
        }
        if (requests->size() != 1 || requests->front() == nullptr) {
            for (auto* reply : std::as_const(*requests)) {
                if (reply) {
                    reply->abort();
                    reply->deleteLater();
                }
            }
            fail({wealthtracker::connectors::ErrorCode::RemoteFailure,
                  QStringLiteral("Коннектор создал некорректное число запросов"),
                  {},
                  false});
            return;
        }

        for (QNetworkReply* rawReply : *requests) {
            ++pending_pages_;
            active_replies_.push_back(rawReply);
            QPointer<QNetworkReply> reply = rawReply;
            const auto response = collectResponse(rawReply);
            connect(rawReply, &QNetworkReply::finished, this,
                    [this, reply, response, accountId, cursor] {
                QByteArray nextCursor;
                if (!reply) {
                    fail(networkFailure(QStringLiteral("Запрос истории был отменён")));
                } else {
                    auto error = responseError(reply, *response);
                    active_replies_.removeAll(reply);
                    reply->deleteLater();
                    if (error) {
                        fail(std::move(*error));
                    } else {
                        auto parsed =
                            runtime_.parsePage(*response, accountId, cursor, runtime_.pageSize);
                        if (!parsed) {
                            fail(parsed.error());
                        } else if (parsed->transactions.size() >
                                       maximumTransactionsPerRun - batch_.transactions.size() ||
                                   parsed->warnings.size() >
                                       maximumWarningsPerRun - batch_.warnings.size()) {
                            fail(safetyLimit(
                                QStringLiteral("История операций превысила безопасный лимит")));
                        } else {
                            batch_.transactions += std::move(parsed->transactions);
                            batch_.warnings += std::move(parsed->warnings);
                            nextCursor = std::move(parsed->nextCursor);
                        }
                    }
                }
                --pending_pages_;
                if (!nextCursor.isEmpty() && !first_error_) {
                    pending_requests_.push_back({accountId, std::move(nextCursor)});
                }
                pumpRequests();
                finishIfReady();
            });
        }
    }

    void pumpRequests() {
        while (!first_error_ && pending_pages_ < runtime_.maximumConcurrentRequests &&
               !pending_requests_.isEmpty()) {
            auto request = pending_requests_.takeFirst();
            requestPage(request.accountId, request.cursor);
        }
    }

    void fail(wealthtracker::connectors::ConnectorError error) {
        if (!first_error_) {
            first_error_ = std::move(error);
            pending_requests_.clear();
        }
    }

    void finishIfReady() {
        if (finished_ || (!first_error_ &&
                          (pending_pages_ != 0 || !pending_requests_.isEmpty()))) {
            return;
        }
        finished_ = true;
        deadline_.stop();
        if (first_error_) {
            for (const auto& reply : std::as_const(active_replies_)) {
                if (reply) {
                    disconnect(reply, nullptr, this, nullptr);
                    reply->abort();
                    reply->deleteLater();
                }
            }
            active_replies_.clear();
            failed_(std::move(*first_error_));
        } else {
            completed_(std::move(batch_));
        }
        deleteLater();
    }

    QTimer deadline_;
    BankRuntime runtime_;
    LoadCredential load_credential_;
    Completed completed_;
    Failed failed_;
    wealthtracker::connectors::NormalizedBatch batch_;
    QHash<QString, int> pages_per_account_;
    QSet<QString> requested_pages_;
    QList<QPointer<QNetworkReply>> active_replies_;
    QList<PendingPage> pending_requests_;
    std::optional<wealthtracker::connectors::ConnectorError> first_error_;
    QDateTime history_from_;
    QDateTime history_to_;
    int pending_pages_ = 0;
    bool started_ = false;
    bool finished_ = false;
};

} // namespace wealthtracker::app
