#include "app/AppController.hpp"
#include "app/BrowserSession.hpp"

#include <QDate>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtWebView/QWebView>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

import wt.connectors;
import wt.alfa_login;
import wt.bank_sync;
import wt.connection_state;
import wt.connection_restore;
import wt.balance_history_view;
import wt.history_store;
import wt.wallet_service;
import wt.cashback;
import wt.cashback_sync;
import wt.cashback_state;
import wt.tbank_cashback;
import wt.storage;
import wt.investment_sync;
import wt.platform;
import wt.reporting;
import wt.finance_projection;
import wt.tbank_login;
import wt.vtb_login;
import wt.ozon_login;
import wt.plugin_service;

namespace wealthtracker::app {
namespace {

using wealthtracker::connectors::ExternalAccount;
using wealthtracker::connectors::Money;
using wealthtracker::connectors::NormalizedBatch;
using wealthtracker::connectors::ZenPluginDescriptor;

template <typename Method, typename... Args>
void dispatchRetail(AppController* controller, const QString& connectorId,
                    const std::array<Method, 4>& methods, Args... args) {
    const std::array ids{QStringLiteral("tbank-retail"), QStringLiteral("alfa-retail"),
                         QStringLiteral("vtb-retail"), QStringLiteral("ozon-retail")};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (connectorId == ids[index]) {
            (controller->*methods[index])(args...);
            return;
        }
    }
}

#ifdef Q_OS_ANDROID
struct BankWindowDisplay {
    QPointer<QWindow> owner;
    QPointer<QWindow> native;

    QWindow* forView(QWindow* view) {
        if (owner != view) {
            owner = view;
            native.clear();
            if (view) {
                // Qt's QQuickWebView embeds this native child, not QWebView's wrapper.
                // Cache it before WindowContainer reparents it into the Quick window.
                const auto children = view->findChildren<QWindow*>(QString{}, Qt::FindDirectChildrenOnly);
                if (children.size() == 1) {
                    native = children.front();
                    prepareAndroidBankWindow(native);
                }
            }
        }
        return native;
    }
};
#endif

class SystemCredentialBackend final : public CredentialBackend {
public:
    SystemCredentialBackend()
        : store_(wealthtracker::platform::make_system_credential_store(
              "org.wealthtracker.desktop")) {}

    bool supported() const noexcept override {
        return store_ && store_->supported();
    }

    std::expected<void, QString> put(const QString& key,
                                     const QByteArray& secret) override {
        if (!store_) {
            return std::unexpected(
                QStringLiteral("Системное хранилище секретов недоступно"));
        }
        const auto* data =
            reinterpret_cast<const std::byte*>(secret.constData());
        const auto result = store_->put(
            key.toStdString(),
            std::span<const std::byte>(
                data, static_cast<std::size_t>(secret.size())));
        if (!result) {
            return std::unexpected(
                QString::fromStdString(result.error().message));
        }
        return {};
    }

    std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) override {
        if (!store_) {
            return std::unexpected(
                QStringLiteral("Системное хранилище секретов недоступно"));
        }
        auto result = store_->get(key.toStdString());
        if (!result) {
            if (result.error().code ==
                wealthtracker::platform::CredentialErrorCode::not_found) {
                return std::optional<QByteArray>{};
            }
            return std::unexpected(
                QString::fromStdString(result.error().message));
        }
        return std::optional<QByteArray>{QByteArray(
            reinterpret_cast<const char*>(result->data()),
            static_cast<qsizetype>(result->size()))};
    }

    std::expected<void, QString> remove(const QString& key) override {
        if (!store_) {
            return std::unexpected(
                QStringLiteral("Системное хранилище секретов недоступно"));
        }
        const auto result = store_->remove(key.toStdString());
        if (!result &&
            result.error().code !=
                wealthtracker::platform::CredentialErrorCode::not_found) {
            return std::unexpected(
                QString::fromStdString(result.error().message));
        }
        return {};
    }

private:
    std::unique_ptr<wealthtracker::platform::CredentialStore> store_;
};

struct BankConfiguration {
    QString credentialKey;
    QString institution;
    int minimumTokenSize = 8;
};

std::optional<BankConfiguration> bankConfiguration(const QString& connectorId) {
    if (connectorId == QStringLiteral("alfa-api")) {
        return BankConfiguration{
            QStringLiteral("alfa-b2c-readonly"),
            QStringLiteral("Альфа-Банк"), 8};
    }
    if (connectorId == QStringLiteral("tbank-business")) {
        return BankConfiguration{
            QStringLiteral("tbank-business-readonly"),
            QStringLiteral("Т-Банк Бизнес"), 8};
    }
    if (connectorId == QStringLiteral("modulbank")) {
        return BankConfiguration{
            QStringLiteral("modulbank-readonly"),
            QStringLiteral("Модульбанк"), 8};
    }
    if (connectorId == QStringLiteral("t-invest")) {
        return BankConfiguration{
            QStringLiteral("t-invest-readonly"),
            QStringLiteral("Т-Инвестиции"), 20};
    }
    return std::nullopt;
}

const QStringList& supportedConnectorIds() {
    static const QStringList ids{
        QStringLiteral("alfa-api"),
        QStringLiteral("tbank-business"),
        QStringLiteral("modulbank"),
        QStringLiteral("t-invest"),
    };
    return ids;
}

struct RetailBankDefinition {
    QString connectorId;
    QString institution;
    QString accent;
};

const QVector<RetailBankDefinition>& retailBankDefinitions() {
    static const QVector<RetailBankDefinition> banks{
        {QStringLiteral("tbank-retail"), QStringLiteral("Т-Банк"),
         QStringLiteral("#F9C74F")},
        {QStringLiteral("alfa-retail"), QStringLiteral("Альфа-Банк"),
         QStringLiteral("#EF4444")},
        {QStringLiteral("sber-retail"), QStringLiteral("Сбер"),
         QStringLiteral("#22C55E")},
        {QStringLiteral("vtb-retail"), QStringLiteral("ВТБ"),
         QStringLiteral("#2563EB")},
        {QStringLiteral("ozon-retail"), QStringLiteral("Ozon Банк"),
         QStringLiteral("#005BFF")},
    };
    return banks;
}

constexpr auto tbankRetailPluginId = "tbank-retail";
constexpr auto tbankRetailCredentialKey = "tbank-retail-session";

bool isRetailBankConnector(const QString& connectorId) {
    return std::ranges::any_of(
        retailBankDefinitions(), [&connectorId](const auto& bank) {
            return bank.connectorId == connectorId;
        });
}

QString retailBankInstitution(const QString& connectorId) {
    const auto match = std::ranges::find_if(
        retailBankDefinitions(), [&connectorId](const auto& bank) {
            return bank.connectorId == connectorId;
        });
    return match == retailBankDefinitions().cend() ? QString{} : match->institution;
}

QString connectorAccent(const QString& connectorId) {
    if (connectorId == QStringLiteral("alfa-api") ||
        connectorId == QStringLiteral("alfa-retail")) {
        return QStringLiteral("#EF4444");
    }
    if (connectorId == QStringLiteral("tbank-business") ||
        connectorId == QStringLiteral("tbank-retail")) {
        return QStringLiteral("#F9C74F");
    }
    if (connectorId == QStringLiteral("sber-retail")) {
        return QStringLiteral("#22C55E");
    }
    return QStringLiteral("#2563EB");
}

constexpr int autoUpdateIntervalHours = 6;
constexpr int autoUpdateIntervalSeconds =
    autoUpdateIntervalHours * 60 * 60;

QDateTime scheduleAutoUpdate(QTimer& timer, const QDateTime& from,
                            const int intervalSeconds = autoUpdateIntervalSeconds) {
    const auto schedule = connectionRefreshSchedule(from, QDateTime::currentDateTimeUtc(), intervalSeconds);
    timer.start(schedule.delayMilliseconds);
    return schedule.next;
}

std::optional<QDateTime> settingsDateTime(const QString& key) {
    const auto value = QSettings{}.value(key).toString();
    if (value.isEmpty()) {
        return std::nullopt;
    }
    auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }
    return parsed.isValid() ? std::optional(parsed.toUTC()) : std::nullopt;
}

QString formatSyncDateTime(const QDateTime& value) {
    auto local = value.toLocalTime();
    return QLocale(QLocale::Russian, QLocale::Russia)
        .toString(local, QStringLiteral("d MMMM, HH:mm"));
}

QString zenPreferenceKindName(
    wealthtracker::connectors::ZenPluginPreferenceKind kind);

