#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QString>
#include <QVector>

namespace wealthtracker::app {

struct AccountRow {
    QString name;
    QString institution;
    QString type;
    QString balance;
    QString currency;
    QString accent;
};

class AccountModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        InstitutionRole,
        TypeRole,
        BalanceRole,
        CurrencyRole,
        AccentRole,
    };

    explicit AccountModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(QVector<AccountRow> rows);

private:
    QVector<AccountRow> rows_;
};

struct TransactionRow {
    QString title;
    QString subtitle;
    QString category;
    QString amount;
    QString date;
    bool income = false;
};

class TransactionModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        CategoryRole,
        AmountRole,
        DateRole,
        IncomeRole,
    };

    explicit TransactionModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(QVector<TransactionRow> rows);

private:
    QVector<TransactionRow> rows_;
};

struct CategoryRow {
    QString name;
    QString amount;
    double share = 0.0;
    QString color;
};

class CategoryModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        AmountRole,
        ShareRole,
        ColorRole,
    };

    explicit CategoryModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(QVector<CategoryRow> rows);

private:
    QVector<CategoryRow> rows_;
};

struct HoldingRow {
    QString ticker;
    QString name;
    QString value;
    QString allocation;
    QString performance;
    bool positive = true;
};

class HoldingModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TickerRole = Qt::UserRole + 1,
        NameRole,
        ValueRole,
        AllocationRole,
        PerformanceRole,
        PositiveRole,
    };

    explicit HoldingModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(QVector<HoldingRow> rows);

private:
    QVector<HoldingRow> rows_;
};

}  // namespace wealthtracker::app
