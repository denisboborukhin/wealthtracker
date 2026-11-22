module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>
}

export module wt.wallet_service;
import wt.crypto_wallet;
import wt.history_store;
import wt.storage;

export namespace wealthtracker::app {

class WalletService final : public QObject {
public:
    using Changed = std::function<void()>;

    WalletService(QNetworkAccessManager& network, HistoryStore& history)
        : network_(network), history_(history) {}
    ~WalletService() override { cancelPending(); }

    void setNotifications(Changed changed, Changed historyChanged) {
        changed_ = std::move(changed);
        history_changed_ = std::move(historyChanged);
    }

    // Quiet cancellation is used during controller destruction, without UI signals.
    void cancelPending() {
        if (sync_) sync_->cancel();
    }

    QVariantList rows() const {
        QVariantList rows;
        for (const auto& wallet : history_.wallets()) {
            const auto address = QString::fromStdString(wallet.address);
            auto row = states_.value(address);
            row.insert(QStringLiteral("id"), address);
            row.insert(QStringLiteral("address"), address);
            row.insert(QStringLiteral("name"), QString::fromStdString(wallet.name));
            if (!row.contains(QStringLiteral("holdings"))) row.insert(QStringLiteral("holdings"), QVariantList{});
            if (!row.contains(QStringLiteral("value"))) {
                row.insert(QStringLiteral("value"), QStringLiteral("—"));
                row.insert(QStringLiteral("unpricedCount"), 0);
                for (const auto& capture : history_.captures()) {
                    if (capture.connection_id != "ethereum:" + wallet.address) continue;
                    for (const auto& balance : capture.balances) {
                        if (balance.currency != "USDT") continue;
                        row.insert(QStringLiteral("value"), usdt_label(balance.amount_minor));
                        row.insert(QStringLiteral("lastSync"), QDateTime::fromString(QString::fromStdString(capture.captured_at), Qt::ISODateWithMs)
                            .toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
                        if (!row.contains(QStringLiteral("status"))) row.insert(QStringLiteral("status"),
                            QStringLiteral("Оценка из истории. Обновите для получения списка токенов."));
                    }
                }
            }
            rows.push_back(row);
        }
        return rows;
    }

    QString status() const { return status_; }
    bool running() const { return running_; }

    bool add(const QString& name, const QString& value) {
        if (running_) return false;
        const auto address = value.trimmed().toLower();
        const auto label = name.trimmed();
        if (!valid_ethereum_address(address) || label.isEmpty() || label.toUtf8().size() > 80 || label.contains(QChar::Null)) {
            status_ = QStringLiteral("Укажите короткое название и публичный EVM-адрес: 0x и 40 шестнадцатеричных символов.");
            notifyChanged();
            return false;
        }
        if (!history_.ready()) {
            status_ = QStringLiteral("Защищённое хранилище недоступно. Кошелёк не сохранён.");
            notifyChanged();
            return false;
        }
        if (!history_.saveWallet(address, label)) {
            status_ = QStringLiteral("Не удалось сохранить кошелёк.");
            notifyChanged();
            return false;
        }
        status_ = QStringLiteral("Кошелёк сохранён. Обнаруживаем сети Rabby…");
        notifyChanged();
        return true;
    }

    bool remove(const QString& address) {
        if (running_ || !history_.ready()) return false;
        if (!history_.removeWallet(address)) {
            status_ = QStringLiteral("Не удалось удалить подключение кошелька.");
            notifyChanged();
            return false;
        }
        states_.remove(address);
        status_ = QStringLiteral("Подключение кошелька удалено. История сохранена.");
        notifyChanged();
        return true;
    }

    bool refresh() {
        if (running_) return false;
        if (history_.wallets().empty()) {
            status_ = QStringLiteral("Добавьте публичный EVM-адрес кошелька.");
            notifyChanged();
            return false;
        }
        pending_.clear();
        for (const auto& wallet : history_.wallets()) pending_.push_back(QString::fromStdString(wallet.address));
        running_ = true;
        failures_ = 0;
        status_ = QStringLiteral("Обновляем кошельки в сетях EVM…");
        notifyChanged();
        startNext();
        return true;
    }

    void stop() {
        if (!running_) return;
        pending_.clear();
        if (sync_) {
            sync_->cancel();
            sync_->deleteLater();
            sync_.clear();
        }
        running_ = false;
        status_ = QStringLiteral("Обновление кошельков остановлено. Предыдущие данные сохранены.");
        notifyChanged();
    }

private:
    void startNext() {
        if (!running_ || sync_) return;
        if (pending_.isEmpty()) {
            running_ = false;
            status_ = failures_ == 0
                ? QStringLiteral("Кошельки обновлены. Автообновление — каждые 6 часов, пока приложение открыто.")
                : QStringLiteral("Не удалось обновить кошельков: %1. Предыдущие данные сохранены.").arg(failures_);
            notifyChanged();
            return;
        }
        const auto address = pending_.takeFirst();
        const auto advance = [this] {
            if (sync_) sync_->deleteLater();
            sync_.clear();
            notifyChanged();
            QTimer::singleShot(0, this, [this] { startNext(); });
        };
        auto* coordinator = new CryptoWalletSync(network_,
            [this, address, advance](CryptoPortfolio portfolio) {
                const auto now = QDateTime::currentDateTimeUtc();
                auto& state = states_[address];
                state = {{QStringLiteral("value"), usdt_label(portfolio.usdtMinor)},
                    {QStringLiteral("holdings"), portfolio.holdings}, {QStringLiteral("unpricedCount"), portfolio.unpricedCount},
                    {QStringLiteral("supportedChains"), portfolio.supportedChains}, {QStringLiteral("networks"), portfolio.usedChains},
                    {QStringLiteral("lastSync"), now.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))},
                    {QStringLiteral("status"), QStringLiteral("Оценённая часть активов по данным Rabby.")}};
                const auto wallet = std::find_if(history_.wallets().begin(), history_.wallets().end(),
                    [&](const auto& item) { return item.address == address.toStdString(); });
                if (wallet != history_.wallets().end() && history_.ready()) {
                    const auto connection = "ethereum:" + wallet->address;
                    const auto institution = "EVM · " + wallet->name;
                    std::vector<storage::HistoryBalance> balances{{wallet->address, wallet->name + " · оценённые активы", "USDT", portfolio.usdtMinor}};
                    const auto capturedAt = now.toString(Qt::ISODateWithMs).toStdString();
                    if (!history_.append(connection, institution, capturedAt, std::move(balances))) {
                        state[QStringLiteral("status")] = QStringLiteral("Оценка обновлена, но историю сохранить не удалось.");
                        ++failures_;
                    }
                    notifyHistoryChanged();
                }
                advance();
            }, [this, address, advance](QString error) {
                states_[address][QStringLiteral("status")] = std::move(error);
                ++failures_;
                advance();
            }, this);
        sync_ = coordinator;
        coordinator->start(address);
    }

    void notifyChanged() { if (changed_) changed_(); }
    void notifyHistoryChanged() { if (history_changed_) history_changed_(); }

    QNetworkAccessManager& network_;
    HistoryStore& history_;
    Changed changed_;
    Changed history_changed_;
    QHash<QString, QVariantMap> states_;
    QPointer<CryptoWalletSync> sync_;
    QStringList pending_;
    QString status_;
    bool running_ = false;
    int failures_ = 0;
};

} // namespace wealthtracker::app