QJsonObject savedZenPluginPreferences() {
    const auto encoded =
        QSettings{}.value(QStringLiteral("zenPlugin/preferences"))
            .toByteArray();
    if (encoded.isEmpty() || encoded.size() > 64 * 1024) {
        return {};
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(encoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

QVariantList zenPluginFieldVariants(const ZenPluginDescriptor& descriptor) {
    const auto saved = savedZenPluginPreferences();
    QVariantList fields;
    for (const auto& field : descriptor.preferences) {
        const auto savedValue = saved.value(field.key);
        fields.push_back(QVariantMap{
            {QStringLiteral("key"), field.key},
            {QStringLiteral("label"), field.label},
            {QStringLiteral("kind"), zenPreferenceKindName(field.kind)},
            {QStringLiteral("defaultValue"),
             savedValue.isUndefined()
                 ? QVariant(field.defaultValue)
                 : savedValue.toVariant()},
            {QStringLiteral("required"), field.required},
            {QStringLiteral("sensitive"), field.sensitive},
        });
    }
    return fields;
}

QString nodeExecutable() {
    auto executable =
        QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!executable.isEmpty()) {
        return executable;
    }
    for (const auto& candidate :
         {QStringLiteral("/opt/homebrew/bin/node"),
          QStringLiteral("/usr/local/bin/node"),
          QStringLiteral("/usr/bin/node")}) {
        const QFileInfo file(candidate);
        if (file.isFile() && file.isExecutable()) {
            return candidate;
        }
    }
    return {};
}

QString zenPreferenceKindName(
    const wealthtracker::connectors::ZenPluginPreferenceKind kind) {
    using wealthtracker::connectors::ZenPluginPreferenceKind;
    switch (kind) {
        case ZenPluginPreferenceKind::Password:
            return QStringLiteral("password");
        case ZenPluginPreferenceKind::Phone:
            return QStringLiteral("phone");
        case ZenPluginPreferenceKind::Number:
            return QStringLiteral("number");
        case ZenPluginPreferenceKind::Date:
            return QStringLiteral("date");
        case ZenPluginPreferenceKind::Checkbox:
            return QStringLiteral("checkbox");
        case ZenPluginPreferenceKind::Choice:
            return QStringLiteral("choice");
        case ZenPluginPreferenceKind::Text:
            return QStringLiteral("text");
    }
    return QStringLiteral("text");
}

}  // namespace

class AppControllerPrivate final {
public:
    explicit AppControllerPrivate(
        std::unique_ptr<CredentialBackend> credentialBackend,
        std::unique_ptr<QNetworkAccessManager> networkManager)
        : credentials(std::move(credentialBackend)),
          network(networkManager ? std::move(networkManager)
                                 : std::make_unique<QNetworkAccessManager>()),
          investment(*network),
          alfa(*network),
          tbank_business(*network),
          modulbank(*network),
          tbank_retail(*network),
          wallets(*network, history) {}

    std::unique_ptr<CredentialBackend> credentials;
    std::unique_ptr<QNetworkAccessManager> network;
    wealthtracker::connectors::TInvestClient investment;
    wealthtracker::connectors::AlfaBankClient alfa;
    wealthtracker::connectors::TBankBusinessClient tbank_business;
    wealthtracker::connectors::ModulbankClient modulbank;
    wealthtracker::connectors::TBankRetailClient tbank_retail;
    QHash<QString, wealthtracker::connectors::NormalizedBatch> bank_batches;
    ConnectionActivityRegistry activity;
    QVariantMap connection_states;
    QHash<QString, QString> bank_institutions;
    HistoryStore history;
    WalletService wallets;
    QTimer crypto_timer;
    QHash<QString, QPointer<BankSyncCoordinator>> bank_sync_coordinators;
    QPointer<InvestmentSyncCoordinator> investment_sync;
    PluginService plugin;
    QTimer zen_auto_update_timer;
    QDateTime next_zen_auto_update_at;
    std::unique_ptr<TBankLoginCoordinator> tbank_login;
    QTimer tbank_auto_update_timer;
    QDateTime next_tbank_auto_update_at;
    QPointer<BankSyncCoordinator> tbank_sync_coordinator;
    std::unique_ptr<AlfaLoginCoordinator> alfa_login;
    QTimer alfa_auto_update_timer;
    QDateTime next_alfa_auto_update_at;
    std::unique_ptr<VtbLoginCoordinator> vtb_login;
    std::unique_ptr<OzonLoginCoordinator> ozon_login;
#ifdef Q_OS_ANDROID
    BankWindowDisplay tbank_display;
    BankWindowDisplay alfa_display;
    BankWindowDisplay vtb_display;
    BankWindowDisplay ozon_display;
#endif
    QTimer vtb_auto_update_timer;
    QDateTime next_vtb_auto_update_at;
    QTimer ozon_auto_update_timer;
    QDateTime next_ozon_auto_update_at;
    QStringList pending_retail_restores;
    CashbackBook cashback;
    QString cashback_error;
    bool cashback_storage_ready = false;
    QTimer cashback_month_timer;
    QDate cashback_month;
    CashbackState cashback_state;
    QPointer<CashbackSync> vtb_cashback_sync;
    QPointer<CashbackSync> alfa_cashback_sync;
    QPointer<CashbackSync> ozon_cashback_sync;
    QPointer<TBankCashbackSync> tbank_cashback_sync;
    std::optional<TBankCashbackContext> tbank_cashback_context;
};

void AppController::startVtbCashback() {
    if (locked_ || demo_mode_ || !vtbConnected() || vtbLoginActive() || vtbSyncRunning() || private_->vtb_cashback_sync) return;
    auto* page = static_cast<QWebView*>(private_->vtb_login->existingWebView());
    if (!page) return;
    private_->cashback_state.setStatus(QStringLiteral("vtb"), QStringLiteral("Получаем выбранные категории кэшбэка…"));
    auto* coordinator = new CashbackSync(
        [this](CashbackSnapshot snapshot) {
            auto* finished = private_->vtb_cashback_sync.data();
            private_->vtb_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.accept(QStringLiteral("vtb"), snapshot, QStringLiteral("Категории получены из ВТБ"), QDateTime::currentDateTime());
            persistBankCashback(QStringLiteral("vtb"));
            emit cashbackChanged();
        }, [this](QString error) {
            auto* finished = private_->vtb_cashback_sync.data();
            private_->vtb_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.setStatus(QStringLiteral("vtb"), std::move(error));
            emit cashbackChanged();
        }, this);
    private_->vtb_cashback_sync = coordinator;
    coordinator->startVtb(page);
    emit cashbackChanged();
}

void AppController::stopVtbCashback(const bool clearData) {
    if (private_->vtb_cashback_sync) {
        private_->vtb_cashback_sync->cancel();
        private_->vtb_cashback_sync->deleteLater();
        private_->vtb_cashback_sync.clear();
        private_->cashback_state.setStatus(QStringLiteral("vtb"), QStringLiteral("Обновление кэшбэка остановлено. Предыдущие данные сохранены."));
    }
    if (clearData) {
        private_->cashback_state.clear(QStringLiteral("vtb"));
        private_->cashback_state.setStatus(QStringLiteral("vtb"), {});
        persistBankCashback(QStringLiteral("vtb"), true);
    }
    emit cashbackChanged();
}

void AppController::initializeCashback() {
    const auto loaded = CashbackBook::parse(QSettings{}.value(QStringLiteral("cashback/categories")).toByteArray());
    if (loaded) {
        private_->cashback = *loaded;
        private_->cashback_storage_ready = true;
    } else {
        private_->cashback_error = loaded.error();
    }
    for (auto signal : {&AppController::tbankConnectionChanged, &AppController::alfaConnectionChanged,
                        &AppController::vtbConnectionChanged, &AppController::ozonConnectionChanged,
                        &AppController::demoModeChanged,
                        &AppController::lockedChanged}) {
        connect(this, signal, this, &AppController::cashbackChanged);
    }
    private_->cashback_month = CashbackBook::monthStart(QDate::currentDate());
    connect(&private_->cashback_month_timer, &QTimer::timeout, this, [this] {
        const auto month = CashbackBook::monthStart(QDate::currentDate());
        if (private_->cashback_month != month) {
            private_->cashback_month = month;
            emit cashbackChanged();
        }
    });
    private_->cashback_month_timer.start(60'000);
}

void AppController::startAlfaCashback() {
    if (locked_ || demo_mode_ || !alfaConnected() || alfaLoginActive() || alfaSyncRunning() || private_->alfa_cashback_sync) return;
    auto* page = static_cast<QWebView*>(private_->alfa_login->existingWebView());
    if (!page) return;
    private_->cashback_state.setStatus(QStringLiteral("alfa"), QStringLiteral("Получаем выбранные категории кэшбэка…"));
    auto* coordinator = new CashbackSync(
        [this](CashbackSnapshot snapshot) {
            auto* finished = private_->alfa_cashback_sync.data();
            private_->alfa_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.accept(QStringLiteral("alfa"), snapshot, QStringLiteral("Категории получены из Альфа-Банка"), QDateTime::currentDateTime());
            persistBankCashback(QStringLiteral("alfa"));
            emit cashbackChanged();
        }, [this](QString error) {
            auto* finished = private_->alfa_cashback_sync.data();
            private_->alfa_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.setStatus(QStringLiteral("alfa"), std::move(error));
            emit cashbackChanged();
        },this);
    private_->alfa_cashback_sync = coordinator;
    coordinator->startAlfa(page);
    emit cashbackChanged();
}

void AppController::stopAlfaCashback(const bool clearData) {
    if (private_->alfa_cashback_sync) {
        private_->alfa_cashback_sync->cancel();
        private_->alfa_cashback_sync->deleteLater();
        private_->alfa_cashback_sync.clear();
        private_->cashback_state.setStatus(QStringLiteral("alfa"), QStringLiteral("Обновление кэшбэка остановлено. Предыдущие данные сохранены."));
    }
    if (clearData) {
        private_->cashback_state.clear(QStringLiteral("alfa"));
        persistBankCashback(QStringLiteral("alfa"), true);
    }
    emit cashbackChanged();
}

void AppController::startOzonCashback() {
    if (locked_ || demo_mode_ || !ozonConnected() || ozonLoginActive() || ozonSyncRunning() || private_->ozon_cashback_sync) return;
    auto* page = static_cast<QWebView*>(private_->ozon_login->existingWebView());
    if (!page) return;
    private_->cashback_state.setStatus(QStringLiteral("ozon"), QStringLiteral("Получаем выбранные категории кэшбэка…"));
    auto* coordinator = new CashbackSync(
        [this](CashbackSnapshot snapshot) {
            auto* finished = private_->ozon_cashback_sync.data();
            private_->ozon_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.accept(QStringLiteral("ozon"), snapshot, QStringLiteral("Категории получены из Ozon Банка"), QDateTime::currentDateTime());
            persistBankCashback(QStringLiteral("ozon"));
            emit cashbackChanged();
        }, [this](QString error) {
            auto* finished = private_->ozon_cashback_sync.data();
            private_->ozon_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.setStatus(QStringLiteral("ozon"), std::move(error));
            emit cashbackChanged();
        },this);
    private_->ozon_cashback_sync = coordinator;
    coordinator->startOzon(page);
    emit cashbackChanged();
}

void AppController::stopOzonCashback(const bool clearData) {
    if (private_->ozon_cashback_sync) {
        private_->ozon_cashback_sync->cancel();
        private_->ozon_cashback_sync->deleteLater();
        private_->ozon_cashback_sync.clear();
        private_->cashback_state.setStatus(QStringLiteral("ozon"), QStringLiteral("Обновление кэшбэка остановлено. Предыдущие данные сохранены."));
    }
    if (clearData) {
        private_->cashback_state.clear(QStringLiteral("ozon"));
        persistBankCashback(QStringLiteral("ozon"), true);
    }
    emit cashbackChanged();
}

void AppController::startTBankCashback() {
    if (locked_ || demo_mode_ || !tbankConnected() || tbankLoginActive() || tbankSyncRunning() ||
        private_->tbank_cashback_sync || !private_->tbank_cashback_context || !private_->credentials) return;
    auto credential = private_->credentials->get(QString::fromLatin1(tbankRetailCredentialKey));
    if (!credential || !*credential) {
        private_->cashback_state.setStatus(QStringLiteral("tbank"), QStringLiteral("Сессия для чтения кэшбэка Т-Банка недоступна."));
        emit cashbackChanged();
        return;
    }
    auto* coordinator = new TBankCashbackSync(*private_->network,
        [this](CashbackSnapshot snapshot) {
            auto* finished = private_->tbank_cashback_sync.data();
            private_->tbank_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.accept(QStringLiteral("tbank"), snapshot, QStringLiteral("Категории получены из Т-Банка"), QDateTime::currentDateTime());
            persistBankCashback(QStringLiteral("tbank"));
            emit cashbackChanged();
        }, [this](QString error) {
            auto* finished = private_->tbank_cashback_sync.data();
            private_->tbank_cashback_sync.clear();
            if (finished) finished->deleteLater();
            private_->cashback_state.setStatus(QStringLiteral("tbank"), std::move(error));
            emit cashbackChanged();
        },this);
    private_->tbank_cashback_sync = coordinator;
    private_->cashback_state.setStatus(QStringLiteral("tbank"), QStringLiteral("Получаем выбранные категории кэшбэка…"));
    coordinator->start(std::move(**credential),*private_->tbank_cashback_context);
    emit cashbackChanged();
}

void AppController::stopTBankCashback(const bool clearData) {
    if (private_->tbank_cashback_sync) {
        private_->tbank_cashback_sync->cancel();
        private_->tbank_cashback_sync->deleteLater();
        private_->tbank_cashback_sync.clear();
        private_->cashback_state.setStatus(QStringLiteral("tbank"), QStringLiteral("Обновление кэшбэка остановлено. Предыдущие данные сохранены."));
    }
    if (clearData) {
        private_->cashback_state.clear(QStringLiteral("tbank"));
        persistBankCashback(QStringLiteral("tbank"), true);
        private_->tbank_cashback_context.reset();
    }
    emit cashbackChanged();
}

QVariantList AppController::cashbackMonths() const {
    QVariantList result;
    const QLocale russian(QLocale::Russian);
    const auto current = CashbackBook::monthStart(QDate::currentDate());
    for (const auto& month : {current, current.addMonths(1)}) {
        result.push_back(QVariantMap{{QStringLiteral("id"), month.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("label"), russian.standaloneMonthName(month.month(), QLocale::LongFormat)
                + QLatin1Char(' ') + QString::number(month.year())}});
    }
    return result;
}

QVariantList AppController::cashbackBanks() const {
    if (locked_ || demo_mode_) return {};
    QVariantList result;
    const auto current = CashbackBook::monthStart(QDate::currentDate());
    const auto append = [&](const QString& id, const QString& name, bool connected, bool running,
                            const QString& lastSync, const QString& status) {
        const auto* automatic = private_->cashback_state.find(id);
        const bool currentFromBank = automatic && automatic->months.contains(current);
        const bool nextFromBank = automatic && automatic->months.contains(current.addMonths(1));
        const auto cashbackStatus = private_->cashback_state.status(id);
        result.push_back(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("connected"), connected}, {QStringLiteral("syncRunning"), running},
            {QStringLiteral("lastSync"), lastSync}, {QStringLiteral("syncStatus"), status},
            {QStringLiteral("cashbackRunning"), (id == QStringLiteral("vtb") && !private_->vtb_cashback_sync.isNull()) ||
                (id == QStringLiteral("alfa") && !private_->alfa_cashback_sync.isNull()) ||
                (id == QStringLiteral("ozon") && !private_->ozon_cashback_sync.isNull()) ||
                (id == QStringLiteral("tbank") && !private_->tbank_cashback_sync.isNull())},
            {QStringLiteral("cashbackStatus"), cashbackStatus},
            {QStringLiteral("cashbackLastSync"), private_->cashback_state.updatedAt(id).isValid()
                ? private_->cashback_state.updatedAt(id).toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")) : QString{}},
            {QStringLiteral("currentFromBank"), currentFromBank}, {QStringLiteral("nextFromBank"), nextFromBank},
            {QStringLiteral("currentLastSync"), currentFromBank ? automatic->observedAt.value(current).toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")) : QString{}},
            {QStringLiteral("nextLastSync"), nextFromBank ? automatic->observedAt.value(current.addMonths(1)).toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")) : QString{}},
            {QStringLiteral("currentSelectionRequired"), currentFromBank && automatic->selectionRequired.contains(current)},
            {QStringLiteral("nextSelectionRequired"), nextFromBank && automatic->selectionRequired.contains(current.addMonths(1))},
            {QStringLiteral("current"), currentFromBank ? automatic->rows(id, current) : private_->cashback.rows(id, current)},
            {QStringLiteral("next"), nextFromBank ? automatic->rows(id, current.addMonths(1)) : private_->cashback.rows(id, current.addMonths(1))}});
    };
    append(QStringLiteral("tbank"), QStringLiteral("Т-Банк"), tbankConnected(), tbankSyncRunning(), tbankLastSync(), tbankLoginStatus());
    append(QStringLiteral("alfa"), QStringLiteral("Альфа-Банк"), alfaConnected(), alfaSyncRunning(), alfaLastSync(), alfaLoginStatus());
    append(QStringLiteral("vtb"), QStringLiteral("ВТБ"), vtbConnected(), vtbSyncRunning(), vtbLastSync(), vtbLoginStatus());
    append(QStringLiteral("ozon"), QStringLiteral("Ozon Банк"), ozonConnected(), ozonSyncRunning(), ozonLastSync(), ozonLoginStatus());
    return result;
}

QString AppController::cashbackError() const { return private_->cashback_error; }

bool AppController::persistCashback(const QByteArray& data) {
    QSettings settings;
    const auto previous = settings.value(QStringLiteral("cashback/categories"));
    settings.setValue(QStringLiteral("cashback/categories"), data);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (previous.isValid()) settings.setValue(QStringLiteral("cashback/categories"), previous);
        else settings.remove(QStringLiteral("cashback/categories"));
        private_->cashback_error = QStringLiteral("Не удалось сохранить категории кэшбэка.");
        emit cashbackChanged();
        return false;
    }
    return true;
}

bool AppController::saveCashbackCategory(const QString& bank, const QString& month,
    const QString& category, const QString& percent) {
    if (locked_ || demo_mode_ || !private_->cashback_storage_ready) return false;
    auto next = private_->cashback;
    const auto saved = next.set(bank, month, category, percent, QDate::currentDate());
    if (!saved) {
        private_->cashback_error = saved.error();
        emit cashbackChanged();
        return false;
    }
    if (!persistCashback(next.serialize())) return false;
    private_->cashback = std::move(next);
    private_->cashback_error.clear();
    emit cashbackChanged();
    return true;
}

bool AppController::removeCashbackCategory(const QString& bank, const QString& month,
    const QString& category) {
    if (locked_ || demo_mode_ || !private_->cashback_storage_ready) return false;
    auto next = private_->cashback;
    if (!next.remove(bank, month, category) || !persistCashback(next.serialize())) return false;
    private_->cashback = std::move(next);
    private_->cashback_error.clear();
    emit cashbackChanged();
    return true;
}

void AppController::refreshCashbackBanks() {
    for (const auto& bank : {QStringLiteral("tbank"), QStringLiteral("alfa"),
                             QStringLiteral("vtb"), QStringLiteral("ozon")})
        refreshCashbackBank(bank);
}

void AppController::refreshCashbackBank(const QString& bank) {
    if (locked_ || demo_mode_) return;
    if (bank == QStringLiteral("tbank") && tbankConnected() && !tbankSyncRunning() && !private_->tbank_cashback_sync) (void)synchronizeTBank();
    if (bank == QStringLiteral("alfa") && alfaConnected() && !alfaSyncRunning() && !private_->alfa_cashback_sync) (void)synchronizeAlfa();
    if (bank == QStringLiteral("vtb") && vtbConnected() && !vtbSyncRunning() && !private_->vtb_cashback_sync) (void)synchronizeVtb();
    if (bank == QStringLiteral("ozon") && ozonConnected() && !ozonSyncRunning() && !private_->ozon_cashback_sync) (void)synchronizeOzon();
}

QVariantMap AppController::connectionStates() const {
    return private_->connection_states;
}

void AppController::initializeBalanceHistory(const QString& path) {
    private_->history.initialize(path, private_->credentials.get(), [this](const auto& saved) {
        const auto bank = QString::fromStdString(saved.bank);
        const auto snapshot = parse_cashback_snapshot(bank, QByteArray::fromStdString(saved.payload));
        if (!snapshot) {
            private_->cashback_error = QStringLiteral("Не удалось прочитать сохранённый кэшбэк. Предыдущая запись сохранена.");
            return;
        }
        private_->cashback_state.restore(bank, *snapshot);
    });
}

void AppController::persistBankCashback(const QString& bank, const bool remove) {
    if (!private_->history.ready()) return;
    if (remove) {
        if (private_->history.removeCashback(bank)) return;
    } else {
        const auto encoded = serialize_cashback_snapshot(bank, private_->cashback_state.snapshot(bank));
        if (encoded && private_->history.saveCashback(bank, *encoded)) return;
    }
    private_->cashback_error = QStringLiteral("Не удалось сохранить кэшбэк. Последняя сохранённая запись не изменена.");
}

void AppController::restoreSavedBalances() {
    QMap<QString, const wealthtracker::storage::BalanceCapture*> latest;
    for (const auto& capture : private_->history.captures()) {
        const auto id = QString::fromStdString(capture.connection_id);
        const bool connected = id == QStringLiteral("tbank-retail") ? tbankConnected()
            : id == QStringLiteral("alfa-retail") ? alfaConnected()
            : id == QStringLiteral("vtb-retail") ? vtbConnected()
            : id == QStringLiteral("ozon-retail") ? ozonConnected()
            : connected_bank_ids_.contains(id);
        if (connected) latest.insert(id, &capture);
    }
    for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
        const auto& capture = **it;
        const auto observed = QDateTime::fromString(QString::fromStdString(capture.captured_at), Qt::ISODateWithMs);
        if (!observed.isValid()) continue;
        NormalizedBatch batch;
        for (const auto& balance : capture.balances) {
            const auto id = QString::fromStdString(balance.account_id);
            const auto currency = QString::fromStdString(balance.currency);
            batch.accounts.push_back({id, QString::fromStdString(balance.name),
                QStringLiteral("Сохранённый остаток"), currency});
            batch.balances.push_back({id, observed, {balance.amount_minor, currency}});
        }
        private_->bank_batches.insert(it.key(), std::move(batch));
        private_->history.observe(it.key(), observed);
    }
    if (!latest.isEmpty()) rebuildBankModels();
}

void AppController::recordBalanceHistory(const QString& connectorId) {
    private_->history.observe(connectorId);
    if (!private_->history.ready()) return;
    const auto batch = private_->bank_batches.constFind(connectorId);
    if (batch == private_->bank_batches.cend()) return;
    const auto configuration = bankConfiguration(connectorId);
    auto institution = configuration ? configuration->institution : retailBankInstitution(connectorId);
    if (institution.isEmpty()) institution = private_->bank_institutions.value(connectorId);
    (void)private_->history.appendBank(connectorId, institution, *batch);
    emit balanceHistoryChanged();
}

QVariantList AppController::cryptoWallets() const { return private_->wallets.rows(); }
QString AppController::cryptoStatus() const { return private_->wallets.status(); }
bool AppController::cryptoSyncRunning() const { return private_->wallets.running(); }

bool AppController::addCryptoWallet(const QString& name, const QString& value) {
    if (locked_ || demo_mode_) return false;
    if (!private_->wallets.add(name, value)) return false;
    refreshCryptoWallets();
    return true;
}

bool AppController::removeCryptoWallet(const QString& address) {
    if (locked_ || demo_mode_) return false;
    return private_->wallets.remove(address);
}

bool AppController::refreshCryptoWallets() {
    if (locked_ || demo_mode_) return false;
    return private_->wallets.refresh();
}

void AppController::stopCryptoRefresh() { private_->wallets.stop(); }

QVariantList AppController::balanceHistoryAccounts() const {
    return balance_history_accounts(private_->history.captures());
}

QVariantList AppController::balanceHistoryConnections() const {
    return balance_history_connections(private_->history.captures());
}

QStringList AppController::balanceHistoryCurrencies() const {
    return balance_history_currencies(private_->history.captures());
}

QString AppController::balanceHistoryError() const { return private_->history.error(); }

QVariantMap AppController::balanceHistoryChart(const QString& accountId, const QString& currency) const {
    return balance_history_chart(private_->history.captures(), accountId, currency);
}

QVariantMap AppController::filteredBalanceHistoryChart(const QString& accountId,
    const QString& currency, const QStringList& connectionIds, const bool restrictConnections,
    const QString& from, const QString& to, const bool daily) const {
    const auto start = QDate::fromString(from, QStringLiteral("dd.MM.yyyy"));
    const auto end = QDate::fromString(to, QStringLiteral("dd.MM.yyyy"));
    if ((!from.isEmpty() && !start.isValid()) || (!to.isEmpty() && !end.isValid())) {
        return {{QStringLiteral("error"), QStringLiteral("Укажите даты в формате ДД.ММ.ГГГГ.")}};
    }
    return balance_history_chart(private_->history.captures(), accountId, currency,
                                 {restrictConnections, connectionIds, start, end, daily});
}

QVariantMap AppController::balanceHistorySummary(const QStringList& connectionIds,
    const bool restrictConnections, const QString& from, const QString& to) const {
    const auto start = QDate::fromString(from, QStringLiteral("dd.MM.yyyy"));
    const auto end = QDate::fromString(to, QStringLiteral("dd.MM.yyyy"));
    if ((!from.isEmpty() && !start.isValid()) || (!to.isEmpty() && !end.isValid()) ||
        (start.isValid() && end.isValid() && start > end)) {
        return {{QStringLiteral("error"), QStringLiteral("Проверьте даты начала и окончания периода.")}};
    }
    QVariantList totals;
    QVariantList banks;
    QString error;
    for (const auto& currency : balanceHistoryCurrencies()) {
        const auto total = filteredBalanceHistoryChart({}, currency, connectionIds, restrictConnections, from, to, true);
        if (!total.value(QStringLiteral("error")).toString().isEmpty()) {
            error = total.value(QStringLiteral("error")).toString();
        } else if (!total.value(QStringLiteral("lastValue")).toString().isEmpty()) {
            totals.push_back(total);
        }
        for (const auto& option : balanceHistoryConnections()) {
            const auto connection = option.toMap();
            const auto id = connection.value(QStringLiteral("id")).toString();
            if (restrictConnections && !connectionIds.contains(id)) continue;
            auto bank = filteredBalanceHistoryChart({}, currency, {id}, true, from, to, true);
            if (bank.value(QStringLiteral("lastValue")).toString().isEmpty()) continue;
            bank.insert(QStringLiteral("institution"), connection.value(QStringLiteral("label")));
            banks.push_back(bank);
        }
    }
    return {{QStringLiteral("totals"), totals}, {QStringLiteral("banks"), banks},
            {QStringLiteral("error"), error}};
}

AppController::AppController(QObject* parent)
    : AppController(std::make_unique<SystemCredentialBackend>(),
                    std::make_unique<QNetworkAccessManager>(), parent,
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/balance-history.db")) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             QObject* parent)
    : AppController(std::move(credentials),
                    std::make_unique<QNetworkAccessManager>(), parent) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             std::unique_ptr<QNetworkAccessManager> network, QObject* parent)
    : AppController(std::move(credentials), std::move(network), parent, QString{}) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             std::unique_ptr<QNetworkAccessManager> network, QObject* parent,
                             const QString& historyPath)
    : QObject(parent),
      accounts_(this),
      transactions_(this),
      categories_(this),
      holdings_(this),
      bank_connections_(this),
      private_(std::make_unique<AppControllerPrivate>(std::move(credentials),
                                                      std::move(network))) {
    private_->wallets.setNotifications([this] { emit cryptoWalletsChanged(); },
        [this] { emit balanceHistoryChanged(); });
    initializeCashback();
    connect(this, &AppController::tbankConnectionChanged, this, [this] {
        emit retailConnectionChanged(QStringLiteral("tbank-retail"));
    });
    connect(this, &AppController::alfaConnectionChanged, this, [this] {
        emit retailConnectionChanged(QStringLiteral("alfa-retail"));
    });
    connect(this, &AppController::vtbConnectionChanged, this, [this] {
        emit retailConnectionChanged(QStringLiteral("vtb-retail"));
    });
    connect(this, &AppController::ozonConnectionChanged, this, [this] {
        emit retailConnectionChanged(QStringLiteral("ozon-retail"));
    });
    initializeBalanceHistory(historyPath);
    const auto defaultPeriod = defaultReportingPeriod(QDate::currentDate());
    reporting_from_ = defaultPeriod.from;
    reporting_to_ = defaultPeriod.to;
    initializeRetailCoordinators();
    QVector<ConnectionCredential> connectionCredentials;
    for (const auto& connectorId : supportedConnectorIds()) {
        const auto configuration = bankConfiguration(connectorId);
        connectionCredentials.push_back({connectorId, configuration->credentialKey});
    }
    QSettings settings;
    auto restored = restoreConnections(private_->credentials.get(), settings, connectionCredentials);
    connected_bank_ids_ = std::move(restored.apiConnectionIds);
    investment_connected_ = connected_bank_ids_.contains(QStringLiteral("t-invest"));
    private_->activity[QStringLiteral("tbank-retail")].connected = restored.tbankConnected;
    private_->activity[QStringLiteral("tbank-retail")].savedSession = restored.tbankSessionSaved;
    private_->activity[QStringLiteral("alfa-retail")].connected = restored.alfaConnected;
    private_->activity[QStringLiteral("vtb-retail")].connected = restored.vtbConnected;
    private_->activity[QStringLiteral("ozon-retail")].connected = restored.ozonConnected;
    if (restored.plugin) {
        private_->plugin.select(std::move(*restored.plugin));
        private_->plugin.setConfiguredStateKey(
            pluginStateCredentialKey(*private_->plugin.descriptor()));
        zen_plugin_id_ = private_->plugin.descriptor()->pluginId;
        zen_plugin_bundle_path_ = private_->plugin.descriptor()->bundlePath;
        zen_plugin_institution_ = std::move(restored.pluginInstitution);
        zen_plugin_fields_ = zenPluginFieldVariants(*private_->plugin.descriptor());
    }
    initializeRefreshTimers();
    refreshZenPluginAutoUpdateSchedule();
    refreshTBankAutoUpdateSchedule();
    refreshAlfaAutoUpdateSchedule();
    refreshVtbAutoUpdateSchedule();
    refreshOzonAutoUpdateSchedule();
    restoreSavedBalances();
    refreshBankConnections();
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    if (tbankConnected()) private_->pending_retail_restores.push_back(QStringLiteral("tbank"));
    if (alfaConnected()) private_->pending_retail_restores.push_back(QStringLiteral("alfa"));
    if (vtbConnected()) private_->pending_retail_restores.push_back(QStringLiteral("vtb"));
    if (ozonConnected()) private_->pending_retail_restores.push_back(QStringLiteral("ozon"));
    const auto resumeRestoration = [this] {
        if (!private_->pending_retail_restores.isEmpty()) {
            QTimer::singleShot(0, this, &AppController::restoreRetailConnections);
        }
    };
    connect(this, &AppController::tbankConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::alfaConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::vtbConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::ozonConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::zenPluginChanged, this, resumeRestoration);
    connect(this, &AppController::lockedChanged, this, resumeRestoration);
    resumeRestoration();
    private_->crypto_timer.setInterval(autoUpdateIntervalSeconds * 1000);
    connect(&private_->crypto_timer, &QTimer::timeout, this, [this] {
        if (!locked_ && !demo_mode_ && !private_->wallets.running() &&
            !private_->history.wallets().empty()) refreshCryptoWallets();
    });
    private_->crypto_timer.start();
    if (!private_->history.wallets().empty()) {
        QTimer::singleShot(0, this, [this] { refreshCryptoWallets(); });
    }
}

