#include "app/FinanceModels.hpp"

#include <algorithm>
#include <utility>

namespace wealthtracker::app {

AccountModel::AccountModel(QObject* parent) : QAbstractListModel(parent) {}

int AccountModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant AccountModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_.at(index.row());
    switch (role) {
        case NameRole: return row.name;
        case InstitutionRole: return row.institution;
        case TypeRole: return row.type;
        case BalanceRole: return row.balance;
        case CurrencyRole: return row.currency;
        case AccentRole: return row.accent;
        default: return {};
    }
}

QHash<int, QByteArray> AccountModel::roleNames() const {
    return {{NameRole, "name"},
            {InstitutionRole, "institution"},
            {TypeRole, "type"},
            {BalanceRole, "balance"},
            {CurrencyRole, "currency"},
            {AccentRole, "accent"}};
}

void AccountModel::replace(QVector<AccountRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

TransactionModel::TransactionModel(QObject* parent) : QAbstractListModel(parent) {}

int TransactionModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TransactionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_.at(index.row());
    switch (role) {
        case TitleRole: return row.title;
        case SubtitleRole: return row.subtitle;
        case CategoryRole: return row.category;
        case AmountRole: return row.amount;
        case DateRole: return row.date;
        case IncomeRole: return row.income;
        case TransferRole: return row.transfer;
        default: return {};
    }
}

QHash<int, QByteArray> TransactionModel::roleNames() const {
    return {{TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {CategoryRole, "category"},
            {AmountRole, "amount"},
            {DateRole, "date"},
            {IncomeRole, "income"},
            {TransferRole, "transfer"}};
}

void TransactionModel::replace(QVector<TransactionRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

CategoryModel::CategoryModel(QObject* parent) : QAbstractListModel(parent) {}

int CategoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_.at(index.row());
    switch (role) {
        case NameRole: return row.name;
        case AmountRole: return row.amount;
        case ShareRole: return row.share;
        case ColorRole: return row.color;
        default: return {};
    }
}

QHash<int, QByteArray> CategoryModel::roleNames() const {
    return {{NameRole, "name"},
            {AmountRole, "amount"},
            {ShareRole, "share"},
            {ColorRole, "color"}};
}

void CategoryModel::replace(QVector<CategoryRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

HoldingModel::HoldingModel(QObject* parent) : QAbstractListModel(parent) {}

int HoldingModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant HoldingModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_.at(index.row());
    switch (role) {
        case TickerRole: return row.ticker;
        case NameRole: return row.name;
        case ValueRole: return row.value;
        case AllocationRole: return row.allocation;
        case PerformanceRole: return row.performance;
        case PositiveRole: return row.positive;
        default: return {};
    }
}

QHash<int, QByteArray> HoldingModel::roleNames() const {
    return {{TickerRole, "ticker"},
            {NameRole, "name"},
            {ValueRole, "value"},
            {AllocationRole, "allocation"},
            {PerformanceRole, "performance"},
            {PositiveRole, "positive"}};
}

void HoldingModel::replace(QVector<HoldingRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

ConnectionModel::ConnectionModel(QObject* parent) : QAbstractListModel(parent) {}

int ConnectionModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ConnectionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_.at(index.row());
    switch (role) {
        case ConnectorIdRole: return row.connectorId;
        case InstitutionRole: return row.institution;
        case ScopeRole: return row.scope;
        case MethodRole: return row.method;
        case StatusRole: return row.status;
        case ConnectedRole: return row.connected;
        case AvailableRole: return row.available;
        default: return {};
    }
}

QHash<int, QByteArray> ConnectionModel::roleNames() const {
    return {{ConnectorIdRole, "connectorId"},
            {InstitutionRole, "institution"},
            {ScopeRole, "scope"},
            {MethodRole, "method"},
            {StatusRole, "status"},
            {ConnectedRole, "connected"},
            {AvailableRole, "available"}};
}

void ConnectionModel::replace(QVector<ConnectionRow> rows) {
    const bool sameRows = rows_.size() == rows.size()
        && std::equal(rows_.cbegin(), rows_.cend(), rows.cbegin(),
                      [](const ConnectionRow& current,
                         const ConnectionRow& replacement) {
                          return current.connectorId == replacement.connectorId;
                      });
    if (sameRows) {
        for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const auto& current = rows_.at(rowIndex);
            const auto& replacement = rows.at(rowIndex);
            QList<int> changedRoles;
            if (current.institution != replacement.institution) {
                changedRoles.push_back(InstitutionRole);
            }
            if (current.scope != replacement.scope) {
                changedRoles.push_back(ScopeRole);
            }
            if (current.method != replacement.method) {
                changedRoles.push_back(MethodRole);
            }
            if (current.status != replacement.status) {
                changedRoles.push_back(StatusRole);
            }
            if (current.connected != replacement.connected) {
                changedRoles.push_back(ConnectedRole);
            }
            if (current.available != replacement.available) {
                changedRoles.push_back(AvailableRole);
            }
            if (changedRoles.isEmpty()) {
                continue;
            }
            rows_[rowIndex] = replacement;
            const auto changedIndex = index(static_cast<int>(rowIndex), 0);
            emit dataChanged(changedIndex, changedIndex, changedRoles);
        }
        return;
    }

    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

}  // namespace wealthtracker::app
