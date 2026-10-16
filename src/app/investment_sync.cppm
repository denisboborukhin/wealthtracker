module;

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <expected>
#include <functional>
#include <utility>

export module wt.investment_sync;

import wt.connectors;

export namespace wealthtracker::app {

// Publish one complete snapshot; a failed refresh leaves the previous one intact.
class InvestmentSyncCoordinator final : public QObject {
  public:
    using LoadCredential = std::function<std::expected<QByteArray, QString>()>;
    using Completed = std::function<void(wealthtracker::connectors::NormalizedBatch)>;
    using Failed = std::function<void(QString)>;

    InvestmentSyncCoordinator(wealthtracker::connectors::TInvestClient& client,
                              LoadCredential load, Completed completed, Failed failed,
                              QObject* parent)
        : QObject(parent), client_(client), load_(std::move(load)),
          completed_(std::move(completed)), failed_(std::move(failed)) {
        deadline_.setSingleShot(true);
        connect(&deadline_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("Превышено время синхронизации; повторите запрос"));
        });
    }

    void start() {
        observed_at_ = QDateTime::currentDateTimeUtc();
        deadline_.start(60'000);
        request(wealthtracker::connectors::TInvestReadMethod::Accounts,
                {{QStringLiteral("status"), QStringLiteral("ACCOUNT_STATUS_OPEN")}});
    }

    void cancel() {
        if (finished_) return;
        finished_ = true;
        deadline_.stop();
        if (reply_) {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort();
            reply_->deleteLater();
            reply_.clear();
        }
        deleteLater();
    }

  private:
    void fail(QString message) {
        if (finished_) return;
        cancel();
        failed_(std::move(message));
    }

    void request(wealthtracker::connectors::TInvestReadMethod method, QJsonObject body) {
        auto token = load_();
        if (!token) {
            fail(token.error());
            return;
        }
        auto result = client_.post(method, body, *token);
        token->fill('\0');
        if (!result) {
            fail(result.error().message);
            return;
        }
        reply_ = *result;
        auto* reply = reply_.data();
        reply->setReadBufferSize(maximumResponseBytes + 1);
        // Bound bytes while receiving, including chunked responses.
        response_.clear();
        connect(reply, &QNetworkReply::readyRead, this, [this] {
            if (reply_->error() != QNetworkReply::NoError) return;
            response_ += reply_->read(maximumResponseBytes + 1 - response_.size());
            if (response_.size() > maximumResponseBytes) {
                fail(QStringLiteral("Ответ Т-Инвест превышает безопасный лимит"));
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, method] {
            const auto error = reply_->error();
            const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (error == QNetworkReply::NoError) {
                response_ += reply_->read(maximumResponseBytes + 1 - response_.size());
            }
            reply_->deleteLater();
            reply_.clear();
            if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
                fail(status == 401 || status == 403
                         ? QStringLiteral("Токен отклонён; проверьте доступ к счёту и замените токен")
                         : QStringLiteral("Не удалось получить данные; проверьте сеть и повторите запрос"));
                return;
            }
            if (response_.size() > maximumResponseBytes) {
                fail(QStringLiteral("Ответ Т-Инвест превышает безопасный лимит"));
                return;
            }
            receive(method);
        });
    }

    void receive(wealthtracker::connectors::TInvestReadMethod method) {
        using namespace wealthtracker::connectors;
        const auto accountId = account_index_ < batch_.accounts.size()
            ? batch_.accounts.at(account_index_).externalId : QString{};
        auto parsed = method == TInvestReadMethod::Accounts
            ? TInvestClient::parseAccountsResponse(response_)
            : TInvestClient::parsePortfolioResponse(response_, accountId, observed_at_);
        if (!parsed) {
            fail(parsed.error().message);
            return;
        }
        if (batch_.warnings.size() + parsed->warnings.size() > 10'000 ||
            batch_.positions.size() + parsed->positions.size() > 20'000) {
            fail(QStringLiteral("Данные Т-Инвест превышают безопасный лимит"));
            return;
        }
        batch_.warnings += parsed->warnings;
        if (method == TInvestReadMethod::Accounts) {
            if (parsed->accounts.size() > 256 || !parsed->warnings.isEmpty()) {
                fail(QStringLiteral("Некорректный список инвестиционных счетов"));
                return;
            }
            QSet<QString> accounts;
            for (const auto& account : parsed->accounts) {
                if (accounts.contains(account.externalId)) {
                    fail(QStringLiteral("Т-Инвест повторил идентификатор счёта"));
                    return;
                }
                accounts.insert(account.externalId);
            }
            batch_.accounts = std::move(parsed->accounts);
            nextAccount();
        } else {
            batch_.balances += std::move(parsed->balances);
            batch_.positions += std::move(parsed->positions);
            ++account_index_;
            nextAccount();
        }
    }

    void nextAccount() {
        if (account_index_ == batch_.accounts.size()) {
            finished_ = true;
            deadline_.stop();
            completed_(std::move(batch_));
            deleteLater();
            return;
        }
        request(wealthtracker::connectors::TInvestReadMethod::Portfolio,
                {{QStringLiteral("accountId"), batch_.accounts.at(account_index_).externalId},
                 {QStringLiteral("currency"), QStringLiteral("RUB")}});
    }

    static constexpr qsizetype maximumResponseBytes = 20 * 1024 * 1024;
    wealthtracker::connectors::TInvestClient& client_;
    LoadCredential load_;
    Completed completed_;
    Failed failed_;
    wealthtracker::connectors::NormalizedBatch batch_;
    QPointer<QNetworkReply> reply_;
    QByteArray response_;
    QTimer deadline_;
    QDateTime observed_at_;
    qsizetype account_index_ = 0;
    bool finished_ = false;
};

} // namespace wealthtracker::app