void AppController::initializeRetailCoordinators() {
    private_->tbank_login = std::make_unique<TBankLoginCoordinator>(
        [this](QString status) {
            tbank_login_status_ = std::move(status);
            emit tbankConnectionChanged();
        },
        [this](QByteArray session) {
            acceptTBankSession(std::move(session));
        },
        [this](QString error) {
            failTBankLogin(std::move(error));
        },
        this);
    private_->alfa_login = std::make_unique<AlfaLoginCoordinator>(
        [this](QString status) {
            alfa_login_status_ = std::move(status);
            if (alfaSyncRunning()) {
                sync_status_ = alfa_login_status_;
                emit syncStatusChanged();
            }
            emit alfaConnectionChanged();
        },
        [this](NormalizedBatch batch) {
            const auto accountCount = batch.accounts.size();
            private_->activity[QStringLiteral("alfa-retail")].refreshActive = false;
            private_->activity[QStringLiteral("alfa-retail")].loginActive = false;
            private_->activity[QStringLiteral("alfa-retail")].connected = true;
            private_->activity[QStringLiteral("alfa-retail")].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(QStringLiteral("alfa-retail"), std::move(batch));
            recordBalanceHistory(QStringLiteral("alfa-retail"));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings settings;
            settings.setValue(QStringLiteral("alfa/connected"), true);
            settings.setValue(QStringLiteral("alfa/autoUpdate"),
                              settings.value(QStringLiteral("alfa/autoUpdate"), true));
            settings.setValue(QStringLiteral("alfa/lastSyncAt"), completedAt.toString(Qt::ISODateWithMs));
            alfa_login_status_ = QStringLiteral("Остатки обновлены · счетов %1").arg(accountCount);
            sync_status_ = QStringLiteral("Альфа-Банк: остатки обновлены · счетов %1").arg(accountCount);
            scheduleAlfaAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit alfaConnectionChanged();
            QTimer::singleShot(0,this,&AppController::startAlfaCashback);
        },
        [this](AlfaLoginFailure failure) {
            private_->activity[QStringLiteral("alfa-retail")].lastRead = ConnectionReadResult::Failed;
            private_->activity[QStringLiteral("alfa-retail")].refreshActive = false;
            private_->activity[QStringLiteral("alfa-retail")].loginActive = false;
            if (private_->bank_batches.contains(QStringLiteral("alfa-retail"))) {
                failure.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
            }
            alfa_login_status_ = failure.message;
            sync_status_ = QStringLiteral("Альфа-Банк: %1").arg(failure.message);
            if (alfaConnected()) {
                scheduleAlfaAutoUpdateAfter(QDateTime::currentDateTimeUtc());
            }
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit alfaConnectionChanged();
        },
        this);
    private_->vtb_login = std::make_unique<VtbLoginCoordinator>(
        [this](QString status) {
            vtb_login_status_ = std::move(status);
            if (vtbSyncRunning()) {
                sync_status_ = vtb_login_status_;
                emit syncStatusChanged();
            }
            emit vtbConnectionChanged();
        },
        [this](NormalizedBatch batch) {
            const auto accountCount = batch.accounts.size();
            private_->activity[QStringLiteral("vtb-retail")].refreshActive = false;
            private_->activity[QStringLiteral("vtb-retail")].loginActive = false;
            private_->activity[QStringLiteral("vtb-retail")].connected = true;
            private_->activity[QStringLiteral("vtb-retail")].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(QStringLiteral("vtb-retail"), std::move(batch));
            recordBalanceHistory(QStringLiteral("vtb-retail"));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings settings;
            settings.setValue(QStringLiteral("vtb/connected"), true);
            settings.setValue(QStringLiteral("vtb/autoUpdate"),
                              settings.value(QStringLiteral("vtb/autoUpdate"), true));
            settings.setValue(QStringLiteral("vtb/lastSyncAt"), completedAt.toString(Qt::ISODateWithMs));
            vtb_login_status_ = QStringLiteral("Остатки обновлены · счетов %1").arg(accountCount);
            sync_status_ = QStringLiteral("ВТБ: остатки обновлены · счетов %1").arg(accountCount);
            scheduleVtbAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit vtbConnectionChanged();
            QTimer::singleShot(0, this, &AppController::startVtbCashback);
        },
        [this](VtbLoginFailure failure) {
            private_->activity[QStringLiteral("vtb-retail")].lastRead = ConnectionReadResult::Failed;
            private_->activity[QStringLiteral("vtb-retail")].refreshActive = false;
            private_->activity[QStringLiteral("vtb-retail")].loginActive = false;
            if (private_->bank_batches.contains(QStringLiteral("vtb-retail"))) {
                failure.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
            }
            vtb_login_status_ = failure.message;
            sync_status_ = QStringLiteral("ВТБ: %1").arg(failure.message);
            if (vtbConnected()) {
                scheduleVtbAutoUpdateAfter(QDateTime::currentDateTimeUtc());
            }
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit vtbConnectionChanged();
        },
        this);
    private_->ozon_login = std::make_unique<OzonLoginCoordinator>(
        [this](QString status) {
            ozon_login_status_ = std::move(status);
            if (ozonSyncRunning()) {
                sync_status_ = ozon_login_status_;
                emit syncStatusChanged();
            }
            emit ozonConnectionChanged();
        },
        [this](NormalizedBatch batch) {
            const auto accountCount = batch.accounts.size();
            private_->activity[QStringLiteral("ozon-retail")].refreshActive = false;
            private_->activity[QStringLiteral("ozon-retail")].loginActive = false;
            private_->activity[QStringLiteral("ozon-retail")].connected = true;
            private_->activity[QStringLiteral("ozon-retail")].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(QStringLiteral("ozon-retail"), std::move(batch));
            recordBalanceHistory(QStringLiteral("ozon-retail"));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings settings;
            settings.setValue(QStringLiteral("ozon/connected"), true);
            settings.setValue(QStringLiteral("ozon/autoUpdate"),
                              settings.value(QStringLiteral("ozon/autoUpdate"), true));
            settings.setValue(QStringLiteral("ozon/lastSyncAt"), completedAt.toString(Qt::ISODateWithMs));
            ozon_login_status_ = QStringLiteral("Остатки обновлены · счетов %1").arg(accountCount);
            sync_status_ = QStringLiteral("Ozon Банк: остатки обновлены · счетов %1").arg(accountCount);
            scheduleOzonAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit ozonConnectionChanged();
            QTimer::singleShot(0, this, &AppController::startOzonCashback);
        },
        [this](OzonLoginFailure failure) {
            private_->activity[QStringLiteral("ozon-retail")].lastRead = ConnectionReadResult::Failed;
            private_->activity[QStringLiteral("ozon-retail")].refreshActive = false;
            private_->activity[QStringLiteral("ozon-retail")].loginActive = false;
            if (private_->bank_batches.contains(QStringLiteral("ozon-retail"))) {
                failure.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
            }
            ozon_login_status_ = failure.message;
            sync_status_ = QStringLiteral("Ozon Банк: %1").arg(failure.message);
            if (ozonConnected()) {
                scheduleOzonAutoUpdateAfter(QDateTime::currentDateTimeUtc());
            }
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit ozonConnectionChanged();
        },
        this);
}

void AppController::initializeRefreshTimers() {
    private_->zen_auto_update_timer.setSingleShot(true);
    connect(&private_->zen_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (zenPluginAutoUpdate() && private_->plugin.descriptor() &&
                    !zen_plugin_running_) {
                    if (!synchronizeZenPlugin()) {
                        scheduleZenPluginAutoUpdateAfter(
                            QDateTime::currentDateTimeUtc());
                        emit zenPluginChanged();
                    }
                }
            });
    private_->tbank_auto_update_timer.setSingleShot(true);
    connect(&private_->tbank_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (tbankAutoUpdate() && tbankConnected()
                    && !tbankSyncRunning()) {
                    if ((locked_ || tbankLoginActive() || !synchronizeTBank()) &&
                        tbankAutoUpdate() && tbankConnected()) {
                        private_->next_tbank_auto_update_at = scheduleAutoUpdate(
                            private_->tbank_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit tbankConnectionChanged();
                    }
                }
            });
    private_->alfa_auto_update_timer.setSingleShot(true);
    connect(&private_->alfa_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (alfaAutoUpdate() && alfaConnected() &&
                    !alfaSyncRunning()) {
                    if ((locked_ || alfaLoginActive() || !synchronizeAlfa()) &&
                        alfaAutoUpdate() && alfaConnected()) {
                        private_->next_alfa_auto_update_at = scheduleAutoUpdate(
                            private_->alfa_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit alfaConnectionChanged();
                    }
                }
            });
    private_->vtb_auto_update_timer.setSingleShot(true);
    connect(&private_->vtb_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (vtbAutoUpdate() && vtbConnected() &&
                    !vtbSyncRunning()) {
                    if ((locked_ || vtbLoginActive() || !synchronizeVtb()) &&
                        vtbAutoUpdate() && vtbConnected()) {
                        private_->next_vtb_auto_update_at = scheduleAutoUpdate(
                            private_->vtb_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit vtbConnectionChanged();
                    }
                }
            });
    private_->ozon_auto_update_timer.setSingleShot(true);
    connect(&private_->ozon_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (ozonAutoUpdate() && ozonConnected() &&
                    !ozonSyncRunning()) {
                    if ((locked_ || ozonLoginActive() || !synchronizeOzon()) &&
                        ozonAutoUpdate() && ozonConnected()) {
                        private_->next_ozon_auto_update_at = scheduleAutoUpdate(
                            private_->ozon_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit ozonConnectionChanged();
                    }
                }
            });
}

