module;
extern "C++" {
#include <QDateTime>
#include <QHash>
#include <QString>
#include <utility>
}
export module wt.cashback_state;
import wt.cashback_sync;

export namespace wealthtracker::app {

// Owns validated automatic selections and their observation/status lifecycle.
// Transport and encrypted persistence remain independent of this state owner.
class CashbackState final {
public:
    void accept(const QString& bank, const CashbackSnapshot& incoming,
                QString status, const QDateTime& observedAt) {
        auto& state = banks_[bank];
        state.snapshot.merge(bank, incoming, observedAt);
        state.updatedAt = observedAt.toUTC();
        state.status = std::move(status);
    }
    void restore(const QString& bank, CashbackSnapshot snapshot) {
        auto& state = banks_[bank];
        state.snapshot = std::move(snapshot);
        state.updatedAt = {};
        for (const auto& observed : state.snapshot.observedAt)
            if (observed > state.updatedAt) state.updatedAt = observed;
        state.status = QStringLiteral("Сохранённые категории · последнее успешное обновление");
    }
    void setStatus(const QString& bank, QString status) {
        banks_[bank].status = std::move(status);
    }
    void clear(const QString& bank) { banks_.remove(bank); }
    [[nodiscard]] const CashbackSnapshot* find(const QString& bank) const {
        const auto it = banks_.constFind(bank);
        return it == banks_.cend() ? nullptr : &it->snapshot;
    }
    [[nodiscard]] CashbackSnapshot snapshot(const QString& bank) const {
        const auto* found = find(bank);
        return found ? *found : CashbackSnapshot{};
    }
    [[nodiscard]] QString status(const QString& bank) const {
        const auto it = banks_.constFind(bank);
        return it == banks_.cend() ? QStringLiteral("Категории ещё не получены из банка") : it->status;
    }
    [[nodiscard]] QDateTime updatedAt(const QString& bank) const {
        const auto it = banks_.constFind(bank);
        return it == banks_.cend() ? QDateTime{} : it->updatedAt;
    }
private:
    struct BankState {
        CashbackSnapshot snapshot;
        QDateTime updatedAt;
        QString status;
    };
    QHash<QString, BankState> banks_;
};
}
