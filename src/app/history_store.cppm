module;
extern "C++" {
#include "app/CredentialBackend.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>
}
export module wt.history_store;
import wt.storage;
import wt.platform;
import wt.connectors;

export namespace wealthtracker::app {
class HistoryStore final {
public:
    void initialize(const QString& path, CredentialBackend* credentials,
                    const std::function<void(const wealthtracker::storage::CashbackSnapshot&)>& restoreCashback) {
        // Injected controllers opt in with an explicit path; tests never open the user's vault.
        if (path.isEmpty()) return;
        error_ = QStringLiteral("История недоступна: не удалось открыть защищённое хранилище.");
        if (!credentials || !credentials->supported()) return;
        const auto keyName = QStringLiteral("balance-history-key");
        auto loaded = credentials->get(keyName);
        if (!loaded) return;
        if (!*loaded) {
            if (QFileInfo::exists(path)) {
                error_ = QStringLiteral("История недоступна: ключ сохранённой базы не найден.");
                return;
            }
            auto generated = wealthtracker::platform::VaultCrypto::generate_database_key();
            if (!generated) return;
            QByteArray key(reinterpret_cast<const char*>(generated->data()),
                           static_cast<qsizetype>(generated->size()));
            if (!credentials->put(keyName, key)) {
                key.fill('\0');
                return;
            }
            *loaded = std::move(key);
        }
        auto& key = **loaded;
        if (key.size() != 32 || !QDir{}.mkpath(QFileInfo(path).absolutePath())) {
            key.fill('\0');
            return;
        }
        auto database = wealthtracker::storage::Database::open(
            QFileInfo(path).filesystemFilePath(),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.constData()),
                                       static_cast<std::size_t>(key.size())));
        key.fill('\0');
        if (!database) return;
        wealthtracker::storage::BalanceHistoryRepository repository(*database);
        if (!repository.initialize()) return;
        auto captures = repository.captures();
        if (!captures) return;
        auto snapshots = repository.cashback_snapshots();
        if (!snapshots) return;
        for (const auto& saved : *snapshots) restoreCashback(saved);
        auto wallets = repository.wallets();
        if (!wallets) return;
        wallets_ = std::move(*wallets);
        captures_ = std::move(*captures);
        database_ = std::move(*database);
        error_.clear();
    }
    [[nodiscard]] bool ready() const noexcept { return database_.has_value(); }
    [[nodiscard]] const auto& captures() const noexcept { return captures_; }
    [[nodiscard]] const auto& wallets() const noexcept { return wallets_; }
    [[nodiscard]] const auto& observations() const noexcept { return observations_; }
    [[nodiscard]] QString error() const { return error_; }
    void observe(const QString& id, const QDateTime& at = QDateTime::currentDateTimeUtc()) {
        observations_.insert(id, at);
    }
    bool saveCashback(const QString& bank, const QByteArray& payload) {
        if (!database_) return false;
        wealthtracker::storage::BalanceHistoryRepository repository(*database_);
        return bool(repository.save_cashback_snapshot(bank.toStdString(), payload.toStdString()));
    }
    bool removeCashback(const QString& bank) {
        if (!database_) return false;
        wealthtracker::storage::BalanceHistoryRepository repository(*database_);
        return bool(repository.remove_cashback_snapshot(bank.toStdString()));
    }
    bool saveWallet(const QString& address, const QString& name) {
        if (!database_) return false;
        wealthtracker::storage::BalanceHistoryRepository repository(*database_);
        if (!repository.save_wallet(address.toStdString(), name.toStdString())) return false;
        auto found = std::find_if(wallets_.begin(), wallets_.end(),
            [&](const auto& wallet) { return wallet.address == address.toStdString(); });
        if (found == wallets_.end()) wallets_.push_back({address.toStdString(),name.toStdString()});
        else found->name = name.toStdString();
        return true;
    }
    bool removeWallet(const QString& address) {
        if (!database_) return false;
        wealthtracker::storage::BalanceHistoryRepository repository(*database_);
        if (!repository.remove_wallet(address.toStdString())) return false;
        std::erase_if(wallets_, [&](const auto& wallet) { return wallet.address == address.toStdString(); });
        return true;
    }
    bool append(const std::string& connection, const std::string& institution,
                const std::string& capturedAt, std::vector<wealthtracker::storage::HistoryBalance> balances) {
        if (!database_) return false;
        wealthtracker::storage::BalanceHistoryRepository repository(*database_);
        const auto saved = repository.append(connection, institution, capturedAt, balances);
        if (!saved) return false;
        captures_.push_back({*saved,connection,institution,capturedAt,std::move(balances)});
        return true;
    }
    bool appendBank(const QString& connectorId, const QString& institution,
                    const wealthtracker::connectors::NormalizedBatch& batch) {
        std::vector<wealthtracker::storage::HistoryBalance> balances;
        // A missing balance is unknown, never zero. Do not save an incomplete observation.
        bool complete = batch.balances.size() == batch.accounts.size();
        for (const auto& account : batch.accounts) {
            const auto matches = [&](const auto& balance) {
                return balance.accountExternalId == account.externalId && balance.balance.currency == account.currency;
            };
            if (std::count_if(batch.balances.cbegin(), batch.balances.cend(), matches) != 1) {
                complete = false;
                break;
            }
            const auto found = std::find_if(batch.balances.cbegin(), batch.balances.cend(), matches);
            balances.push_back({account.externalId.toStdString(), account.name.toStdString(),
                                account.currency.toStdString(), found->balance.minorUnits});
        }
        if (!complete) {
            error_ = QStringLiteral("История не обновлена: получены не все остатки по счетам.");
            return false;
        }
        const auto at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
        const bool saved = append(connectorId.toStdString(), institution.toStdString(), at, std::move(balances));
        error_ = saved ? QString{} : QStringLiteral("Не удалось сохранить историю остатков. Предыдущие записи сохранены.");
        return saved;
    }
private:
    std::optional<wealthtracker::storage::Database> database_;
    std::vector<wealthtracker::storage::BalanceCapture> captures_;
    std::vector<wealthtracker::storage::CryptoWallet> wallets_;
    QHash<QString,QDateTime> observations_;
    QString error_;
};
}