void AppController::restoreRetailConnections() {
    if (locked_ || zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() ||
        vtbLoginActive() || ozonLoginActive()) {
        return;
    }
    while (!private_->pending_retail_restores.isEmpty()) {
        const auto bank = private_->pending_retail_restores.takeFirst();
        if (bank == QStringLiteral("tbank") && tbankConnected()) (void)synchronizeTBank();
        if (bank == QStringLiteral("alfa") && alfaConnected()) (void)synchronizeAlfa();
        if (bank == QStringLiteral("vtb") && vtbConnected()) (void)synchronizeVtb();
        if (bank == QStringLiteral("ozon") && ozonConnected()) (void)synchronizeOzon();
    }
}

AppController::~AppController() {
    private_->wallets.cancelPending();
}

QAbstractItemModel* AppController::accounts() noexcept { return &accounts_; }
QAbstractItemModel* AppController::transactions() noexcept { return &transactions_; }
QAbstractItemModel* AppController::categories() noexcept { return &categories_; }
QAbstractItemModel* AppController::holdings() noexcept { return &holdings_; }
QAbstractItemModel* AppController::bankConnections() noexcept { return &bank_connections_; }

QString AppController::netWorth() const {
    return demo_mode_ ? QStringLiteral("4 826 450 ₽") : live_net_worth_;
}

QString AppController::netWorthUpdatedAt() const {
    if (demo_mode_ || locked_) return {};
    QDateTime oldest;
    for (auto it = private_->history.observations().cbegin(); it != private_->history.observations().cend(); ++it) {
        if (private_->bank_batches.contains(it.key()) && it->isValid() && (!oldest.isValid() || *it < oldest))
            oldest = *it;
    }
    return oldest.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

QString AppController::reportIncome() const {
    return demo_mode_ ? QStringLiteral("284 000 ₽") : live_report_income_;
}

QString AppController::reportSpending() const {
    return demo_mode_ ? QStringLiteral("146 830 ₽") : live_report_spending_;
}

QString AppController::savings() const {
    return demo_mode_ ? QStringLiteral("137 170 ₽") : live_savings_;
}

QString AppController::reportingFrom() const {
    return reportingInputDate(reporting_from_);
}

QString AppController::reportingTo() const {
    return reportingInputDate(reporting_to_);
}

QString AppController::reportingPeriodLabel() const {
    return wealthtracker::app::reportingPeriodLabel({reporting_from_, reporting_to_});
}

QString AppController::reportingPeriodError() const {
    return reporting_period_error_;
}

QString AppController::portfolioValue() const {
    return demo_mode_ ? QStringLiteral("2 318 900 ₽") : live_portfolio_value_;
}

QString AppController::syncStatus() const { return sync_status_; }
bool AppController::locked() const noexcept { return locked_; }
bool AppController::demoMode() const noexcept { return demo_mode_; }
bool AppController::hasFinancialData() const noexcept {
    return accounts_.rowCount() > 0;
}
bool AppController::investmentConnected() const noexcept {
    return investment_connected_;
}
QString AppController::connectionMessage() const { return connection_message_; }
QVariantList AppController::zenPluginFields() const {
    return zen_plugin_fields_;
}
QString AppController::zenPluginId() const { return zen_plugin_id_; }
QString AppController::zenPluginBundlePath() const {
    return zen_plugin_bundle_path_;
}
QString AppController::zenPluginInstitution() const {
    return zen_plugin_institution_;
}
QString AppController::zenPluginPrompt() const {
    return zen_plugin_prompt_;
}
bool AppController::zenPluginPromptSecret() const noexcept {
    return zen_plugin_prompt_secret_;
}
bool AppController::zenPluginRunning() const noexcept {
    return zen_plugin_running_;
}

bool AppController::zenPluginAutoUpdate() const {
    return QSettings{}.value(QStringLiteral("zenPlugin/autoUpdate"), false)
        .toBool();
}

QString AppController::zenPluginLastSync() const {
    const auto lastSync =
        settingsDateTime(QStringLiteral("zenPlugin/lastSyncAt"));
    return lastSync ? formatSyncDateTime(*lastSync)
                    : QStringLiteral("Нет успешного обновления");
}

QString AppController::zenPluginNextSync() const {
    if (!zenPluginAutoUpdate()) {
        return QStringLiteral("Выключено");
    }
    if (!private_->plugin.descriptor()) {
        return QStringLiteral("Выберите bundle");
    }
    if (!private_->next_zen_auto_update_at.isValid()) {
        return QStringLiteral("После первого успешного запуска");
    }
    return formatSyncDateTime(private_->next_zen_auto_update_at);
}

QVariantMap AppController::retailConnectionState(const QString& connectorId) {
    const auto snapshot = [](bool connected, bool loginActive, bool syncRunning,
                             bool autoUpdate, const QString& lastSync, const QString& nextSync,
                             const QString& status, QWindow* webView, bool certificateHelp = false) {
        return QVariantMap{{QStringLiteral("supported"), true},
            {QStringLiteral("connected"), connected}, {QStringLiteral("loginActive"), loginActive},
            {QStringLiteral("syncRunning"), syncRunning}, {QStringLiteral("autoUpdate"), autoUpdate},
            {QStringLiteral("lastSync"), lastSync}, {QStringLiteral("nextSync"), nextSync},
            {QStringLiteral("loginStatus"), status},
            {QStringLiteral("webView"), QVariant::fromValue(webView)},
            {QStringLiteral("certificateHelpRequired"), certificateHelp}};
    };
    if (connectorId == QStringLiteral("tbank-retail"))
        return snapshot(tbankConnected(), tbankLoginActive(), tbankSyncRunning(),
            tbankAutoUpdate(), tbankLastSync(), tbankNextSync(), tbankLoginStatus(),
            tbankWebView());
    if (connectorId == QStringLiteral("alfa-retail"))
        return snapshot(alfaConnected(), alfaLoginActive(), alfaSyncRunning(),
            alfaAutoUpdate(), alfaLastSync(), alfaNextSync(), alfaLoginStatus(),
            alfaWebView(), alfaCertificateHelpRequired());
    if (connectorId == QStringLiteral("vtb-retail"))
        return snapshot(vtbConnected(), vtbLoginActive(), vtbSyncRunning(),
            vtbAutoUpdate(), vtbLastSync(), vtbNextSync(), vtbLoginStatus(),
            vtbWebView());
    if (connectorId == QStringLiteral("ozon-retail"))
        return snapshot(ozonConnected(), ozonLoginActive(), ozonSyncRunning(),
            ozonAutoUpdate(), ozonLastSync(), ozonNextSync(), ozonLoginStatus(),
            ozonWebView());
    return {};
}

void AppController::prepareRetailConnection(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::prepareTBankConnection, &AppController::prepareAlfaConnection, &AppController::prepareVtbConnection, &AppController::prepareOzonConnection});
}

void AppController::beginRetailLogin(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::beginTBankLogin, &AppController::beginAlfaLogin, &AppController::beginVtbLogin, &AppController::beginOzonLogin});
}

void AppController::reloadRetailLogin(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::reloadTBankLoginPage, &AppController::reloadAlfaLoginPage, &AppController::reloadVtbLoginPage, &AppController::reloadOzonLoginPage});
}

void AppController::cancelRetailLogin(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::cancelTBankLogin, &AppController::cancelAlfaLogin, &AppController::cancelVtbLogin, &AppController::cancelOzonLogin});
}

void AppController::synchronizeRetailConnection(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::synchronizeTBank, &AppController::synchronizeAlfa, &AppController::synchronizeVtb, &AppController::synchronizeOzon});
}

void AppController::setRetailAutoUpdate(const QString& connectorId, const bool enabled) {
    dispatchRetail(this, connectorId, std::array{&AppController::setTBankAutoUpdate, &AppController::setAlfaAutoUpdate, &AppController::setVtbAutoUpdate, &AppController::setOzonAutoUpdate}, enabled);
}

void AppController::disconnectRetailConnection(const QString& connectorId) {
    dispatchRetail(this, connectorId, std::array{&AppController::disconnectTBank, &AppController::disconnectAlfa, &AppController::disconnectVtb, &AppController::disconnectOzon});
}

bool AppController::openRetailCertificateInstructions(const QString& connectorId) {
    return connectorId == QStringLiteral("alfa-retail") && openAlfaCertificateInstructions();
}

bool AppController::tbankConnected() const noexcept {
    return private_->activity.value(QStringLiteral("tbank-retail")).connected;
}

bool AppController::tbankLoginActive() const noexcept {
    return private_->activity.value(QStringLiteral("tbank-retail")).loginActive;
}

bool AppController::tbankSyncRunning() const noexcept {
    return private_->activity.value(QStringLiteral("tbank-retail")).refreshActive;
}

bool AppController::tbankAutoUpdate() const {
    return QSettings{}.value(QStringLiteral("tbank/autoUpdate"), true).toBool();
}

QString AppController::tbankLastSync() const {
    const auto lastSync = settingsDateTime(QStringLiteral("tbank/lastSyncAt"));
    return lastSync ? formatSyncDateTime(*lastSync)
                    : QStringLiteral("Нет успешного обновления");
}

QString AppController::tbankNextSync() const {
    if (!tbankAutoUpdate()) {
        return QStringLiteral("Выключено");
    }
    if (!tbankConnected()) {
        return QStringLiteral("После подключения");
    }
    return private_->next_tbank_auto_update_at.isValid()
        ? formatSyncDateTime(private_->next_tbank_auto_update_at)
        : QStringLiteral("После успешного обновления");
}

QString AppController::tbankLoginStatus() const {
    return tbank_login_status_;
}

QUrl AppController::tbankLoginUrl() const {
    return QUrl{QStringLiteral("https://www.tbank.ru/login/")};
}

QWindow* AppController::tbankWebView() {
#ifdef Q_OS_ANDROID
    return private_->tbank_display.forView(private_->tbank_login->existingWebView());
#else
    return private_->tbank_login->existingWebView();
#endif
}

bool AppController::alfaConnected() const noexcept { return private_->activity.value(QStringLiteral("alfa-retail")).connected; }

bool AppController::alfaLoginActive() const noexcept { return private_->activity.value(QStringLiteral("alfa-retail")).loginActive; }

bool AppController::alfaSyncRunning() const noexcept {
    return private_->activity.value(QStringLiteral("alfa-retail")).refreshActive;
}

bool AppController::alfaAutoUpdate() const {
    return QSettings{}.value(QStringLiteral("alfa/autoUpdate"), true).toBool();
}

QString AppController::alfaLastSync() const {
    const auto lastSync = settingsDateTime(QStringLiteral("alfa/lastSyncAt"));
    return lastSync ? formatSyncDateTime(*lastSync)
                    : QStringLiteral("Нет успешного обновления");
}

QString AppController::alfaNextSync() const {
    if (!alfaAutoUpdate()) {
        return QStringLiteral("Выключено");
    }
    if (!alfaConnected()) {
        return QStringLiteral("После подключения");
    }
    return private_->next_alfa_auto_update_at.isValid()
        ? formatSyncDateTime(private_->next_alfa_auto_update_at)
        : QStringLiteral("После успешного обновления");
}

QString AppController::alfaLoginStatus() const { return alfa_login_status_; }

bool AppController::alfaCertificateHelpRequired() const noexcept {
    return private_->alfa_login->certificateHelpRequired();
}

QUrl AppController::alfaLoginUrl() const {
    return QUrl{QStringLiteral("https://web.alfabank.ru/")};
}

QWindow* AppController::alfaWebView() {
#ifdef Q_OS_ANDROID
    return private_->alfa_display.forView(private_->alfa_login->existingWebView());
#else
    return private_->alfa_login->existingWebView();
#endif
}

bool AppController::vtbConnected() const noexcept { return private_->activity.value(QStringLiteral("vtb-retail")).connected; }

bool AppController::vtbLoginActive() const noexcept { return private_->activity.value(QStringLiteral("vtb-retail")).loginActive; }

bool AppController::vtbSyncRunning() const noexcept {
    return private_->activity.value(QStringLiteral("vtb-retail")).refreshActive;
}

bool AppController::vtbAutoUpdate() const {
    return QSettings{}.value(QStringLiteral("vtb/autoUpdate"), true).toBool();
}

QString AppController::vtbLastSync() const {
    const auto lastSync = settingsDateTime(QStringLiteral("vtb/lastSyncAt"));
    return lastSync ? formatSyncDateTime(*lastSync)
                    : QStringLiteral("Нет успешного обновления");
}

QString AppController::vtbNextSync() const {
    if (!vtbAutoUpdate()) {
        return QStringLiteral("Выключено");
    }
    if (!vtbConnected()) {
        return QStringLiteral("После подключения");
    }
    return private_->next_vtb_auto_update_at.isValid()
        ? formatSyncDateTime(private_->next_vtb_auto_update_at)
        : QStringLiteral("После успешного обновления");
}

QString AppController::vtbLoginStatus() const { return vtb_login_status_; }

QUrl AppController::vtbLoginUrl() const {
    return QUrl{QStringLiteral("https://online.vtb.ru/")};
}

QWindow* AppController::vtbWebView() {
#ifdef Q_OS_ANDROID
    return private_->vtb_display.forView(private_->vtb_login->existingWebView());
#else
    return private_->vtb_login->existingWebView();
#endif
}

bool AppController::ozonConnected() const noexcept { return private_->activity.value(QStringLiteral("ozon-retail")).connected; }

bool AppController::ozonLoginActive() const noexcept { return private_->activity.value(QStringLiteral("ozon-retail")).loginActive; }

bool AppController::ozonSyncRunning() const noexcept {
    return private_->activity.value(QStringLiteral("ozon-retail")).refreshActive;
}

bool AppController::ozonAutoUpdate() const {
    return QSettings{}.value(QStringLiteral("ozon/autoUpdate"), true).toBool();
}

QString AppController::ozonLastSync() const {
    const auto lastSync = settingsDateTime(QStringLiteral("ozon/lastSyncAt"));
    return lastSync ? formatSyncDateTime(*lastSync)
                    : QStringLiteral("Нет успешного обновления");
}

QString AppController::ozonNextSync() const {
    if (!ozonAutoUpdate()) {
        return QStringLiteral("Выключено");
    }
    if (!ozonConnected()) {
        return QStringLiteral("После подключения");
    }
    return private_->next_ozon_auto_update_at.isValid()
        ? formatSyncDateTime(private_->next_ozon_auto_update_at)
        : QStringLiteral("После успешного обновления");
}

QString AppController::ozonLoginStatus() const { return ozon_login_status_; }

QUrl AppController::ozonLoginUrl() const {
    return QUrl{QStringLiteral("https://finance.ozon.ru/m/lk/main")};
}

QWindow* AppController::ozonWebView() {
#ifdef Q_OS_ANDROID
    return private_->ozon_display.forView(private_->ozon_login->existingWebView());
#else
    return private_->ozon_login->existingWebView();
#endif
}

