#include "app/FinanceModels.hpp"

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
        default: return {};
    }
}

QHash<int, QByteArray> TransactionModel::roleNames() const {
    return {{TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {CategoryRole, "category"},
            {AmountRole, "amount"},
            {DateRole, "date"},
            {IncomeRole, "income"}};
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
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

}  // namespace wealthtracker::app
