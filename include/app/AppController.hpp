#pragma once

#include "app/FinanceModels.hpp"

#include <QObject>
#include <QVariantList>

#include <memory>

namespace wealthtracker::app {

class AppControllerPrivate;

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* accounts READ accounts CONSTANT)
    Q_PROPERTY(QAbstractItemModel* transactions READ transactions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* categories READ categories CONSTANT)
    Q_PROPERTY(QAbstractItemModel* holdings READ holdings CONSTANT)
    Q_PROPERTY(QAbstractItemModel* bankConnections READ bankConnections CONSTANT)
    Q_PROPERTY(QVariantList netWorthSeries READ netWorthSeries NOTIFY dashboardChanged)
    Q_PROPERTY(QString netWorth READ netWorth NOTIFY dashboardChanged)
    Q_PROPERTY(QString monthlyIncome READ monthlyIncome NOTIFY dashboardChanged)
    Q_PROPERTY(QString monthlySpending READ monthlySpending NOTIFY dashboardChanged)
    Q_PROPERTY(QString savingsRate READ savingsRate NOTIFY dashboardChanged)
    Q_PROPERTY(QString portfolioValue READ portfolioValue NOTIFY dashboardChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importStatusChanged)
    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY syncStatusChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(bool demoMode READ demoMode NOTIFY demoModeChanged)
    Q_PROPERTY(bool investmentConnected READ investmentConnected NOTIFY investmentConnectedChanged)
    Q_PROPERTY(QString connectionMessage READ connectionMessage NOTIFY connectionMessageChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QAbstractItemModel* accounts() noexcept;
    [[nodiscard]] QAbstractItemModel* transactions() noexcept;
    [[nodiscard]] QAbstractItemModel* categories() noexcept;
    [[nodiscard]] QAbstractItemModel* holdings() noexcept;
    [[nodiscard]] QAbstractItemModel* bankConnections() noexcept;
    [[nodiscard]] QVariantList netWorthSeries() const;
    [[nodiscard]] QString netWorth() const;
    [[nodiscard]] QString monthlyIncome() const;
    [[nodiscard]] QString monthlySpending() const;
    [[nodiscard]] QString savingsRate() const;
    [[nodiscard]] QString portfolioValue() const;
    [[nodiscard]] QString importStatus() const;
    [[nodiscard]] QString syncStatus() const;
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] bool demoMode() const noexcept;
    [[nodiscard]] bool investmentConnected() const noexcept;
    [[nodiscard]] QString connectionMessage() const;

    Q_INVOKABLE void loadDemoData();
    Q_INVOKABLE void clearDemoData();
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool unlock();
    Q_INVOKABLE void previewImport(const QUrl& source);
    Q_INVOKABLE bool saveInvestmentToken(const QString& token);
    Q_INVOKABLE void removeInvestmentToken();
    Q_INVOKABLE void synchronizeInvestments();
    Q_INVOKABLE void describeBankConnection(const QString& connectorId);

signals:
    void dashboardChanged();
    void importStatusChanged();
    void syncStatusChanged();
    void lockedChanged();
    void demoModeChanged();
    void investmentConnectedChanged();
    void connectionMessageChanged();

private:
    AccountModel accounts_;
    TransactionModel transactions_;
    CategoryModel categories_;
    HoldingModel holdings_;
    ConnectionModel bank_connections_;
    QVariantList net_worth_series_;
    QString import_status_;
    QString sync_status_;
    QString connection_message_;
    bool locked_ = false;
    bool demo_mode_ = true;
    bool investment_connected_ = false;
    std::unique_ptr<AppControllerPrivate> private_;

    void refreshBankConnections();
};

}  // namespace wealthtracker::app
