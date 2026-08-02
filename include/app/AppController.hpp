#pragma once

#include "app/FinanceModels.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <expected>
#include <memory>
#include <optional>

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
    Q_PROPERTY(bool tbankSessionAvailable READ tbankSessionAvailable NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QString tbankLoginStatus READ tbankLoginStatus NOTIFY tbankConnectionChanged)
    Q_PROPERTY(QUrl tbankLoginUrl READ tbankLoginUrl CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    explicit AppController(std::unique_ptr<CredentialBackend> credentials,
                           QObject* parent);
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
    [[nodiscard]] bool tbankSessionAvailable() const noexcept;
    [[nodiscard]] QString tbankLoginStatus() const;
    [[nodiscard]] QUrl tbankLoginUrl() const;

    Q_INVOKABLE void loadDemoData();
    Q_INVOKABLE void clearDemoData();
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
    Q_INVOKABLE void attachTBankWebProfile(QObject* profile);
    Q_INVOKABLE [[nodiscard]] bool isAllowedTBankLoginUrl(const QUrl& url) const;
    Q_INVOKABLE [[nodiscard]] QString reportTBankCertificateError(
        const QUrl& url, int errorType, bool mainFrame);
    Q_INVOKABLE bool prepareTBankConnection();
    Q_INVOKABLE void beginTBankLogin();
    Q_INVOKABLE void confirmTBankLogin();
    Q_INVOKABLE void cancelTBankLogin();
    Q_INVOKABLE void disconnectTBank();

signals:
    void dashboardChanged();
    void importStatusChanged();
    void syncStatusChanged();
    void lockedChanged();
    void demoModeChanged();
    void investmentConnectedChanged();
    void connectionMessageChanged();
    void zenPluginChanged();
    void tbankConnectionChanged();

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
    QString live_net_worth_ = QStringLiteral("0 ₽");
    QString live_monthly_income_ = QStringLiteral("0 ₽");
    QString live_monthly_spending_ = QStringLiteral("0 ₽");
    QString live_savings_rate_ = QStringLiteral("—");
    bool locked_ = false;
    bool demo_mode_ = false;
    bool investment_connected_ = false;
    QSet<QString> connected_bank_ids_;
    std::unique_ptr<AppControllerPrivate> private_;

    void refreshBankConnections();
    void refreshZenPluginAutoUpdateSchedule();
    void scheduleZenPluginAutoUpdateAfter(const QDateTime& from);
    void acceptTBankSession(QByteArray session);
    void rebuildBankModels();
};

}  // namespace wealthtracker::app
