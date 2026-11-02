#pragma once

#include "app/FinanceModels.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <expected>
#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace wealthtracker::app {

class AppControllerPrivate;

class CredentialBackend {
public:
    virtual ~CredentialBackend() = default;
    [[nodiscard]] virtual bool supported() const noexcept = 0;
    [[nodiscard]] virtual std::expected<void, QString>
    put(const QString& key, const QByteArray& secret) = 0;
    [[nodiscard]] virtual std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) = 0;
    [[nodiscard]] virtual std::expected<void, QString>
    remove(const QString& key) = 0;
};

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* accounts READ accounts CONSTANT)
    Q_PROPERTY(QAbstractItemModel* transactions READ transactions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* categories READ categories CONSTANT)
    Q_PROPERTY(QAbstractItemModel* holdings READ holdings CONSTANT)
    Q_PROPERTY(QAbstractItemModel* bankConnections READ bankConnections CONSTANT)
    Q_PROPERTY(QVariantMap connectionStates READ connectionStates NOTIFY connectionStatesChanged)
    Q_PROPERTY(QVariantList cryptoWallets READ cryptoWallets NOTIFY cryptoWalletsChanged)
    Q_PROPERTY(QString cryptoStatus READ cryptoStatus NOTIFY cryptoWalletsChanged)
    Q_PROPERTY(bool cryptoSyncRunning READ cryptoSyncRunning NOTIFY cryptoWalletsChanged)
    Q_PROPERTY(QVariantList cashbackBanks READ cashbackBanks NOTIFY cashbackChanged)
    Q_PROPERTY(QVariantList cashbackMonths READ cashbackMonths NOTIFY cashbackChanged)
    Q_PROPERTY(QString cashbackError READ cashbackError NOTIFY cashbackChanged)
    Q_PROPERTY(QVariantList balanceHistoryAccounts READ balanceHistoryAccounts NOTIFY balanceHistoryChanged)
    Q_PROPERTY(QVariantList balanceHistoryConnections READ balanceHistoryConnections NOTIFY balanceHistoryChanged)
    Q_PROPERTY(QStringList balanceHistoryCurrencies READ balanceHistoryCurrencies NOTIFY balanceHistoryChanged)
    Q_PROPERTY(QString balanceHistoryError READ balanceHistoryError NOTIFY balanceHistoryChanged)
    Q_PROPERTY(QVariantList netWorthSeries READ netWorthSeries NOTIFY dashboardChanged)
    Q_PROPERTY(QString netWorth READ netWorth NOTIFY dashboardChanged)
    Q_PROPERTY(QString netWorthUpdatedAt READ netWorthUpdatedAt NOTIFY dashboardChanged)
    Q_PROPERTY(QString reportIncome READ reportIncome NOTIFY dashboardChanged)
    Q_PROPERTY(QString reportSpending READ reportSpending NOTIFY dashboardChanged)
    Q_PROPERTY(QString savings READ savings NOTIFY dashboardChanged)
    Q_PROPERTY(QString reportingFrom READ reportingFrom NOTIFY reportingPeriodChanged)
    Q_PROPERTY(QString reportingTo READ reportingTo NOTIFY reportingPeriodChanged)
    Q_PROPERTY(QString reportingPeriodLabel READ reportingPeriodLabel
               NOTIFY reportingPeriodChanged)
    Q_PROPERTY(QString reportingPeriodError READ reportingPeriodError
               NOTIFY reportingPeriodChanged)
    Q_PROPERTY(QString portfolioValue READ portfolioValue NOTIFY dashboardChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importStatusChanged)
    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY syncStatusChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(bool demoMode READ demoMode NOTIFY demoModeChanged)
    Q_PROPERTY(bool hasFinancialData READ hasFinancialData NOTIFY dashboardChanged)
    Q_PROPERTY(bool investmentConnected READ investmentConnected NOTIFY investmentConnectedChanged)
    Q_PROPERTY(QString connectionMessage READ connectionMessage NOTIFY connectionMessageChanged)
    Q_PROPERTY(QVariantList zenPluginFields READ zenPluginFields NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginId READ zenPluginId NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginBundlePath READ zenPluginBundlePath NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginInstitution READ zenPluginInstitution NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginPrompt READ zenPluginPrompt NOTIFY zenPluginChanged)
    Q_PROPERTY(bool zenPluginPromptSecret READ zenPluginPromptSecret NOTIFY zenPluginChanged)
    Q_PROPERTY(bool zenPluginRunning READ zenPluginRunning NOTIFY zenPluginChanged)
    Q_PROPERTY(bool zenPluginAutoUpdate READ zenPluginAutoUpdate NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginLastSync READ zenPluginLastSync NOTIFY zenPluginChanged)
    Q_PROPERTY(QString zenPluginNextSync READ zenPluginNextSync NOTIFY zenPluginChanged)
    Q_PROPERTY(bool tbankConnected READ tbankConnected NOTIFY tbankConnectionChanged)
    Q_PROPERTY(bool tbankLoginActive READ tbankLoginActive NOTIFY tbankConnectionChanged)
    Q_PROPERTY(bool tbankSyncRunning READ tbankSyncRunning NOTIFY tbankConnectionChanged)
    Q_PROPERTY(bool tbankAutoUpdate READ tbankAutoUpdate NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QString tbankLastSync READ tbankLastSync NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QString tbankNextSync READ tbankNextSync NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QString tbankLoginStatus READ tbankLoginStatus NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QUrl tbankLoginUrl READ tbankLoginUrl CONSTANT)
    Q_PROPERTY(QWindow* tbankWebView READ tbankWebView NOTIFY tbankConnectionChanged)
    Q_PROPERTY(bool alfaConnected READ alfaConnected NOTIFY alfaConnectionChanged)
    Q_PROPERTY(bool alfaLoginActive READ alfaLoginActive NOTIFY alfaConnectionChanged)
    Q_PROPERTY(bool alfaSyncRunning READ alfaSyncRunning NOTIFY alfaConnectionChanged)
    Q_PROPERTY(bool alfaAutoUpdate READ alfaAutoUpdate NOTIFY alfaConnectionChanged)
    Q_PROPERTY(QString alfaLastSync READ alfaLastSync NOTIFY alfaConnectionChanged)
    Q_PROPERTY(QString alfaNextSync READ alfaNextSync NOTIFY alfaConnectionChanged)
    Q_PROPERTY(QString alfaLoginStatus READ alfaLoginStatus NOTIFY alfaConnectionChanged)
    Q_PROPERTY(bool alfaCertificateHelpRequired READ alfaCertificateHelpRequired
               NOTIFY alfaConnectionChanged)
    Q_PROPERTY(QUrl alfaLoginUrl READ alfaLoginUrl CONSTANT)
    Q_PROPERTY(QWindow* alfaWebView READ alfaWebView NOTIFY alfaConnectionChanged)
    Q_PROPERTY(bool vtbConnected READ vtbConnected NOTIFY vtbConnectionChanged)
    Q_PROPERTY(bool vtbLoginActive READ vtbLoginActive NOTIFY vtbConnectionChanged)
    Q_PROPERTY(bool vtbSyncRunning READ vtbSyncRunning NOTIFY vtbConnectionChanged)
    Q_PROPERTY(bool vtbAutoUpdate READ vtbAutoUpdate NOTIFY vtbConnectionChanged)
    Q_PROPERTY(QString vtbLastSync READ vtbLastSync NOTIFY vtbConnectionChanged)
    Q_PROPERTY(QString vtbNextSync READ vtbNextSync NOTIFY vtbConnectionChanged)
    Q_PROPERTY(QString vtbLoginStatus READ vtbLoginStatus NOTIFY vtbConnectionChanged)
    Q_PROPERTY(QUrl vtbLoginUrl READ vtbLoginUrl CONSTANT)
    Q_PROPERTY(QWindow* vtbWebView READ vtbWebView NOTIFY vtbConnectionChanged)
    Q_PROPERTY(bool ozonConnected READ ozonConnected NOTIFY ozonConnectionChanged)
    Q_PROPERTY(bool ozonLoginActive READ ozonLoginActive NOTIFY ozonConnectionChanged)
    Q_PROPERTY(bool ozonSyncRunning READ ozonSyncRunning NOTIFY ozonConnectionChanged)
    Q_PROPERTY(bool ozonAutoUpdate READ ozonAutoUpdate NOTIFY ozonConnectionChanged)
    Q_PROPERTY(QString ozonLastSync READ ozonLastSync NOTIFY ozonConnectionChanged)
    Q_PROPERTY(QString ozonNextSync READ ozonNextSync NOTIFY ozonConnectionChanged)
    Q_PROPERTY(QString ozonLoginStatus READ ozonLoginStatus NOTIFY ozonConnectionChanged)
    Q_PROPERTY(QUrl ozonLoginUrl READ ozonLoginUrl CONSTANT)
    Q_PROPERTY(QWindow* ozonWebView READ ozonWebView NOTIFY ozonConnectionChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    explicit AppController(std::unique_ptr<CredentialBackend> credentials,
                           QObject* parent);
    AppController(std::unique_ptr<CredentialBackend> credentials,
                  std::unique_ptr<QNetworkAccessManager> network, QObject* parent);
    AppController(std::unique_ptr<CredentialBackend> credentials,
                  std::unique_ptr<QNetworkAccessManager> network, QObject* parent,
                  const QString& historyPath);
    ~AppController() override;

    [[nodiscard]] QVariantList cryptoWallets() const;
    [[nodiscard]] QString cryptoStatus() const;
    [[nodiscard]] bool cryptoSyncRunning() const;
    Q_INVOKABLE bool addCryptoWallet(const QString& name, const QString& address);
    Q_INVOKABLE bool removeCryptoWallet(const QString& address);
    Q_INVOKABLE bool refreshCryptoWallets();

    [[nodiscard]] QVariantList cashbackBanks() const;
    [[nodiscard]] QVariantList cashbackMonths() const;
    [[nodiscard]] QString cashbackError() const;
    Q_INVOKABLE bool saveCashbackCategory(const QString& bank, const QString& month,
        const QString& category, const QString& percent);
    Q_INVOKABLE bool removeCashbackCategory(const QString& bank, const QString& month,
        const QString& category);
    Q_INVOKABLE void refreshCashbackBanks();
    Q_INVOKABLE void refreshCashbackBank(const QString& bank);
    [[nodiscard]] QVariantMap connectionStates() const;

    [[nodiscard]] QVariantList balanceHistoryAccounts() const;
    [[nodiscard]] QVariantList balanceHistoryConnections() const;
    [[nodiscard]] QStringList balanceHistoryCurrencies() const;
    [[nodiscard]] QString balanceHistoryError() const;
    Q_INVOKABLE QVariantMap balanceHistoryChart(const QString& accountId,
                                               const QString& currency) const;
    Q_INVOKABLE QVariantMap filteredBalanceHistoryChart(const QString& accountId,
        const QString& currency, const QStringList& connectionIds, bool restrictConnections,
        const QString& from, const QString& to, bool daily = false) const;
    Q_INVOKABLE QVariantMap balanceHistorySummary(const QStringList& connectionIds,
        bool restrictConnections, const QString& from, const QString& to) const;

    [[nodiscard]] QAbstractItemModel* accounts() noexcept;
    [[nodiscard]] QAbstractItemModel* transactions() noexcept;
    [[nodiscard]] QAbstractItemModel* categories() noexcept;
    [[nodiscard]] QAbstractItemModel* holdings() noexcept;
    [[nodiscard]] QAbstractItemModel* bankConnections() noexcept;
    [[nodiscard]] QVariantList netWorthSeries() const;
    [[nodiscard]] QString netWorth() const;
    [[nodiscard]] QString netWorthUpdatedAt() const;
    [[nodiscard]] QString reportIncome() const;
    [[nodiscard]] QString reportSpending() const;
    [[nodiscard]] QString savings() const;
    [[nodiscard]] QString reportingFrom() const;
    [[nodiscard]] QString reportingTo() const;
    [[nodiscard]] QString reportingPeriodLabel() const;
    [[nodiscard]] QString reportingPeriodError() const;
    [[nodiscard]] QString portfolioValue() const;
    [[nodiscard]] QString importStatus() const;
    [[nodiscard]] QString syncStatus() const;
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] bool demoMode() const noexcept;
    [[nodiscard]] bool hasFinancialData() const noexcept;
    [[nodiscard]] bool investmentConnected() const noexcept;
    [[nodiscard]] QString connectionMessage() const;
    [[nodiscard]] QVariantList zenPluginFields() const;
    [[nodiscard]] QString zenPluginId() const;
    [[nodiscard]] QString zenPluginBundlePath() const;
    [[nodiscard]] QString zenPluginInstitution() const;
    [[nodiscard]] QString zenPluginPrompt() const;
    [[nodiscard]] bool zenPluginPromptSecret() const noexcept;
    [[nodiscard]] bool zenPluginRunning() const noexcept;
    [[nodiscard]] bool zenPluginAutoUpdate() const;
    [[nodiscard]] QString zenPluginLastSync() const;
    [[nodiscard]] QString zenPluginNextSync() const;
    [[nodiscard]] bool tbankConnected() const noexcept;
    [[nodiscard]] bool tbankLoginActive() const noexcept;
    [[nodiscard]] bool tbankSyncRunning() const noexcept;
    [[nodiscard]] bool tbankAutoUpdate() const;
    [[nodiscard]] QString tbankLastSync() const;
    [[nodiscard]] QString tbankNextSync() const;
    [[nodiscard]] QString tbankLoginStatus() const;
    [[nodiscard]] QUrl tbankLoginUrl() const;
    [[nodiscard]] QWindow* tbankWebView();
    [[nodiscard]] bool alfaConnected() const noexcept;
    [[nodiscard]] bool alfaLoginActive() const noexcept;
    [[nodiscard]] bool alfaSyncRunning() const noexcept;
    [[nodiscard]] bool alfaAutoUpdate() const;
    [[nodiscard]] QString alfaLastSync() const;
    [[nodiscard]] QString alfaNextSync() const;
    [[nodiscard]] QString alfaLoginStatus() const;
    [[nodiscard]] bool alfaCertificateHelpRequired() const noexcept;
    [[nodiscard]] QUrl alfaLoginUrl() const;
    [[nodiscard]] QWindow* alfaWebView();
    [[nodiscard]] bool vtbConnected() const noexcept;
    [[nodiscard]] bool vtbLoginActive() const noexcept;
    [[nodiscard]] bool vtbSyncRunning() const noexcept;
    [[nodiscard]] bool vtbAutoUpdate() const;
    [[nodiscard]] QString vtbLastSync() const;
    [[nodiscard]] QString vtbNextSync() const;
    [[nodiscard]] QString vtbLoginStatus() const;
    [[nodiscard]] QUrl vtbLoginUrl() const;
    [[nodiscard]] QWindow* vtbWebView();
    [[nodiscard]] bool ozonConnected() const noexcept;
    [[nodiscard]] bool ozonLoginActive() const noexcept;
    [[nodiscard]] bool ozonSyncRunning() const noexcept;
    [[nodiscard]] bool ozonAutoUpdate() const;
    [[nodiscard]] QString ozonLastSync() const;
    [[nodiscard]] QString ozonNextSync() const;
    [[nodiscard]] QString ozonLoginStatus() const;
    [[nodiscard]] QUrl ozonLoginUrl() const;
    [[nodiscard]] QWindow* ozonWebView();

    Q_INVOKABLE void loadDemoData();
    Q_INVOKABLE void clearDemoData();
    Q_INVOKABLE bool setReportingPeriod(const QString& from, const QString& to);
    Q_INVOKABLE void resetReportingPeriod();
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool unlock();
    Q_INVOKABLE void previewImport(const QUrl& source);
    Q_INVOKABLE bool saveInvestmentToken(const QString& token);
    Q_INVOKABLE void removeInvestmentToken();
    Q_INVOKABLE void synchronizeInvestments();
    Q_INVOKABLE bool saveBankToken(const QString& connectorId, const QString& token);
    Q_INVOKABLE void removeBankToken(const QString& connectorId);
    Q_INVOKABLE void synchronizeBank(const QString& connectorId);
    Q_INVOKABLE void describeBankConnection(const QString& connectorId);
    Q_INVOKABLE bool inspectZenPluginBundle(const QUrl& source);
    Q_INVOKABLE bool startZenPlugin(const QString& institution,
                                    const QVariantMap& preferences);
    Q_INVOKABLE bool synchronizeZenPlugin();
    Q_INVOKABLE void setZenPluginAutoUpdate(bool enabled);
    Q_INVOKABLE bool answerZenPluginPrompt(const QString& value);
    Q_INVOKABLE void cancelZenPlugin();
    Q_INVOKABLE void removeZenPlugin();
    Q_INVOKABLE bool prepareTBankConnection();
    Q_INVOKABLE void beginTBankLogin();
    Q_INVOKABLE void reloadTBankLoginPage();
    Q_INVOKABLE void cancelTBankLogin();
    Q_INVOKABLE bool synchronizeTBank();
    Q_INVOKABLE void setTBankAutoUpdate(bool enabled);
    Q_INVOKABLE void disconnectTBank();
    Q_INVOKABLE bool prepareAlfaConnection();
    Q_INVOKABLE void beginAlfaLogin();
    Q_INVOKABLE void reloadAlfaLoginPage();
    Q_INVOKABLE bool openAlfaCertificateInstructions();
    Q_INVOKABLE void cancelAlfaLogin();
    Q_INVOKABLE bool synchronizeAlfa();
    Q_INVOKABLE void setAlfaAutoUpdate(bool enabled);
    Q_INVOKABLE void disconnectAlfa();
    Q_INVOKABLE bool prepareVtbConnection();
    Q_INVOKABLE void beginVtbLogin();
    Q_INVOKABLE void reloadVtbLoginPage();
    Q_INVOKABLE void cancelVtbLogin();
    Q_INVOKABLE bool synchronizeVtb();
    Q_INVOKABLE void setVtbAutoUpdate(bool enabled);
    Q_INVOKABLE void disconnectVtb();
    Q_INVOKABLE bool prepareOzonConnection();
    Q_INVOKABLE void beginOzonLogin();
    Q_INVOKABLE void reloadOzonLoginPage();
    Q_INVOKABLE void cancelOzonLogin();
    Q_INVOKABLE bool synchronizeOzon();
    Q_INVOKABLE void setOzonAutoUpdate(bool enabled);
    Q_INVOKABLE void disconnectOzon();