void AppController::refreshBankConnections() {
    const bool externalBundleSelected = private_->plugin.descriptor().has_value();
    const auto localZenStatus = [this, externalBundleSelected] {
        if (nodeExecutable().isEmpty()) {
            return QStringLiteral("Требуется Node.js");
        }
        if (!externalBundleSelected) {
            return QStringLiteral("Выберите bundle");
        }
        if (zen_plugin_running_) {
            return QStringLiteral("Синхронизация");
        }
        return zenPluginAutoUpdate() ? QStringLiteral("Автообновление")
                                     : QStringLiteral("Настроен");
    }();
    QVector<ConnectionRow> rows{
        {QStringLiteral("tbank-retail"),
         QStringLiteral("Т-Банк"),
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         {},
         tbankConnected(),
         true},
        {QStringLiteral("alfa-retail"),
         QStringLiteral("Альфа-Банк"),
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         {},
         alfaConnected(),
         true},
        {QStringLiteral("vtb-retail"),
         QStringLiteral("ВТБ"),
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         {},
         vtbConnected(),
         true},
        {QStringLiteral("ozon-retail"),
         QStringLiteral("Ozon Банк"),
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         {},
         ozonConnected(),
         true},
        {QStringLiteral("tbank-business"),
         QStringLiteral("Т-Банк Бизнес"),
         QStringLiteral("Счета и выписки"),
         QStringLiteral("Официальный T-API · только чтение"),
         {},
         connected_bank_ids_.contains(QStringLiteral("tbank-business")),
         true},
        {QStringLiteral("alfa-api"),
         QStringLiteral("Альфа-Банк API"),
         QStringLiteral("Счета и операции"),
         QStringLiteral("Официальный B2C API · только чтение"),
         {},
         connected_bank_ids_.contains(QStringLiteral("alfa-api")),
         true},
        {QStringLiteral("modulbank"),
         QStringLiteral("Модульбанк"),
         QStringLiteral("Счета и операции"),
         QStringLiteral("Официальный API · только чтение"),
         {},
         connected_bank_ids_.contains(QStringLiteral("modulbank")),
         true},
        {QStringLiteral("sber-retail"),
         QStringLiteral("Сбер"),
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Телефон + код · локальный адаптер"),
         QStringLiteral("Нужен адаптер"),
         false,
         false},
        {QStringLiteral("t-invest"),
         QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Счета, остатки и позиции"),
         QStringLiteral("Официальный read-only API"),
         {},
         connected_bank_ids_.contains(QStringLiteral("t-invest")),
         true},
        {QStringLiteral("zenplugins-local"),
         externalBundleSelected
             ? zen_plugin_institution_
             : QStringLiteral("Локальный ZenPlugins"),
         QStringLiteral("Счета, остатки и операции"),
         QStringLiteral("Внешний bundle · локальный процесс"),
         localZenStatus,
         externalBundleSelected,
         !nodeExecutable().isEmpty()},
    };
    QVariantMap states;
    for (auto& row : rows) {
        const auto& id = row.connectorId;
        const bool retail = id.endsWith(QStringLiteral("-retail")) && row.available;
        bool busy = private_->bank_sync_coordinators.contains(id);
        bool login = false;
        bool saved = row.connected;
        if (id == QStringLiteral("tbank-retail")) {
            busy = tbankSyncRunning();
            login = tbankLoginActive();
            saved = private_->activity[QStringLiteral("tbank-retail")].savedSession || tbankConnected();
        } else if (id == QStringLiteral("alfa-retail")) {
            busy = alfaSyncRunning();
            login = alfaLoginActive();
        } else if (id == QStringLiteral("vtb-retail")) {
            busy = vtbSyncRunning();
            login = vtbLoginActive();
        } else if (id == QStringLiteral("ozon-retail")) {
            busy = ozonSyncRunning();
            login = ozonLoginActive();
        } else if (id == QStringLiteral("t-invest")) {
            busy = bool(private_->investment_sync);
        }
        const ConnectionState connection{
            .available = row.available,
            .externallyManaged = id == QStringLiteral("zenplugins-local"),
            .saved = saved,
            .loginActive = login,
            .refreshActive = busy,
            .lastRead = private_->activity.value(id).lastRead,
            .accessMaterial = retail ? ConnectionAccessMaterial::Session : ConnectionAccessMaterial::Token,
        };
        const auto presentation = connectionPresentation(connection, row.status);
        row.state = presentation.state;
        row.status = presentation.text;
        states.insert(id, QVariantMap{{QStringLiteral("text"), row.status},
                                     {QStringLiteral("state"), row.state}});
    }
    bank_connections_.replace(std::move(rows));
    if (private_->connection_states != states) {
        private_->connection_states = std::move(states);
        emit connectionStatesChanged();
    }
}

void AppController::refreshZenPluginAutoUpdateSchedule() {
    private_->zen_auto_update_timer.stop();
    private_->next_zen_auto_update_at = {};

    if (!zenPluginAutoUpdate() || !private_->plugin.descriptor()) {
        return;
    }
    const auto lastSync =
        settingsDateTime(QStringLiteral("zenPlugin/lastSyncAt"));
    if (!lastSync) {
        return;
    }
    scheduleZenPluginAutoUpdateAfter(*lastSync);
}

void AppController::scheduleZenPluginAutoUpdateAfter(const QDateTime& from) {
    if (!zenPluginAutoUpdate() || !private_->plugin.descriptor()) {
        return;
    }

    private_->next_zen_auto_update_at =
        scheduleAutoUpdate(private_->zen_auto_update_timer, from);
}

void AppController::refreshTBankAutoUpdateSchedule() {
    refreshConnectionSchedule(private_->tbank_auto_update_timer, private_->next_tbank_auto_update_at,
        tbankAutoUpdate() && tbankConnected(),
        settingsDateTime(QStringLiteral("tbank/lastSyncAt")));
}

void AppController::scheduleTBankAutoUpdateAfter(const QDateTime& from) {
    scheduleConnectionRefreshAfter(private_->tbank_auto_update_timer, private_->next_tbank_auto_update_at,
        tbankAutoUpdate() && tbankConnected(), from);
}

void AppController::refreshAlfaAutoUpdateSchedule() {
    refreshConnectionSchedule(private_->alfa_auto_update_timer, private_->next_alfa_auto_update_at,
        alfaAutoUpdate() && alfaConnected(),
        settingsDateTime(QStringLiteral("alfa/lastSyncAt")));
}

void AppController::scheduleAlfaAutoUpdateAfter(const QDateTime& from) {
    scheduleConnectionRefreshAfter(private_->alfa_auto_update_timer, private_->next_alfa_auto_update_at,
        alfaAutoUpdate() && alfaConnected(), from);
}

void AppController::refreshVtbAutoUpdateSchedule() {
    refreshConnectionSchedule(private_->vtb_auto_update_timer, private_->next_vtb_auto_update_at,
        vtbAutoUpdate() && vtbConnected(),
        settingsDateTime(QStringLiteral("vtb/lastSyncAt")));
}

void AppController::scheduleVtbAutoUpdateAfter(const QDateTime& from) {
    scheduleConnectionRefreshAfter(private_->vtb_auto_update_timer, private_->next_vtb_auto_update_at,
        vtbAutoUpdate() && vtbConnected(), from);
}

void AppController::refreshOzonAutoUpdateSchedule() {
    refreshConnectionSchedule(private_->ozon_auto_update_timer, private_->next_ozon_auto_update_at,
        ozonAutoUpdate() && ozonConnected(),
        settingsDateTime(QStringLiteral("ozon/lastSyncAt")));
}

void AppController::scheduleOzonAutoUpdateAfter(const QDateTime& from) {
    scheduleConnectionRefreshAfter(private_->ozon_auto_update_timer, private_->next_ozon_auto_update_at,
        ozonAutoUpdate() && ozonConnected(), from);
}

void AppController::loadDemoData() {
    stopAlfaCashback();
    stopOzonCashback();
    stopTBankCashback();
    stopVtbCashback();
    stopCryptoRefresh();
    demo_mode_ = true;
    const auto today = QDate::currentDate();
    const auto demoDate = [this](const int daysBeforeEnd) {
        return std::max(reporting_from_, reporting_to_.addDays(-daysBeforeEnd));
    };
    accounts_.replace({
        {QStringLiteral("Black"), QStringLiteral("Т-Банк"), QStringLiteral("Дебетовая карта"),
         QStringLiteral("328 450"), QStringLiteral("₽"), QStringLiteral("#F9C74F")},
        {QStringLiteral("Текущий счёт"), QStringLiteral("Альфа-Банк"), QStringLiteral("Счёт"),
         QStringLiteral("184 300"), QStringLiteral("₽"), QStringLiteral("#EF4444")},
        {QStringLiteral("Накопительный"), QStringLiteral("Сбер"), QStringLiteral("Накопительный счёт"),
         QStringLiteral("1 420 000"), QStringLiteral("₽"), QStringLiteral("#22C55E")},
        {QStringLiteral("Брокерский"), QStringLiteral("Т-Инвестиции"), QStringLiteral("Инвестиции"),
         QStringLiteral("2 318 900"), QStringLiteral("₽"), QStringLiteral("#60A5FA")},
        {QStringLiteral("Мультикарта"), QStringLiteral("ВТБ"), QStringLiteral("Кредитная карта"),
         QStringLiteral("574 800"), QStringLiteral("₽"), QStringLiteral("#2563EB")},
    });

    transactions_.replace({
        {QStringLiteral("Перекрёсток"), QStringLiteral("Продукты и супермаркеты"),
         QStringLiteral("Продукты"), QStringLiteral("−4 860 ₽"),
         formatTransactionDate(demoDate(0), today), false},
        {QStringLiteral("Заработная плата"), QStringLiteral("ООО «Компания»"),
         QStringLiteral("Доход"), QStringLiteral("+240 000 ₽"),
         formatTransactionDate(demoDate(2), today), true},
        {QStringLiteral("Яндекс Go"), QStringLiteral("Поездка"),
         QStringLiteral("Транспорт"), QStringLiteral("−1 170 ₽"),
         formatTransactionDate(demoDate(2), today), false},
        {QStringLiteral("Пополнение брокерского счёта"), QStringLiteral("Внутренний перевод"),
         QStringLiteral("Перевод"), QStringLiteral("−50 000 ₽"),
         formatTransactionDate(demoDate(3), today), false, true},
        {QStringLiteral("Купон ОФЗ"), QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Инвестиционный доход"), QStringLiteral("+3 480 ₽"),
         formatTransactionDate(demoDate(4), today), true},
        {QStringLiteral("ЖКХ"), QStringLiteral("Единый платёжный документ"),
         QStringLiteral("Дом"), QStringLiteral("−12 640 ₽"),
         formatTransactionDate(demoDate(6), today), false},
    });

    categories_.replace({
        {QStringLiteral("Продукты"), QStringLiteral("42 680 ₽"), 0.291, QStringLiteral("#60A5FA")},
        {QStringLiteral("Дом"), QStringLiteral("31 200 ₽"), 0.213, QStringLiteral("#A78BFA")},
        {QStringLiteral("Транспорт"), QStringLiteral("24 350 ₽"), 0.166, QStringLiteral("#34D399")},
        {QStringLiteral("Развлечения"), QStringLiteral("19 800 ₽"), 0.135, QStringLiteral("#FBBF24")},
        {QStringLiteral("Остальное"), QStringLiteral("28 800 ₽"), 0.195, QStringLiteral("#64748B")},
    });

    holdings_.replace({
        {QStringLiteral("TMOS"), QStringLiteral("Тинькофф Индекс МосБиржи"),
         QStringLiteral("684 300 ₽"), QStringLiteral("29,5%"), QStringLiteral("+18,7%"), true, 68430000, QStringLiteral("RUB")},
        {QStringLiteral("SU26238"), QStringLiteral("ОФЗ 26238"),
         QStringLiteral("532 600 ₽"), QStringLiteral("23,0%"), QStringLiteral("+7,2%"), true, 53260000, QStringLiteral("RUB")},
        {QStringLiteral("SBER"), QStringLiteral("Сбербанк"),
         QStringLiteral("408 200 ₽"), QStringLiteral("17,6%"), QStringLiteral("+24,1%"), true, 40820000, QStringLiteral("RUB")},
        {QStringLiteral("CNY"), QStringLiteral("Китайский юань"),
         QStringLiteral("321 500 ₽"), QStringLiteral("13,9%"), QStringLiteral("−2,4%"), false, 32150000, QStringLiteral("RUB")},
        {QStringLiteral("LQDT"), QStringLiteral("Ликвидность"),
         QStringLiteral("372 300 ₽"), QStringLiteral("16,0%"), QStringLiteral("+13,8%"), true, 37230000, QStringLiteral("RUB")},
    });


    sync_status_ = QStringLiteral("Т-Инвестиции: демонстрационные данные");
    emit dashboardChanged();
    emit syncStatusChanged();
    emit demoModeChanged();
}

bool AppController::setReportingPeriod(const QString& from, const QString& to) {
    const auto parsed = parseReportingPeriod(from, to);
    if (!parsed) {
        reporting_period_error_ = parsed.error();
        emit reportingPeriodChanged();
        return false;
    }
    if (demo_mode_) {
        reporting_period_error_ = QStringLiteral("Отключите демо-данные, чтобы изменить период");
        emit reportingPeriodChanged();
        return false;
    }
    reporting_period_error_.clear();
    reporting_from_ = parsed->from;
    reporting_to_ = parsed->to;
    rebuildBankModels();
    emit reportingPeriodChanged();
    return true;
}

void AppController::resetReportingPeriod() {
    if (demo_mode_) {
        reporting_period_error_ = QStringLiteral("Отключите демо-данные, чтобы изменить период");
        emit reportingPeriodChanged();
        return;
    }
    const auto period = defaultReportingPeriod(QDate::currentDate());
    reporting_period_error_.clear();
    reporting_from_ = period.from;
    reporting_to_ = period.to;
    rebuildBankModels();
    emit reportingPeriodChanged();
}

void AppController::clearDemoData() {
    if (!private_->bank_batches.isEmpty()) {
        rebuildBankModels();
        sync_status_ =
            QStringLiteral("Синхронизированные банковские данные восстановлены");
        emit syncStatusChanged();
        return;
    }
    demo_mode_ = false;
    accounts_.replace({});
    transactions_.replace({});
    categories_.replace({});
    holdings_.replace({});
    live_net_worth_ = QStringLiteral("0 ₽");
    live_portfolio_value_ = QStringLiteral("0 ₽");
    live_report_income_ = QStringLiteral("0 ₽");
    live_report_spending_ = QStringLiteral("0 ₽");
    live_savings_ = QStringLiteral("0 ₽");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    emit dashboardChanged();
    emit syncStatusChanged();
    emit demoModeChanged();
}

void AppController::lock() {
    stopAlfaCashback();
    stopOzonCashback();
    stopTBankCashback();
    stopVtbCashback();
    stopCryptoRefresh();
    if (tbankLoginActive()) {
        cancelTBankLogin();
    }
    if (alfaLoginActive() || alfaSyncRunning()) {
        private_->alfa_login->cancel();
        private_->activity[QStringLiteral("alfa-retail")].loginActive = false;
        private_->activity[QStringLiteral("alfa-retail")].refreshActive = false;
        alfa_login_status_ = QStringLiteral("Вход или синхронизация остановлены экраном приватности");
        emit alfaConnectionChanged();
    }
    if (vtbLoginActive() || vtbSyncRunning()) {
        private_->vtb_login->cancel();
        private_->activity[QStringLiteral("vtb-retail")].loginActive = false;
        private_->activity[QStringLiteral("vtb-retail")].refreshActive = false;
        vtb_login_status_ =
            QStringLiteral("Вход или синхронизация остановлены экраном приватности");
        emit vtbConnectionChanged();
    }
    if (ozonLoginActive() || ozonSyncRunning()) {
        private_->ozon_login->cancel();
        private_->activity[QStringLiteral("ozon-retail")].loginActive = false;
        private_->activity[QStringLiteral("ozon-retail")].refreshActive = false;
        ozon_login_status_ =
            QStringLiteral("Вход или синхронизация остановлены экраном приватности");
        emit ozonConnectionChanged();
    }
    if (zen_plugin_running_) {
        cancelZenPlugin();
    }
    if (private_->investment_sync) {
        private_->investment_sync->cancel();
        private_->investment_sync.clear();
    }
    for (auto coordinator = private_->bank_sync_coordinators.begin();
         coordinator != private_->bank_sync_coordinators.end(); ++coordinator) {
        if (*coordinator) {
            (*coordinator)->cancel();
        }
    }
    private_->bank_sync_coordinators.clear();
    if (private_->tbank_sync_coordinator) {
        private_->tbank_sync_coordinator->cancel();
        private_->tbank_sync_coordinator.clear();
        private_->activity[QStringLiteral("tbank-retail")].refreshActive = false;
        emit tbankConnectionChanged();
    }
    refreshBankConnections();
    if (!locked_) {
        locked_ = true;
        emit lockedChanged();
    }
}

bool AppController::unlock() {
    if (locked_) {
        locked_ = false;
        emit lockedChanged();
    }
    return true;
}