signals:
    void connectionStatesChanged();
    void cashbackChanged();
    void cryptoWalletsChanged();
    void balanceHistoryChanged();
    void dashboardChanged();
    void reportingPeriodChanged();
    void importStatusChanged();
    void syncStatusChanged();
    void lockedChanged();
    void demoModeChanged();
    void investmentConnectedChanged();
    void connectionMessageChanged();
    void zenPluginChanged();
    void tbankConnectionChanged();
    void alfaConnectionChanged();
    void vtbConnectionChanged();
    void ozonConnectionChanged();

private:
    void initializeCashback();
    void persistBankCashback(const QString& bank, bool remove = false);
    void restoreSavedBalances();
    void startVtbCashback();
    void stopVtbCashback(bool clearData = false);
    void startTBankCashback();
    void stopTBankCashback(bool clearData = false);
    void startAlfaCashback();
    void stopAlfaCashback(bool clearData = false);
    void startOzonCashback();
    void stopOzonCashback(bool clearData = false);
    bool persistCashback(const QByteArray& data);
    void startNextCryptoWallet();
    void stopCryptoRefresh();
    void initializeBalanceHistory(const QString& path);
    void recordBalanceHistory(const QString& connectorId);

    AccountModel accounts_;
    TransactionModel transactions_;
    CategoryModel categories_;
    HoldingModel holdings_;
    ConnectionModel bank_connections_;
    QVariantList net_worth_series_;
    QString import_status_;
    QString sync_status_;
    QString connection_message_;
    QVariantList zen_plugin_fields_;
    QString zen_plugin_id_;
    QString zen_plugin_bundle_path_;
    QString zen_plugin_institution_;
    QString zen_plugin_prompt_;
    bool zen_plugin_prompt_secret_ = true;
    bool zen_plugin_running_ = false;
    bool tbank_connected_ = false;
    bool tbank_login_active_ = false;
    QString tbank_login_status_;
    bool alfa_connected_ = false;
    bool alfa_login_active_ = false;
    QString alfa_login_status_;
    bool vtb_connected_ = false;
    bool vtb_login_active_ = false;
    QString vtb_login_status_;
    bool ozon_connected_ = false;
    bool ozon_login_active_ = false;
    QString ozon_login_status_;
    QString live_portfolio_value_ = QStringLiteral("0 ₽");
    QString live_net_worth_ = QStringLiteral("0 ₽");
    QString live_report_income_ = QStringLiteral("0 ₽");
    QString live_report_spending_ = QStringLiteral("0 ₽");
    QString live_savings_ = QStringLiteral("0 ₽");
    QDate reporting_from_;
    QDate reporting_to_;
    QString reporting_period_error_;
    bool locked_ = false;
    bool demo_mode_ = false;
    bool investment_connected_ = false;
    QSet<QString> connected_bank_ids_;
    std::unique_ptr<AppControllerPrivate> private_;

    void refreshBankConnections();
    void refreshZenPluginAutoUpdateSchedule();
    void scheduleZenPluginAutoUpdateAfter(const QDateTime& from);
    void refreshTBankAutoUpdateSchedule();
    void scheduleTBankAutoUpdateAfter(const QDateTime& from);
    void failTBankLogin(QString message);
    void acceptTBankSession(QByteArray session);
    void refreshAlfaAutoUpdateSchedule();
    void scheduleAlfaAutoUpdateAfter(const QDateTime& from);
    void refreshVtbAutoUpdateSchedule();
    void scheduleVtbAutoUpdateAfter(const QDateTime& from);
    void refreshOzonAutoUpdateSchedule();
    void scheduleOzonAutoUpdateAfter(const QDateTime& from);
    void restoreRetailConnections();
    void rebuildBankModels();
};

}  // namespace wealthtracker::app