bool AppController::saveInvestmentToken(const QString& token) {
    return saveBankToken(QStringLiteral("t-invest"), token);
}

bool AppController::saveBankToken(const QString& connectorId,
                                  const QString& token) {
    const auto configuration = bankConfiguration(connectorId);
    if (!configuration) {
        sync_status_ = QStringLiteral("Неизвестный банковский коннектор");
        emit syncStatusChanged();
        return false;
    }
    if (!private_->credentials || !private_->credentials->supported()) {
        sync_status_ =
            QStringLiteral("Системное хранилище секретов недоступно");
        emit syncStatusChanged();
        return false;
    }

    QByteArray encoded = token.trimmed().toUtf8();
    const bool containsWhitespace =
        std::ranges::any_of(encoded, [](const char value) {
            return static_cast<unsigned char>(value) <= 0x20;
        });
    if (encoded.size() < configuration->minimumTokenSize ||
        encoded.size() > 16 * 1024 || containsWhitespace) {
        encoded.fill('\0');
        sync_status_ =
            QStringLiteral("Токен имеет недопустимый формат или длину");
        emit syncStatusChanged();
        return false;
    }
    const auto result = private_->credentials->put(
        configuration->credentialKey, encoded);
    encoded.fill('\0');
    if (!result) {
        sync_status_ = result.error();
        emit syncStatusChanged();
        return false;
    }

    // A new token must not be combined with responses obtained using the old token.
    if (connectorId == QStringLiteral("t-invest") && private_->investment_sync) {
        private_->investment_sync->cancel();
        private_->investment_sync.clear();
    }
    if (auto coordinator = private_->bank_sync_coordinators.take(connectorId)) {
        coordinator->cancel();
    }
    private_->bank_batches.remove(connectorId);
    private_->activity.resetRead(connectorId);
    if (!demo_mode_) rebuildBankModels();
    const bool investmentChanged =
        connectorId == QStringLiteral("t-invest") &&
        !investment_connected_;
    connected_bank_ids_.insert(connectorId);
    investment_connected_ =
        connected_bank_ids_.contains(QStringLiteral("t-invest"));
    sync_status_ =
        QStringLiteral("%1: токен сохранён в системном хранилище")
            .arg(configuration->institution);
    refreshBankConnections();
    if (investmentChanged) {
        emit investmentConnectedChanged();
    }
    emit syncStatusChanged();
    return true;
}

void AppController::removeInvestmentToken() {
    removeBankToken(QStringLiteral("t-invest"));
}

void AppController::removeBankToken(const QString& connectorId) {
    const auto configuration = bankConfiguration(connectorId);
    if (!configuration) {
        sync_status_ = QStringLiteral("Неизвестный банковский коннектор");
        emit syncStatusChanged();
        return;
    }
    if (auto coordinator = private_->bank_sync_coordinators.take(connectorId)) {
        coordinator->cancel();
    }
    if (connectorId == QStringLiteral("t-invest") && private_->investment_sync) {
        private_->investment_sync->cancel();
        private_->investment_sync.clear();
    }
    if (private_->credentials) {
        const auto removed =
            private_->credentials->remove(configuration->credentialKey);
        if (!removed) {
            sync_status_ = removed.error();
            emit syncStatusChanged();
            return;
        }
    }
    const bool investmentChanged =
        connectorId == QStringLiteral("t-invest") &&
        investment_connected_;
    connected_bank_ids_.remove(connectorId);
    investment_connected_ =
        connected_bank_ids_.contains(QStringLiteral("t-invest"));
    private_->bank_batches.remove(connectorId);
    private_->activity.resetRead(connectorId);
    if (!demo_mode_) {
        rebuildBankModels();
    }
    sync_status_ =
        QStringLiteral("%1: подключение удалено")
            .arg(configuration->institution);
    refreshBankConnections();
    if (investmentChanged) {
        emit investmentConnectedChanged();
    }
    emit syncStatusChanged();
}

void AppController::synchronizeInvestments() {
    if (locked_) {
        sync_status_ = QStringLiteral("Сначала отключите экран приватности");
        emit syncStatusChanged();
        return;
    }
    if (private_->investment_sync) {
        sync_status_ = QStringLiteral("Т-Инвестиции уже синхронизируются");
        emit syncStatusChanged();
        return;
    }
    if (!private_->credentials || !investment_connected_) {
        sync_status_ = QStringLiteral("Сначала добавьте read-only токен");
        emit syncStatusChanged();
        return;
    }
    auto* coordinator = new InvestmentSyncCoordinator(
        private_->investment,
        [this]() -> std::expected<QByteArray, QString> {
            auto loaded = private_->credentials->get(QStringLiteral("t-invest-readonly"));
            if (!loaded) return std::unexpected(loaded.error());
            if (!*loaded) return std::unexpected(QStringLiteral("Токен Т-Инвест не найден"));
            return std::move(**loaded);
        },
        [this](NormalizedBatch batch) {
            private_->investment_sync.clear();
            sync_status_ = QStringLiteral("Т-Инвестиции: счетов %1 · позиций %2")
                .arg(batch.accounts.size()).arg(batch.positions.size());
            if (!batch.warnings.isEmpty()) {
                sync_status_ += QStringLiteral(" · пропущено записей %1").arg(batch.warnings.size());
            }
            private_->activity[QStringLiteral("t-invest")].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(QStringLiteral("t-invest"), std::move(batch));
            refreshBankConnections();
            recordBalanceHistory(QStringLiteral("t-invest"));
            rebuildBankModels();
            emit syncStatusChanged();
        },
        [this](const QString& message) {
            private_->investment_sync.clear();
            sync_status_ = QStringLiteral("Ошибка Т-Инвест: %1").arg(message);
            private_->activity[QStringLiteral("t-invest")].lastRead = ConnectionReadResult::Failed;
            refreshBankConnections();
            emit syncStatusChanged();
        }, this);
    private_->investment_sync = coordinator;
    refreshBankConnections();
    sync_status_ = QStringLiteral("Получаем инвестиционные счета и остатки…");
    emit syncStatusChanged();
    coordinator->start();
}

void AppController::synchronizeBank(const QString& connectorId) {
    if (connectorId == QStringLiteral("t-invest")) {
        synchronizeInvestments();
        return;
    }

    const auto configuration = bankConfiguration(connectorId);
    if (!configuration ||
        !connected_bank_ids_.contains(connectorId) ||
        !private_->credentials) {
        sync_status_ = QStringLiteral(
            "Сначала сохраните токен для этого подключения");
        emit syncStatusChanged();
        return;
    }
    if (private_->bank_sync_coordinators.value(connectorId)) {
        sync_status_ = QStringLiteral("Синхронизация этого банка уже выполняется");
        emit syncStatusChanged();
        return;
    }

    BankRuntime runtime;
    if (connectorId == QStringLiteral("alfa-api")) {
        runtime = {
            &private_->alfa,
            100,
            [](const QByteArray& body, const QDateTime& observedAt) {
                return wealthtracker::connectors::AlfaBankClient::
                    parseAccountsResponse(body, observedAt);
            },
            [](const QByteArray& body, const QString& accountId,
               const QByteArray& cursor, const int pageSize) {
                return wealthtracker::connectors::AlfaBankClient::
                    parseOperationsResponse(
                        body, accountId,
                        cursor.isEmpty() ? 0 : cursor.toInt(), pageSize);
            },
        };
    } else if (connectorId == QStringLiteral("tbank-business")) {
        runtime = {
            &private_->tbank_business,
            1000,
            [](const QByteArray& body, const QDateTime& observedAt) {
                return wealthtracker::connectors::TBankBusinessClient::
                    parseAccountsResponse(body, observedAt);
            },
            [](const QByteArray& body, const QString& accountId,
               const QByteArray&, int) {
                return wealthtracker::connectors::TBankBusinessClient::
                    parseStatementResponse(body, accountId);
            },
        };
    } else if (connectorId == QStringLiteral("modulbank")) {
        runtime = {
            &private_->modulbank,
            50,
            [](const QByteArray& body, const QDateTime& observedAt) {
                return wealthtracker::connectors::ModulbankClient::
                    parseAccountsResponse(body, observedAt);
            },
            [](const QByteArray& body, const QString& accountId,
               const QByteArray& cursor, const int pageSize) {
                return wealthtracker::connectors::ModulbankClient::
                    parseOperationsResponse(
                        body, accountId,
                        cursor.isEmpty() ? 0 : cursor.toInt(), pageSize);
            },
        };
    } else {
        sync_status_ = QStringLiteral("Этот коннектор не поддерживает синхронизацию");
        emit syncStatusChanged();
        return;
    }

    auto secret =
        private_->credentials->get(configuration->credentialKey);
    if (!secret || !*secret) {
        sync_status_ =
            secret ? QStringLiteral("Сохранённый токен не найден")
                   : secret.error();
        emit syncStatusChanged();
        return;
    }
    QByteArray token = std::move(**secret);
    auto discovery = runtime.connector->discoverAccounts(token);
    token.fill('\0');
    if (!discovery) {
        sync_status_ = discovery.error().message;
        emit syncStatusChanged();
        return;
    }

    sync_status_ =
        QStringLiteral("%1: получаем счета…")
            .arg(configuration->institution);
    emit syncStatusChanged();

    auto* coordinator = new BankSyncCoordinator(
        std::move(runtime),
        [this, credentialKey = configuration->credentialKey]()
            -> std::expected<QByteArray, QString> {
            auto loaded = private_->credentials->get(credentialKey);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            if (!*loaded) {
                return std::unexpected(
                    QStringLiteral("Сохранённый токен был удалён"));
            }
            return std::move(**loaded);
        },
        [this, connectorId,
         institution = configuration->institution](NormalizedBatch batch) {
            private_->bank_sync_coordinators.remove(connectorId);
            const auto accountCount = batch.accounts.size();
            const auto transactionCount = batch.transactions.size();
            const auto warningCount = batch.warnings.size();
            private_->activity[connectorId].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(connectorId, std::move(batch));
            refreshBankConnections();
            recordBalanceHistory(connectorId);
            rebuildBankModels();
            sync_status_ =
                QStringLiteral("%1: счетов %2 · операций %3")
                    .arg(institution)
                    .arg(accountCount)
                    .arg(transactionCount);
            if (warningCount > 0) {
                sync_status_ +=
                    QStringLiteral(" · пропущено записей %1")
                        .arg(warningCount);
            }
            emit syncStatusChanged();
        },
        [this, connectorId, institution = configuration->institution](
            const wealthtracker::connectors::ConnectorError& error) {
            private_->bank_sync_coordinators.remove(connectorId);
            private_->activity[connectorId].lastRead = ConnectionReadResult::Failed;
            refreshBankConnections();
            sync_status_ =
                QStringLiteral("%1: %2").arg(institution, error.message);
            emit syncStatusChanged();
        },
        this);
    private_->bank_sync_coordinators.insert(connectorId, coordinator);
    refreshBankConnections();
    coordinator->start(*discovery);
}

void AppController::rebuildBankModels() {
    QHash<QString, ProjectionInstitution> institutions;
    for (auto bank = private_->bank_batches.cbegin(); bank != private_->bank_batches.cend(); ++bank) {
        const auto configuration = bankConfiguration(bank.key());
        auto institution = configuration ? configuration->institution : retailBankInstitution(bank.key());
        if (institution.isEmpty()) institution = private_->bank_institutions.value(bank.key());
        institutions.insert(bank.key(), {institution, connectorAccent(bank.key())});
    }
    auto projection = projectFinance(private_->bank_batches, institutions,
        {reporting_from_, reporting_to_}, QDate::currentDate());
    accounts_.replace(std::move(projection.accounts));
    transactions_.replace(std::move(projection.transactions));
    categories_.replace(std::move(projection.categories));
    holdings_.replace(std::move(projection.holdings));
    live_portfolio_value_ = std::move(projection.portfolioValue);
    live_net_worth_ = std::move(projection.netWorth);
    live_report_income_ = std::move(projection.income);
    live_report_spending_ = std::move(projection.spending);
    live_savings_ = std::move(projection.savings);
    const bool demoChanged = demo_mode_;
    demo_mode_ = false;
    emit dashboardChanged();
    if (demoChanged) emit demoModeChanged();
}

bool AppController::inspectZenPluginBundle(const QUrl& source) {
#ifdef Q_OS_ANDROID
    Q_UNUSED(source);
    sync_status_ = QStringLiteral("Локальные ZenPlugins доступны только в настольной версии");
    emit syncStatusChanged();
    return false;
#endif
    if (zen_plugin_running_) {
        sync_status_ =
            QStringLiteral("Сначала завершите текущую синхронизацию");
        emit syncStatusChanged();
        return false;
    }
    if (!source.isLocalFile()) {
        sync_status_ =
            QStringLiteral("Можно выбрать только локальный файл bundle");
        emit syncStatusChanged();
        return false;
    }

    auto descriptor =
        wealthtracker::connectors::inspectZenPluginBundle(
            source.toLocalFile());
    if (!descriptor) {
        sync_status_ = descriptor.error().message;
        if (!descriptor.error().detail.isEmpty()) {
            sync_status_ +=
                QStringLiteral(" · ") + descriptor.error().detail;
        }
        emit syncStatusChanged();
        return false;
    }
    if (descriptor->pluginId == QString::fromLatin1(tbankRetailPluginId)) {
        sync_status_ = QStringLiteral(
            "Имя tbank-retail зарезервировано встроенным коннектором");
        emit syncStatusChanged();
        return false;
    }

    const auto previousStateKey =
        private_->plugin.descriptor() ? pluginStateCredentialKey(*private_->plugin.descriptor())
                                 : QString{};
    const auto newStateKey = pluginStateCredentialKey(*descriptor);
    if (!previousStateKey.isEmpty() && previousStateKey != newStateKey) {
        QSettings settings;
        settings.remove(QStringLiteral("zenPlugin/preferences"));
        settings.remove(QStringLiteral("zenPlugin/lastSyncAt"));
        settings.setValue(QStringLiteral("zenPlugin/autoUpdate"), false);
    }

    private_->plugin.select(std::move(*descriptor));
    zen_plugin_id_ = private_->plugin.descriptor()->pluginId;
    zen_plugin_bundle_path_ =
        private_->plugin.descriptor()->bundlePath;
    zen_plugin_institution_ =
        zen_plugin_id_ == QStringLiteral("altabanka-rs")
        ? QStringLiteral("Alta Banka (Сербия)")
        : zen_plugin_id_;
    zen_plugin_fields_ =
        zenPluginFieldVariants(*private_->plugin.descriptor());
    refreshZenPluginAutoUpdateSchedule();
    refreshBankConnections();
    sync_status_ =
        QStringLiteral("%1: bundle проверен · полей %2")
            .arg(zen_plugin_id_)
            .arg(zen_plugin_fields_.size());
    emit zenPluginChanged();
    emit syncStatusChanged();
    return true;
}

bool AppController::startZenPlugin(
    const QString& institution, const QVariantMap& preferences) {
    if (zen_plugin_running_ || !private_->plugin.descriptor()) {
        sync_status_ = zen_plugin_running_
            ? QStringLiteral("Плагин уже выполняется")
            : QStringLiteral("Сначала выберите собранный bundle");
        emit syncStatusChanged();
        return false;
    }
    const auto node = nodeExecutable();
    if (node.isEmpty()) {
        sync_status_ =
            QStringLiteral("Для внешних JavaScript-плагинов нужен Node.js 18+");
        emit syncStatusChanged();
        return false;
    }
    if (!private_->credentials
        || !private_->credentials->supported()) {
        sync_status_ = QStringLiteral(
            "Системное хранилище секретов нужно для состояния плагина");
        emit syncStatusChanged();
        return false;
    }
    const auto displayName = institution.trimmed();
    if (displayName.isEmpty() || displayName.size() > 160) {
        sync_status_ =
            QStringLiteral("Укажите короткое название банка");
        emit syncStatusChanged();
        return false;
    }

    auto prepared = private_->plugin.prepare(preferences, private_->credentials.get());
    if (!prepared) {
        sync_status_ = prepared.error();
        emit syncStatusChanged();
        return false;
    }
    const auto safePreferences = prepared->safePreferences;
    const auto stateKey = prepared->stateKey;

    const auto pluginId = private_->plugin.descriptor()->pluginId;
    const auto batchKey =
        QStringLiteral("zen:") + pluginId;
    zen_plugin_institution_ = displayName;
    auto prompted = [this](wealthtracker::connectors::ZenPluginPrompt prompt) {
            zen_plugin_prompt_ = prompt.message;
            zen_plugin_prompt_secret_ = true;
            sync_status_ =
                QStringLiteral("Плагин ждёт одноразовый ответ");
            emit zenPluginChanged();
            emit syncStatusChanged();
        };
    auto completed = [this, batchKey, stateKey, safePreferences, pluginId,
         displayName](NormalizedBatch batch, QJsonObject newState) {
            const auto stateSaved = private_->plugin.saveState(*private_->credentials, stateKey, newState);
            const bool stored = stateSaved.stored;
            const bool oldStateRemoved = stateSaved.oldStateRemoved;

            const auto accountCount = batch.accounts.size();
            const auto transactionCount =
                batch.transactions.size();
            for (auto existing =
                     private_->bank_batches.begin();
                 existing != private_->bank_batches.end();) {
                if (existing.key().startsWith(
                        QStringLiteral("zen:"))
                    && existing.key() != batchKey) {
                    private_->bank_institutions.remove(
                        existing.key());
                    existing =
                        private_->bank_batches.erase(existing);
                } else {
                    ++existing;
                }
            }
            private_->bank_batches.insert(batchKey,
                                          std::move(batch));
            private_->bank_institutions.insert(batchKey,
                                               displayName);
            recordBalanceHistory(batchKey);
            rebuildBankModels();

            QSettings settings;
            settings.setValue(
                QStringLiteral("zenPlugin/bundlePath"),
                private_->plugin.descriptor()->bundlePath);
            settings.setValue(
                QStringLiteral("zenPlugin/institution"),
                displayName);
            settings.setValue(
                QStringLiteral("zenPlugin/preferences"),
                QJsonDocument(safePreferences).toJson(
                    QJsonDocument::Compact));
            settings.setValue(
                QStringLiteral("zenPlugin/lastSyncAt"),
                QDateTime::currentDateTimeUtc().toString(
                    Qt::ISODateWithMs));
            zen_plugin_fields_ =
                zenPluginFieldVariants(*private_->plugin.descriptor());
            zen_plugin_running_ = false;
            zen_plugin_prompt_.clear();
            private_->plugin.clearPrompt();
            sync_status_ =
                QStringLiteral("%1: счетов %2 · операций %3")
                    .arg(displayName)
                    .arg(accountCount)
                    .arg(transactionCount);
            if (!stored) {
                sync_status_ += QStringLiteral(
                    " · состояние сессии не сохранено");
            } else if (!oldStateRemoved) {
                sync_status_ += QStringLiteral(
                    " · старое состояние не удалено");
            }
            refreshZenPluginAutoUpdateSchedule();
            refreshBankConnections();
            emit zenPluginChanged();
            emit syncStatusChanged();
        };
    auto failed = [this, displayName](const QString& error) {
            zen_plugin_running_ = false;
            zen_plugin_prompt_.clear();
            private_->plugin.clearPrompt();
            sync_status_ =
                QStringLiteral("%1: %2").arg(displayName, error);
            scheduleZenPluginAutoUpdateAfter(
                QDateTime::currentDateTimeUtc());
            refreshBankConnections();
            emit zenPluginChanged();
            emit syncStatusChanged();
        };

    zen_plugin_running_ = true;
    zen_plugin_prompt_.clear();
    sync_status_ =
        QStringLiteral("%1: запускаем локальный плагин…")
            .arg(displayName);
    if (!private_->plugin.start(node, prepared->startMessage, std::move(prompted),
                                std::move(completed), std::move(failed), this)) {
        prepared->startMessage.fill('\0');
        zen_plugin_running_ = false;
        sync_status_ =
            QStringLiteral("Не удалось подготовить локальный host плагина");
        refreshBankConnections();
        emit zenPluginChanged();
        emit syncStatusChanged();
        return false;
    }
    prepared->startMessage.fill('\0');
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
    return true;
}

bool AppController::synchronizeZenPlugin() {
    if (!private_->plugin.descriptor()) {
        sync_status_ = QStringLiteral("Сначала выберите собранный bundle");
        emit syncStatusChanged();
        return false;
    }
    auto preferences = savedZenPluginPreferences().toVariantMap();
    const auto displayName =
        zen_plugin_institution_.trimmed().isEmpty()
            ? private_->plugin.descriptor()->pluginId
            : zen_plugin_institution_;
    return startZenPlugin(displayName, preferences);
}

void AppController::setZenPluginAutoUpdate(const bool enabled) {
    if (enabled && !private_->plugin.descriptor()) {
        sync_status_ =
            QStringLiteral("Сначала выберите и запустите локальный bundle");
        emit syncStatusChanged();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("zenPlugin/autoUpdate"), enabled);
    refreshZenPluginAutoUpdateSchedule();
    refreshBankConnections();
    if (enabled) {
        sync_status_ =
            settingsDateTime(QStringLiteral("zenPlugin/lastSyncAt"))
                ? QStringLiteral(
                      "Локальный плагин будет обновляться каждые %1 ч при запущенном приложении")
                      .arg(autoUpdateIntervalHours)
                : QStringLiteral(
                      "Автообновление начнётся после первого успешного запуска плагина");
    } else {
        sync_status_ =
            QStringLiteral("Автообновление локального плагина выключено");
    }
    emit zenPluginChanged();
    emit syncStatusChanged();
}

bool AppController::answerZenPluginPrompt(const QString& value) {
    const auto accepted = private_->plugin.answer(value);
    if (!accepted) {
        sync_status_ = accepted.error();
        emit syncStatusChanged();
        return false;
    }
    if (!*accepted) return false;
    zen_plugin_prompt_.clear();
    sync_status_ =
        QStringLiteral("Ответ передан локальному плагину");
    emit zenPluginChanged();
    emit syncStatusChanged();
    return true;
}

void AppController::cancelZenPlugin() {
    private_->plugin.cancel();
    zen_plugin_running_ = false;
    zen_plugin_prompt_.clear();
    private_->plugin.clearPrompt();
    sync_status_ = QStringLiteral("Синхронизация плагина отменена");
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
}

void AppController::removeZenPlugin() {
    cancelZenPlugin();
    if (private_->credentials) {
        const auto removed = private_->plugin.removeSavedState(*private_->credentials);
        if (!removed) {
            sync_status_ = removed.error();
            emit syncStatusChanged();
            return;
        }
        for (auto existing =
                 private_->bank_batches.begin();
             existing != private_->bank_batches.end();) {
            if (existing.key().startsWith(
                    QStringLiteral("zen:"))) {
                private_->bank_institutions.remove(existing.key());
                existing =
                    private_->bank_batches.erase(existing);
            } else {
                ++existing;
            }
        }
    }
    QSettings settings;
    settings.remove(QStringLiteral("zenPlugin"));
    private_->zen_auto_update_timer.stop();
    private_->next_zen_auto_update_at = {};
    private_->plugin.resetSelection();
    zen_plugin_fields_.clear();
    zen_plugin_id_.clear();
    zen_plugin_bundle_path_.clear();
    zen_plugin_institution_.clear();
    if (!demo_mode_) {
        rebuildBankModels();
    }
    sync_status_ =
        QStringLiteral("Конфигурация локального плагина удалена");
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
}

bool AppController::prepareTBankConnection() {
    const auto reportFailure = [this](QString message) {
        sync_status_ = std::move(message);
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    };
    if (zen_plugin_running_ || tbankSyncRunning() || alfaLoginActive() ||
        vtbLoginActive() || ozonLoginActive()) {
        return reportFailure(QStringLiteral(
            "Сначала завершите текущую синхронизацию"));
    }
    if (!private_->credentials || !private_->credentials->supported()) {
        return reportFailure(QStringLiteral(
            "Системное хранилище секретов недоступно"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginTBankLogin() {
#ifdef Q_OS_ANDROID
    if (!androidBankBrowserStorageSupported()) {
        tbank_login_status_ = QStringLiteral("Обновите Android System WebView: текущая версия не поддерживает безопасное хранение и удаление данных банка.");
        emit tbankConnectionChanged();
        return;
    }
#endif
    if (tbankLoginActive()) {
        tbank_login_status_ = QStringLiteral("Вход в Т-Банк уже выполняется");
        emit tbankConnectionChanged();
        return;
    }
    if (!prepareTBankConnection()) {
        return;
    }
    if (!private_->credentials || !private_->credentials->supported()) {
        tbank_login_status_ = QStringLiteral(
            "Системное хранилище секретов недоступно");
        emit tbankConnectionChanged();
        return;
    }

    private_->activity[QStringLiteral("tbank-retail")].loginActive = true;
    private_->tbank_login->start(tbankLoginUrl());
    refreshBankConnections();
    emit tbankConnectionChanged();
}

void AppController::reloadTBankLoginPage() {
    if (tbankLoginActive()) {
        private_->tbank_login->reload();
    }
}

void AppController::failTBankLogin(QString message) {
    private_->activity[QStringLiteral("tbank-retail")].loginActive = false;
    tbank_login_status_ = std::move(message);
    sync_status_ = tbank_login_status_;
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();
}

void AppController::cancelTBankLogin() {
    if (!tbankLoginActive()) {
        return;
    }
    private_->tbank_login->cancel();
    private_->activity[QStringLiteral("tbank-retail")].loginActive = false;
    tbank_login_status_ = QStringLiteral("Вход отменён");
    if (!tbankConnected() && private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation
        && private_->credentials) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
        private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
    }
    refreshBankConnections();
    emit tbankConnectionChanged();
}

void AppController::acceptTBankSession(QByteArray session) {
    const auto view = std::string_view{
        session.constData(), static_cast<std::size_t>(session.size())};
    if (!tbankLoginActive()) {
        session.fill('\0');
        return;
    }
    if (!wealthtracker::connectors::isValidTBankRetailSessionId(view)) {
        session.fill('\0');
        failTBankLogin(QStringLiteral(
            "Т-Банк вернул некорректную API-сессию"));
        return;
    }
    if (!prepareTBankConnection()) {
        session.fill('\0');
        failTBankLogin(tbank_login_status_);
        return;
    }

    const auto stored = private_->credentials->put(
        QString::fromLatin1(tbankRetailCredentialKey), session);
    session.fill('\0');
    if (!stored) {
        failTBankLogin(stored.error());
        return;
    }

    QSettings settings;
    settings.remove(QStringLiteral("tbank/lastSyncAt"));
    settings.setValue(QStringLiteral("tbank/autoUpdate"), true);
    private_->activity[QStringLiteral("tbank-retail")].connected = false;
    private_->activity[QStringLiteral("tbank-retail")].loginActive = false;
    private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = true;
    private_->activity[QStringLiteral("tbank-retail")].savedSession = true;
    tbank_login_status_ = QStringLiteral(
        "Сессия получена. Загружаем счета и остатки…");
    refreshBankConnections();
    emit tbankConnectionChanged();

    if (!synchronizeTBank()) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
        private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
        private_->activity[QStringLiteral("tbank-retail")].savedSession = false;
        failTBankLogin(sync_status_);
    }
}

bool AppController::synchronizeTBank() {
    using wealthtracker::connectors::ConnectorError;
    using wealthtracker::connectors::ErrorCode;
    using wealthtracker::connectors::TBankRetailClient;

    if (tbankSyncRunning() || tbankLoginActive()) {
        sync_status_ = QStringLiteral("Т-Банк уже выполняет вход или синхронизацию");
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    if ((!tbankConnected() && !private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation)
        || !private_->credentials) {
        sync_status_ = QStringLiteral("Сначала войдите в Т-Банк");
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || alfaLoginActive() || vtbLoginActive() || ozonLoginActive()) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }

    auto credential = private_->credentials->get(
        QString::fromLatin1(tbankRetailCredentialKey));
    if (!credential || !*credential) {
        sync_status_ = credential ? QStringLiteral("Сессия Т-Банка не найдена")
                                  : credential.error();
        private_->activity[QStringLiteral("tbank-retail")].connected = false;
        private_->activity[QStringLiteral("tbank-retail")].savedSession = false;
        private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
        refreshBankConnections();
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    auto session = std::move(**credential);
    auto discovery = private_->tbank_retail.discoverAccounts(session);
    session.fill('\0');
    if (!discovery) {
        private_->activity[QStringLiteral("tbank-retail")].lastRead = ConnectionReadResult::Failed;
        sync_status_ = discovery.error().message;
        tbank_login_status_ = sync_status_;
        refreshBankConnections();
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }

    stopTBankCashback();
    private_->tbank_cashback_context.reset();
    private_->tbank_auto_update_timer.stop();
    private_->activity[QStringLiteral("tbank-retail")].refreshActive = true;
    tbank_login_status_ = QStringLiteral("Обновляем остатки по счетам…");
    sync_status_ = QStringLiteral("Т-Банк: получаем счета…");
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();

    BankRuntime runtime{
        nullptr,
        1'000,
        [this](const QByteArray& body, const QDateTime& observedAt) {
            auto batch = TBankRetailClient::parseAccountsResponse(body, observedAt);
            if (batch) {
                auto context = tbank_cashback_context(body);
                if (context) {
                    for (const auto& account : batch->accounts) {
                        if (!account.maskedNumber.isEmpty() && context->accountIds.contains(account.externalId))
                            context->accountNames[account.externalId] = (account.name + QLatin1Char(' ') + account.maskedNumber).left(48);
                    }
                    private_->tbank_cashback_context = std::move(*context);
                }
                else private_->cashback_state.setStatus(QStringLiteral("tbank"), context.error());
            }
            return batch;
        },
        {},
    };
    runtime.maximumConcurrentRequests = 1;
    runtime.synchronizeHistory = false;
    runtime.maximumDurationMilliseconds = 10'000;
    auto* coordinator = new BankSyncCoordinator(
        std::move(runtime),
        {},
        [this](NormalizedBatch batch) {
            private_->tbank_sync_coordinator.clear();
            private_->activity[QStringLiteral("tbank-retail")].refreshActive = false;
            private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
            private_->activity[QStringLiteral("tbank-retail")].connected = true;
            const auto accountCount = batch.accounts.size();
            private_->activity[QStringLiteral("tbank-retail")].lastRead = ConnectionReadResult::Succeeded;
            private_->bank_batches.insert(QStringLiteral("tbank-retail"),
                                          std::move(batch));
            recordBalanceHistory(QStringLiteral("tbank-retail"));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings{}.setValue(
                QStringLiteral("tbank/lastSyncAt"),
                completedAt.toString(Qt::ISODateWithMs));
            tbank_login_status_ = QStringLiteral("Остатки обновлены · счетов %1").arg(accountCount);
            sync_status_ = QStringLiteral("Т-Банк: остатки обновлены · счетов %1").arg(accountCount);
            scheduleTBankAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit tbankConnectionChanged();
            QTimer::singleShot(0,this,&AppController::startTBankCashback);
        },
        [this](ConnectorError error) {
            private_->activity[QStringLiteral("tbank-retail")].lastRead = ConnectionReadResult::Failed;
            private_->tbank_sync_coordinator.clear();
            private_->activity[QStringLiteral("tbank-retail")].refreshActive = false;
            const bool authenticationRejected = error.code == ErrorCode::AuthenticationFailed;
            const bool validatedSession = private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation;
            private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
            if (authenticationRejected) {
                // A rejected request must not rewrite the Keychain during startup.
                // Keep the saved session until replacement or explicit disconnect,
                // but stop publishing data or scheduling refreshes with it.
                private_->activity[QStringLiteral("tbank-retail")].connected = false;
            }
            if (private_->bank_batches.contains(QStringLiteral("tbank-retail"))) {
                error.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
            }
            if (validatedSession && !authenticationRejected) {
                private_->activity[QStringLiteral("tbank-retail")].connected = true;
                tbank_login_status_ =
                    QStringLiteral("Т-Банк подключён, но синхронизация не завершена: %1")
                        .arg(error.message);
            } else {
                tbank_login_status_ = error.message;
            }
            sync_status_ = QStringLiteral("Т-Банк: %1").arg(error.message);
            if (tbankConnected()) {
                scheduleTBankAutoUpdateAfter(QDateTime::currentDateTimeUtc());
            }
            refreshBankConnections();
            emit syncStatusChanged();
            emit tbankConnectionChanged();
        },
        this);
    private_->tbank_sync_coordinator = coordinator;
    coordinator->start(*discovery);
    return true;
}

void AppController::setTBankAutoUpdate(const bool enabled) {
    QSettings{}.setValue(QStringLiteral("tbank/autoUpdate"), enabled);
    refreshTBankAutoUpdateSchedule();
    refreshBankConnections();
    emit tbankConnectionChanged();
}

void AppController::disconnectTBank() {
    private_->activity[QStringLiteral("tbank-retail")].savedSession = false;
    stopTBankCashback(true);
    if (private_->tbank_sync_coordinator) {
        private_->tbank_sync_coordinator->cancel();
        private_->tbank_sync_coordinator.clear();
    }
    private_->activity[QStringLiteral("tbank-retail")].refreshActive = false;
    private_->activity[QStringLiteral("tbank-retail")].pendingSessionValidation = false;
    private_->activity[QStringLiteral("tbank-retail")].connected = false;
    private_->activity[QStringLiteral("tbank-retail")].loginActive = false;
    private_->tbank_login->disconnectBank();
    private_->tbank_auto_update_timer.stop();
    private_->next_tbank_auto_update_at = {};
    if (private_->credentials) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
    }
    QSettings{}.remove(QStringLiteral("tbank"));
    private_->bank_batches.remove(QStringLiteral("tbank-retail"));
    private_->activity.resetRead(QStringLiteral("tbank-retail"));
    if (!demo_mode_) {
        rebuildBankModels();
    }
    tbank_login_status_ = QStringLiteral("Т-Банк отключён");
    sync_status_ = tbank_login_status_;
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();
}

bool AppController::prepareAlfaConnection() {
    const auto reportFailure = [this](QString message) {
        sync_status_ = std::move(message);
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    };
    if (zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() || vtbLoginActive() || ozonLoginActive()) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginAlfaLogin() {
#ifdef Q_OS_ANDROID
    if (!androidBankBrowserStorageSupported()) {
        alfa_login_status_ = QStringLiteral("Обновите Android System WebView: текущая версия не поддерживает безопасное хранение и удаление данных банка.");
        emit alfaConnectionChanged();
        return;
    }
#endif
    if (!prepareAlfaConnection()) {
        return;
    }
    stopAlfaCashback();
    private_->activity[QStringLiteral("alfa-retail")].refreshActive = false;
    private_->activity[QStringLiteral("alfa-retail")].loginActive = true;
    private_->alfa_login->startLogin(alfaLoginUrl());
    refreshBankConnections();
    emit alfaConnectionChanged();
}

void AppController::reloadAlfaLoginPage() {
    if (alfaLoginActive()) {
        private_->alfa_login->reload();
    }
}

bool AppController::openAlfaCertificateInstructions() {
    const bool opened =
        QDesktopServices::openUrl(alfa_certificate_instructions_url());
    if (!opened) {
        alfa_login_status_ = QStringLiteral(
            "Не удалось открыть инструкцию. Откройте вручную: "
            "https://alfa.bank/cert");
        emit alfaConnectionChanged();
    }
    return opened;
}

void AppController::cancelAlfaLogin() {
    if (!alfaLoginActive()) {
        return;
    }
    private_->alfa_login->cancel();
    private_->activity[QStringLiteral("alfa-retail")].loginActive = false;
    alfa_login_status_ = QStringLiteral("Вход отменён");
    refreshBankConnections();
    emit alfaConnectionChanged();
}

bool AppController::synchronizeAlfa() {
    if (alfaSyncRunning() || alfaLoginActive()) {
        sync_status_ = QStringLiteral("Альфа-Банк уже выполняет вход или синхронизацию");
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }
    if (!alfaConnected()) {
        sync_status_ = QStringLiteral("Сначала войдите в Альфа-Банк");
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || tbankLoginActive() || vtbLoginActive() || ozonLoginActive()) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }

    stopAlfaCashback();
    private_->alfa_auto_update_timer.stop();
    private_->activity[QStringLiteral("alfa-retail")].refreshActive = true;
    alfa_login_status_ = QStringLiteral("Обновляем остатки по счетам…");
    sync_status_ = QStringLiteral("Альфа-Банк: получаем счета…");
    private_->alfa_login->synchronize(QUrl{QStringLiteral("https://web.alfabank.ru/dashboard")});
    refreshBankConnections();
    emit syncStatusChanged();
    emit alfaConnectionChanged();
    return true;
}

void AppController::setAlfaAutoUpdate(const bool enabled) {
    QSettings{}.setValue(QStringLiteral("alfa/autoUpdate"), enabled);
    refreshAlfaAutoUpdateSchedule();
    refreshBankConnections();
    emit alfaConnectionChanged();
}

void AppController::disconnectAlfa() {
    stopAlfaCashback(true);
    private_->alfa_login->disconnectBank();
    private_->activity[QStringLiteral("alfa-retail")].refreshActive = false;
    private_->activity[QStringLiteral("alfa-retail")].connected = false;
    private_->activity[QStringLiteral("alfa-retail")].loginActive = false;
    private_->alfa_auto_update_timer.stop();
    private_->next_alfa_auto_update_at = {};
    QSettings{}.remove(QStringLiteral("alfa"));
    private_->bank_batches.remove(QStringLiteral("alfa-retail"));
    private_->activity.resetRead(QStringLiteral("alfa-retail"));
    if (!demo_mode_) {
        rebuildBankModels();
    }
    alfa_login_status_ = QStringLiteral("Альфа-Банк отключён");
    sync_status_ = alfa_login_status_;
    refreshBankConnections();
    emit dashboardChanged();
    emit syncStatusChanged();
    emit alfaConnectionChanged();
}

bool AppController::prepareVtbConnection() {
    const auto reportFailure = [this](QString message) {
        sync_status_ = std::move(message);
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    };
    if (zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() || vtbLoginActive() || ozonLoginActive()) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginVtbLogin() {
#ifdef Q_OS_ANDROID
    if (!androidBankBrowserStorageSupported()) {
        vtb_login_status_ = QStringLiteral("Обновите Android System WebView: текущая версия не поддерживает безопасное хранение и удаление данных банка.");
        emit vtbConnectionChanged();
        return;
    }
#endif
    if (!prepareVtbConnection()) {
        return;
    }
    stopVtbCashback();
    private_->activity[QStringLiteral("vtb-retail")].refreshActive = false;
    private_->activity[QStringLiteral("vtb-retail")].loginActive = true;
    private_->vtb_login->startLogin(vtbLoginUrl());
    refreshBankConnections();
    emit vtbConnectionChanged();
}

void AppController::reloadVtbLoginPage() {
    if (vtbLoginActive()) {
        private_->vtb_login->reload();
    }
}

void AppController::cancelVtbLogin() {
    if (!vtbLoginActive()) {
        return;
    }
    private_->vtb_login->cancel();
    private_->activity[QStringLiteral("vtb-retail")].loginActive = false;
    vtb_login_status_ = QStringLiteral("Вход отменён");
    refreshBankConnections();
    emit vtbConnectionChanged();
}

bool AppController::synchronizeVtb() {
    if (vtbSyncRunning() || vtbLoginActive()) {
        sync_status_ = QStringLiteral("ВТБ уже выполняет вход или синхронизацию");
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }
    if (!vtbConnected()) {
        sync_status_ = QStringLiteral("Сначала войдите в ВТБ");
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() || ozonLoginActive()) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }

    stopVtbCashback();
    private_->vtb_auto_update_timer.stop();
    private_->activity[QStringLiteral("vtb-retail")].refreshActive = true;
    vtb_login_status_ = QStringLiteral("Обновляем остатки по счетам…");
    sync_status_ = QStringLiteral("ВТБ: получаем счета…");
    private_->vtb_login->synchronize(vtbLoginUrl());
    refreshBankConnections();
    emit syncStatusChanged();
    emit vtbConnectionChanged();
    return true;
}

void AppController::setVtbAutoUpdate(const bool enabled) {
    QSettings{}.setValue(QStringLiteral("vtb/autoUpdate"), enabled);
    refreshVtbAutoUpdateSchedule();
    refreshBankConnections();
    emit vtbConnectionChanged();
}

void AppController::disconnectVtb() {
    stopVtbCashback(true);
    private_->vtb_login->disconnectBank();
    private_->activity[QStringLiteral("vtb-retail")].refreshActive = false;
    private_->activity[QStringLiteral("vtb-retail")].connected = false;
    private_->activity[QStringLiteral("vtb-retail")].loginActive = false;
    private_->vtb_auto_update_timer.stop();
    private_->next_vtb_auto_update_at = {};
    QSettings{}.remove(QStringLiteral("vtb"));
    private_->bank_batches.remove(QStringLiteral("vtb-retail"));
    private_->activity.resetRead(QStringLiteral("vtb-retail"));
    if (!demo_mode_) {
        rebuildBankModels();
    }
    vtb_login_status_ = QStringLiteral("ВТБ отключён");
    sync_status_ = vtb_login_status_;
    refreshBankConnections();
    emit dashboardChanged();
    emit syncStatusChanged();
    emit vtbConnectionChanged();
}

bool AppController::prepareOzonConnection() {
    const auto reportFailure = [this](QString message) {
        sync_status_ = std::move(message);
        ozon_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit ozonConnectionChanged();
        return false;
    };
    if (zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() || vtbLoginActive() || ozonLoginActive()) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginOzonLogin() {
    stopOzonCashback();
#ifdef Q_OS_ANDROID
    if (!androidBankBrowserStorageSupported()) {
        ozon_login_status_ = QStringLiteral("Обновите Android System WebView: текущая версия не поддерживает безопасное хранение и удаление данных банка.");
        emit ozonConnectionChanged();
        return;
    }
#endif
    if (!prepareOzonConnection()) {
        return;
    }
    private_->activity[QStringLiteral("ozon-retail")].refreshActive = false;
    private_->activity[QStringLiteral("ozon-retail")].loginActive = true;
    private_->ozon_login->startLogin(ozonLoginUrl());
    refreshBankConnections();
    emit ozonConnectionChanged();
}

void AppController::reloadOzonLoginPage() {
    if (ozonLoginActive()) {
        private_->ozon_login->reload();
    }
}

void AppController::cancelOzonLogin() {
    if (!ozonLoginActive()) {
        return;
    }
    private_->ozon_login->cancel();
    private_->activity[QStringLiteral("ozon-retail")].loginActive = false;
    ozon_login_status_ = QStringLiteral("Вход отменён");
    refreshBankConnections();
    emit ozonConnectionChanged();
}

bool AppController::synchronizeOzon() {
    stopOzonCashback();
    if (ozonSyncRunning() || ozonLoginActive()) {
        sync_status_ = QStringLiteral("Ozon Банк уже выполняет вход или синхронизацию");
        ozon_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit ozonConnectionChanged();
        return false;
    }
    if (!ozonConnected()) {
        sync_status_ = QStringLiteral("Сначала войдите в Ozon Банк");
        ozon_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit ozonConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || tbankLoginActive() || alfaLoginActive() || vtbLoginActive()) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        ozon_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit ozonConnectionChanged();
        return false;
    }

    private_->ozon_auto_update_timer.stop();
    private_->activity[QStringLiteral("ozon-retail")].refreshActive = true;
    ozon_login_status_ = QStringLiteral("Обновляем остатки по счетам…");
    sync_status_ = QStringLiteral("Ozon Банк: получаем счета…");
    private_->ozon_login->synchronize(ozonLoginUrl());
    refreshBankConnections();
    emit syncStatusChanged();
    emit ozonConnectionChanged();
    return true;
}

void AppController::setOzonAutoUpdate(const bool enabled) {
    QSettings{}.setValue(QStringLiteral("ozon/autoUpdate"), enabled);
    refreshOzonAutoUpdateSchedule();
    refreshBankConnections();
    emit ozonConnectionChanged();
}

void AppController::disconnectOzon() {
    stopOzonCashback(true);
    private_->ozon_login->disconnectBank();
    private_->activity[QStringLiteral("ozon-retail")].refreshActive = false;
    private_->activity[QStringLiteral("ozon-retail")].connected = false;
    private_->activity[QStringLiteral("ozon-retail")].loginActive = false;
    private_->ozon_auto_update_timer.stop();
    private_->next_ozon_auto_update_at = {};
    QSettings{}.remove(QStringLiteral("ozon"));
    private_->bank_batches.remove(QStringLiteral("ozon-retail"));
    private_->activity.resetRead(QStringLiteral("ozon-retail"));
    if (!demo_mode_) {
        rebuildBankModels();
    }
    ozon_login_status_ = QStringLiteral("Ozon Банк отключён");
    sync_status_ = ozon_login_status_;
    refreshBankConnections();
    emit dashboardChanged();
    emit syncStatusChanged();
    emit ozonConnectionChanged();
}

void AppController::describeBankConnection(const QString& connectorId) {
    if (connectorId == QStringLiteral("zenplugins-local")) {
        connection_message_ = QStringLiteral(
            "Выберите bundle, который вы самостоятельно собрали из "
            "ZenPlugins. Он запускается отдельным локальным процессом и "
            "может обращаться к сети от вашего имени. Проверяйте исходный "
            "код и commit перед запуском. Пароли и одноразовые коды не "
            "сохраняются; состояние сессии хранится в системном хранилище "
            "ключей.");
    } else if (connectorId == QStringLiteral("tbank-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход Т-Банка внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает страница банка. Приложение получает только идентификатор "
            "сессии из адреса запроса к точному API www.tbank.ru и сохраняет "
            "его в системном хранилище только после подтверждения доступа "
            "CLIENT. Браузерная сессия сохраняется между запусками и "
            "удаляется при отключении банка. Встроенный "
            "C++-коннектор запрашивает только счета и остатки.");
    } else if (connectorId == QStringLiteral("alfa-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход Альфа-Банка внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает только страница банка. Альфа-Онлайн использует "
            "браузерную сессию: приложение не читает её cookies, заголовки или "
            "хранилище и получает из страницы только ограниченный снимок счетов, "
            "остатков. Сессия остаётся в защищённом хранилище "
            "системного WebView для автообновления и удаляется при отключении.");
    } else if (connectorId == QStringLiteral("vtb-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход ВТБ внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает только страница банка. Приложение не читает cookies, "
            "поля входа или браузерное хранилище. Оно вызывает через клиент "
            "авторизованной страницы только разрешённые GET-маршруты счетов, "
            "остатков в пределах жёстких лимитов. Сессия остаётся в "
            "WebView и удаляется при отключении.");
    } else if (connectorId == QStringLiteral("ozon-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход Ozon Банка внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает только страница банка. Приложение не читает cookies, "
            "поля входа или браузерное хранилище и получает только проверенный "
            "снимок счетов и остатков. Сессия остаётся в WebView и удаляется "
            "при отключении банка.");
    } else if (connectorId == QStringLiteral("alfa-api")) {
        connection_message_ = QStringLiteral(
            "Нужен access token приложения Альфа-Банка со scope для счетов и "
            "истории операций. Доступ выдаётся через кабинет разработчика.");
    } else if (connectorId == QStringLiteral("tbank-business")) {
        connection_message_ = QStringLiteral(
            "Нужен T-API токен организации. Официальный API возвращает "
            "бизнес-счета и выписки; розничные карты этим способом недоступны.");
    } else if (connectorId == QStringLiteral("modulbank")) {
        connection_message_ = QStringLiteral(
            "Создайте в интернет-банке токен со scope account-info и "
            "operation-history. Для проверки можно использовать официальный "
            "sandbox-токен sandboxtoken.");
    } else if (connectorId == QStringLiteral("t-invest")) {
        connection_message_ =
            QStringLiteral("Т-Инвестиции подключаются официальным токеном только для чтения.");
    } else if (isRetailBankConnector(connectorId)) {
        const auto institution = retailBankInstitution(connectorId);
        connection_message_ =
            QStringLiteral(
                "%1 в розничном режиме обычно требует вход по номеру телефона "
                "и одноразовому коду. Встроенного чистого адаптера для этого "
                "банка пока нет: нужен отдельный локальный bundle или будущий "
                "адаптер, который вы сможете проверить перед запуском. После "
                "успешного первого входа WealthTracker хранит только состояние "
                "сессии в системном хранилище ключей и может автообновлять "
                "данные при запущенном приложении. Пароли и одноразовые коды "
                "не сохраняются.")
                .arg(institution);
    } else {
        connection_message_ =
            QStringLiteral(
                "Розничный банк требует отдельный локальный плагин. Плагин выполняет вход "
                "на этом компьютере, запрашивает пароль и одноразовый код только на время "
                "сессии и преобразует счета, остатки и операции в общий формат. Встроенный "
                "плагин для этого банка пока не поставляется.");
    }
    emit connectionMessageChanged();
}

}  // namespace wealthtracker::app
