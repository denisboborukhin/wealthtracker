#include "app/AppController.hpp"

#include <QDate>
#include <QCryptographicHash>
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

import wt.connectors;
import wt.alfa_login;
import wt.bank_sync;
import wt.platform;
import wt.reporting;
import wt.tbank_login;
import wt.transfer_matching;
import wt.vtb_login;
import wt.zen_plugin_runner;

namespace wealthtracker::app {
namespace {

using wealthtracker::connectors::ExternalAccount;
using wealthtracker::connectors::Money;
using wealthtracker::connectors::NormalizedBatch;
using wealthtracker::connectors::ZenPluginDescriptor;

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
    QString connectorId;
    QString credentialKey;
    QString institution;
    int minimumTokenSize = 8;
};

std::optional<BankConfiguration> bankConfiguration(const QString& connectorId) {
    if (connectorId == QStringLiteral("alfa-api")) {
        return BankConfiguration{
            connectorId, QStringLiteral("alfa-b2c-readonly"),
            QStringLiteral("Альфа-Банк"), 8};
    }
    if (connectorId == QStringLiteral("tbank-business")) {
        return BankConfiguration{
            connectorId, QStringLiteral("tbank-business-readonly"),
            QStringLiteral("Т-Банк Бизнес"), 8};
    }
    if (connectorId == QStringLiteral("modulbank")) {
        return BankConfiguration{
            connectorId, QStringLiteral("modulbank-readonly"),
            QStringLiteral("Модульбанк"), 8};
    }
    if (connectorId == QStringLiteral("t-invest")) {
        return BankConfiguration{
            connectorId, QStringLiteral("t-invest-readonly"),
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

QString formatMinorUnits(const std::int64_t minorUnits) {
    const bool negative = minorUnits < 0;
    const auto magnitude = negative
        ? static_cast<std::uint64_t>(-(minorUnits + 1)) + 1
        : static_cast<std::uint64_t>(minorUnits);
    const auto units = static_cast<qlonglong>(magnitude / 100);
    const auto cents = static_cast<unsigned>(magnitude % 100);
    auto result =
        QLocale(QLocale::Russian, QLocale::Russia).toString(units);
    result.replace(QChar(0x00a0), QLatin1Char(' '));
    result.replace(QChar(0x202f), QLatin1Char(' '));
    if (cents != 0) {
        result += QStringLiteral(",%1").arg(cents, 2, 10, QLatin1Char('0'));
    }
    return negative ? QStringLiteral("−%1").arg(result) : result;
}

QString currencySymbol(const QString& currency) {
    if (currency == QStringLiteral("RUB")) {
        return QStringLiteral("₽");
    }
    if (currency == QStringLiteral("USD")) {
        return QStringLiteral("$");
    }
    if (currency == QStringLiteral("EUR")) {
        return QStringLiteral("€");
    }
    if (currency == QStringLiteral("CNY")) {
        return QStringLiteral("¥");
    }
    return currency;
}

QString signedMoney(const Money& money) {
    const auto sign = money.minorUnits >= 0 ? QStringLiteral("+") : QString{};
    return QStringLiteral("%1%2 %3")
        .arg(sign, formatMinorUnits(money.minorUnits),
             currencySymbol(money.currency));
}

bool checkedAdd(std::int64_t& total, const std::int64_t value) {
    if ((value > 0 &&
         total > std::numeric_limits<std::int64_t>::max() - value) ||
        (value < 0 &&
         total < std::numeric_limits<std::int64_t>::min() - value)) {
        return false;
    }
    total += value;
    return true;
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

constexpr int zenAutoUpdateIntervalHours = 6;
constexpr int zenAutoUpdateIntervalSeconds =
    zenAutoUpdateIntervalHours * 60 * 60;

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

QString formatTransactionDate(const QDate& value, const QDate& today) {
    if (value == today) {
        return QStringLiteral("Сегодня");
    }
    return QLocale(QLocale::Russian, QLocale::Russia)
        .toString(value, QStringLiteral("d MMM yyyy"));
}

bool canPersistZenPreference(
    const wealthtracker::connectors::ZenPluginPreference& field) {
    using wealthtracker::connectors::ZenPluginPreferenceKind;
    return !field.sensitive && field.kind != ZenPluginPreferenceKind::Password;
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

QJsonObject persistableZenPreferences(
    const ZenPluginDescriptor& descriptor, const QJsonObject& preferences) {
    QJsonObject safe;
    for (const auto& field : descriptor.preferences) {
        if (canPersistZenPreference(field) && preferences.contains(field.key)) {
            safe.insert(field.key, preferences.value(field.key));
        }
    }
    return safe;
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

QString zenStateCredentialKey(const ZenPluginDescriptor& descriptor) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(descriptor.pluginId.toUtf8());
    hash.addData("\n");
    hash.addData(descriptor.bundlePath.toUtf8());
    return QStringLiteral("zen-plugin-state-%1")
        .arg(QString::fromLatin1(hash.result().toHex()));
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
          tbank_retail(*network) {}

    std::unique_ptr<CredentialBackend> credentials;
    std::unique_ptr<QNetworkAccessManager> network;
    wealthtracker::connectors::TInvestClient investment;
    wealthtracker::connectors::AlfaBankClient alfa;
    wealthtracker::connectors::TBankBusinessClient tbank_business;
    wealthtracker::connectors::ModulbankClient modulbank;
    wealthtracker::connectors::TBankRetailClient tbank_retail;
    QHash<QString, wealthtracker::connectors::NormalizedBatch> bank_batches;
    QHash<QString, QString> bank_institutions;
    QHash<QString, QPointer<BankSyncCoordinator>> bank_sync_coordinators;
    QPointer<QNetworkReply> investment_reply;
    std::optional<ZenPluginDescriptor> zen_descriptor;
    std::unique_ptr<ZenPluginRunner> zen_runner;
    QTimer zen_auto_update_timer;
    QDateTime next_zen_auto_update_at;
    QString zen_prompt_id;
    QString configured_zen_state_key;
    std::unique_ptr<TBankLoginCoordinator> tbank_login;
    QTimer tbank_auto_update_timer;
    QDateTime next_tbank_auto_update_at;
    QPointer<BankSyncCoordinator> tbank_sync_coordinator;
    bool tbank_sync_running = false;
    bool tbank_session_pending_validation = false;
    std::unique_ptr<AlfaLoginCoordinator> alfa_login;
    QTimer alfa_auto_update_timer;
    QDateTime next_alfa_auto_update_at;
    bool alfa_sync_running = false;
    std::unique_ptr<VtbLoginCoordinator> vtb_login;
    QTimer vtb_auto_update_timer;
    QDateTime next_vtb_auto_update_at;
    bool vtb_sync_running = false;
};

AppController::AppController(QObject* parent)
    : AppController(std::make_unique<SystemCredentialBackend>(),
                    std::make_unique<QNetworkAccessManager>(), parent) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             QObject* parent)
    : AppController(std::move(credentials),
                    std::make_unique<QNetworkAccessManager>(), parent) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             std::unique_ptr<QNetworkAccessManager> network, QObject* parent)
    : QObject(parent),
      accounts_(this),
      transactions_(this),
      categories_(this),
      holdings_(this),
      bank_connections_(this),
      private_(std::make_unique<AppControllerPrivate>(std::move(credentials),
                                                      std::move(network))) {
    const auto defaultPeriod = defaultReportingPeriod(QDate::currentDate());
    reporting_from_ = defaultPeriod.from;
    reporting_to_ = defaultPeriod.to;
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
            if (private_->alfa_sync_running) {
                sync_status_ = alfa_login_status_;
                emit syncStatusChanged();
            }
            emit alfaConnectionChanged();
        },
        [this](NormalizedBatch batch) {
            const auto accountCount = batch.accounts.size();
            const bool historyIncomplete = std::ranges::any_of(
                batch.warnings, [](const auto& warning) {
                    return warning.code == QStringLiteral("alfa-history-incomplete");
                });
            if (historyIncomplete) {
                const auto previous =
                    private_->bank_batches.constFind(QStringLiteral("alfa-retail"));
                if (previous != private_->bank_batches.cend()) {
                    (void)retainPreviousTransactionsForPartialHistory(
                        batch, *previous, QDate::currentDate().addYears(-2));
                }
            }
            const auto transactionCount = batch.transactions.size();
            const auto warningCount = std::ranges::count_if(
                batch.warnings, [](const auto& warning) {
                    return warning.code != QStringLiteral("alfa-history-incomplete");
                });
            private_->alfa_sync_running = false;
            alfa_login_active_ = false;
            alfa_connected_ = true;
            private_->bank_batches.insert(QStringLiteral("alfa-retail"), std::move(batch));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings settings;
            settings.setValue(QStringLiteral("alfa/connected"), true);
            settings.setValue(QStringLiteral("alfa/autoUpdate"),
                              settings.value(QStringLiteral("alfa/autoUpdate"), true));
            settings.setValue(QStringLiteral("alfa/lastSyncAt"), completedAt.toString(Qt::ISODate));
            alfa_login_status_ = historyIncomplete
                ? QStringLiteral(
                      "Подключено частично · счетов %1 · операций %2 · "
                      "история за 2 года неполная")
                      .arg(accountCount)
                      .arg(transactionCount)
                : QStringLiteral(
                      "Подключено · счетов %1 · операций %2 · история за 2 года")
                      .arg(accountCount)
                      .arg(transactionCount);
            sync_status_ = QStringLiteral("Альфа-Банк: счетов %1 · операций %2")
                               .arg(accountCount)
                               .arg(transactionCount);
            if (historyIncomplete) {
                sync_status_ += QStringLiteral(" · история за 2 года неполная");
            }
            if (warningCount > 0) {
                sync_status_ +=
                    QStringLiteral(" · пропущено записей %1").arg(warningCount);
            }
            scheduleAlfaAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit alfaConnectionChanged();
        },
        [this](AlfaLoginFailure failure) {
            private_->alfa_sync_running = false;
            alfa_login_active_ = false;
            if (failure.kind == AlfaLoginFailureKind::AuthenticationRequired) {
                alfa_connected_ = false;
                QSettings{}.setValue(QStringLiteral("alfa/connected"), false);
                private_->bank_batches.remove(QStringLiteral("alfa-retail"));
                if (!demo_mode_) {
                    rebuildBankModels();
                }
            }
            alfa_login_status_ = failure.message;
            sync_status_ = QStringLiteral("Альфа-Банк: %1").arg(failure.message);
            if (alfa_connected_) {
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
            if (private_->vtb_sync_running) {
                sync_status_ = vtb_login_status_;
                emit syncStatusChanged();
            }
            emit vtbConnectionChanged();
        },
        [this](NormalizedBatch batch) {
            const auto accountCount = batch.accounts.size();
            const bool historyIncomplete = std::ranges::any_of(
                batch.warnings, [](const auto& warning) {
                    return warning.code == QStringLiteral("vtb-history-incomplete");
                });
            if (historyIncomplete) {
                const auto previous =
                    private_->bank_batches.constFind(QStringLiteral("vtb-retail"));
                if (previous != private_->bank_batches.cend()) {
                    (void)retainPreviousTransactionsForPartialHistory(
                        batch, *previous, QDate::currentDate().addYears(-2));
                }
            }
            const auto transactionCount = batch.transactions.size();
            const auto warningCount = std::ranges::count_if(
                batch.warnings, [](const auto& warning) {
                    return warning.code != QStringLiteral("vtb-history-incomplete");
                });
            private_->vtb_sync_running = false;
            vtb_login_active_ = false;
            vtb_connected_ = true;
            private_->bank_batches.insert(QStringLiteral("vtb-retail"), std::move(batch));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings settings;
            settings.setValue(QStringLiteral("vtb/connected"), true);
            settings.setValue(QStringLiteral("vtb/autoUpdate"),
                              settings.value(QStringLiteral("vtb/autoUpdate"), true));
            settings.setValue(QStringLiteral("vtb/lastSyncAt"),
                              completedAt.toString(Qt::ISODate));
            vtb_login_status_ = historyIncomplete
                ? QStringLiteral(
                      "Подключено частично · счетов %1 · операций %2 · "
                      "история за 2 года неполная")
                      .arg(accountCount)
                      .arg(transactionCount)
                : QStringLiteral(
                      "Подключено · счетов %1 · операций %2 · история за 2 года")
                      .arg(accountCount)
                      .arg(transactionCount);
            sync_status_ = QStringLiteral("ВТБ: счетов %1 · операций %2")
                               .arg(accountCount)
                               .arg(transactionCount);
            if (historyIncomplete) {
                sync_status_ += QStringLiteral(" · история за 2 года неполная");
            }
            if (warningCount > 0) {
                sync_status_ +=
                    QStringLiteral(" · пропущено записей %1").arg(warningCount);
            }
            scheduleVtbAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit vtbConnectionChanged();
        },
        [this](VtbLoginFailure failure) {
            private_->vtb_sync_running = false;
            vtb_login_active_ = false;
            if (failure.kind == VtbLoginFailureKind::AuthenticationRequired) {
                vtb_connected_ = false;
                QSettings{}.setValue(QStringLiteral("vtb/connected"), false);
                private_->bank_batches.remove(QStringLiteral("vtb-retail"));
                if (!demo_mode_) {
                    rebuildBankModels();
                }
            }
            vtb_login_status_ = failure.message;
            sync_status_ = QStringLiteral("ВТБ: %1").arg(failure.message);
            if (vtb_connected_) {
                scheduleVtbAutoUpdateAfter(QDateTime::currentDateTimeUtc());
            }
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit vtbConnectionChanged();
        },
        this);
    if (private_->credentials && private_->credentials->supported()) {
        for (const auto& connectorId : supportedConnectorIds()) {
            const auto configuration = bankConfiguration(connectorId);
            auto secret =
                private_->credentials->get(configuration->credentialKey);
            if (secret && *secret) {
                connected_bank_ids_.insert(connectorId);
                (**secret).fill('\0');
            }
        }
        investment_connected_ =
            connected_bank_ids_.contains(QStringLiteral("t-invest"));

        auto retailSession = private_->credentials->get(
            QString::fromLatin1(tbankRetailCredentialKey));
        if (retailSession && *retailSession) {
            auto session = std::move(**retailSession);
            tbank_connected_ =
                wealthtracker::connectors::isValidTBankRetailSessionId(
                    std::string_view{
                        session.constData(),
                        static_cast<std::size_t>(session.size())});
            session.fill('\0');
            if (!tbank_connected_) {
                (void)private_->credentials->remove(
                    QString::fromLatin1(tbankRetailCredentialKey));
            }
        }
    }

    QSettings settings;
    alfa_connected_ = settings.value(QStringLiteral("alfa/connected"), false).toBool();
    vtb_connected_ = settings.value(QStringLiteral("vtb/connected"), false).toBool();
    const auto configuredBundle =
        settings.value(QStringLiteral("zenPlugin/bundlePath"))
            .toString();
    if (!configuredBundle.isEmpty()) {
        auto descriptor =
            wealthtracker::connectors::inspectZenPluginBundle(
                configuredBundle);
        if (descriptor
            && descriptor->pluginId == QString::fromLatin1(tbankRetailPluginId)) {
            if (private_->credentials && private_->credentials->supported()) {
                const auto legacyStateKey = zenStateCredentialKey(*descriptor);
                auto legacyState = private_->credentials->get(legacyStateKey);
                if (legacyState && *legacyState) {
                    auto encoded = std::move(**legacyState);
                    if (encoded.size() <= 1024 * 1024) {
                        const auto document = QJsonDocument::fromJson(encoded);
                        auto session = document.object()
                                           .value(QStringLiteral("pluginData"))
                                           .toObject()
                                           .value(QStringLiteral("sessionId"))
                                           .toString()
                                           .toLatin1();
                        if (wealthtracker::connectors::isValidTBankRetailSessionId(
                                std::string_view{
                                    session.constData(),
                                    static_cast<std::size_t>(session.size())})) {
                            const auto migrated = private_->credentials->put(
                                QString::fromLatin1(tbankRetailCredentialKey), session);
                            tbank_connected_ = migrated.has_value();
                        }
                        session.fill('\0');
                    }
                    encoded.fill('\0');
                }
                (void)private_->credentials->remove(legacyStateKey);
            }
            const auto legacyLastSync = settings.value(
                QStringLiteral("zenPlugin/lastSyncAt"));
            if (legacyLastSync.isValid()) {
                settings.setValue(QStringLiteral("tbank/lastSyncAt"),
                                  legacyLastSync);
            }
            settings.setValue(
                QStringLiteral("tbank/autoUpdate"),
                settings.value(QStringLiteral("zenPlugin/autoUpdate"), true));
            settings.remove(QStringLiteral("zenPlugin"));
        } else if (descriptor) {
            private_->zen_descriptor = std::move(*descriptor);
            private_->configured_zen_state_key =
                zenStateCredentialKey(*private_->zen_descriptor);
            zen_plugin_id_ = private_->zen_descriptor->pluginId;
            zen_plugin_bundle_path_ = private_->zen_descriptor->bundlePath;
            zen_plugin_institution_ = settings
                .value(QStringLiteral("zenPlugin/institution"), zen_plugin_id_)
                .toString();
            zen_plugin_fields_ =
                zenPluginFieldVariants(*private_->zen_descriptor);
        }
    }
    private_->zen_auto_update_timer.setSingleShot(true);
    connect(&private_->zen_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (zenPluginAutoUpdate() && private_->zen_descriptor &&
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
                if (tbankAutoUpdate() && tbank_connected_
                    && !private_->tbank_sync_running) {
                    (void)synchronizeTBank();
                }
            });
    private_->alfa_auto_update_timer.setSingleShot(true);
    connect(&private_->alfa_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (alfaAutoUpdate() && alfa_connected_ &&
                    !private_->alfa_sync_running && !alfa_login_active_) {
                    (void)synchronizeAlfa();
                }
            });
    private_->vtb_auto_update_timer.setSingleShot(true);
    connect(&private_->vtb_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (vtbAutoUpdate() && vtb_connected_ &&
                    !private_->vtb_sync_running && !vtb_login_active_) {
                    (void)synchronizeVtb();
                }
            });
    refreshZenPluginAutoUpdateSchedule();
    refreshTBankAutoUpdateSchedule();
    refreshAlfaAutoUpdateSchedule();
    refreshVtbAutoUpdateSchedule();
    refreshBankConnections();
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    if (tbank_connected_) {
        QTimer::singleShot(0, this, [this] { (void)synchronizeTBank(); });
    }
    if (alfa_connected_) {
        QTimer::singleShot(0, this, [this] { (void)synchronizeAlfa(); });
    }
    if (vtb_connected_) {
        QTimer::singleShot(0, this, [this] { (void)synchronizeVtb(); });
    }
}

AppController::~AppController() = default;

QAbstractItemModel* AppController::accounts() noexcept { return &accounts_; }
QAbstractItemModel* AppController::transactions() noexcept { return &transactions_; }
QAbstractItemModel* AppController::categories() noexcept { return &categories_; }
QAbstractItemModel* AppController::holdings() noexcept { return &holdings_; }
QAbstractItemModel* AppController::bankConnections() noexcept { return &bank_connections_; }
QVariantList AppController::netWorthSeries() const { return net_worth_series_; }

QString AppController::netWorth() const {
    return demo_mode_ ? QStringLiteral("4 826 450 ₽") : live_net_worth_;
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
    return demo_mode_ ? QStringLiteral("2 318 900 ₽") : QStringLiteral("0 ₽");
}

QString AppController::importStatus() const { return import_status_; }
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
    if (!private_->zen_descriptor) {
        return QStringLiteral("Выберите bundle");
    }
    if (!private_->next_zen_auto_update_at.isValid()) {
        return QStringLiteral("После первого успешного запуска");
    }
    return formatSyncDateTime(private_->next_zen_auto_update_at);
}

bool AppController::tbankConnected() const noexcept {
    return tbank_connected_;
}

bool AppController::tbankLoginActive() const noexcept {
    return tbank_login_active_;
}

bool AppController::tbankSyncRunning() const noexcept {
    return private_->tbank_sync_running;
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
    if (!tbank_connected_) {
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
    return private_->tbank_login->existingWebView();
}

bool AppController::alfaConnected() const noexcept { return alfa_connected_; }

bool AppController::alfaLoginActive() const noexcept { return alfa_login_active_; }

bool AppController::alfaSyncRunning() const noexcept {
    return private_->alfa_sync_running;
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
    if (!alfa_connected_) {
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
    return private_->alfa_login->existingWebView();
}

bool AppController::vtbConnected() const noexcept { return vtb_connected_; }

bool AppController::vtbLoginActive() const noexcept { return vtb_login_active_; }

bool AppController::vtbSyncRunning() const noexcept {
    return private_->vtb_sync_running;
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
    if (!vtb_connected_) {
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
    return private_->vtb_login->existingWebView();
}

void AppController::refreshBankConnections() {
    const bool externalBundleSelected = private_->zen_descriptor.has_value();
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
    bank_connections_.replace({
        {QStringLiteral("tbank-retail"),
         QStringLiteral("Т-Банк"),
         QStringLiteral("Карты, счета и операции"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         tbank_login_active_
             ? QStringLiteral("Вход в банк")
             : private_->tbank_sync_running
                 ? QStringLiteral("Синхронизация")
                 : tbank_connected_
                     ? tbankAutoUpdate()
                         ? QStringLiteral("Автообновление")
                         : QStringLiteral("Подключено")
                     : QStringLiteral("Требуется вход"),
         tbank_connected_,
         true},
        {QStringLiteral("alfa-retail"),
         QStringLiteral("Альфа-Банк"),
         QStringLiteral("Карты, счета и последние операции"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         alfa_login_active_
             ? QStringLiteral("Вход в банк")
             : private_->alfa_sync_running
                 ? QStringLiteral("Синхронизация")
                 : alfa_connected_
                     ? alfaAutoUpdate() ? QStringLiteral("Автообновление")
                                        : QStringLiteral("Подключено")
                     : QStringLiteral("Требуется вход"),
         alfa_connected_,
         true},
        {QStringLiteral("vtb-retail"),
         QStringLiteral("ВТБ"),
         QStringLiteral("Карты, счета и последние операции"),
         QStringLiteral("Вход внутри приложения · встроенный коннектор"),
         vtb_login_active_
             ? QStringLiteral("Вход в банк")
             : private_->vtb_sync_running
                 ? QStringLiteral("Синхронизация")
                 : vtb_connected_
                     ? vtbAutoUpdate() ? QStringLiteral("Автообновление")
                                       : QStringLiteral("Подключено")
                     : QStringLiteral("Требуется вход"),
         vtb_connected_,
         true},
        {QStringLiteral("tbank-business"),
         QStringLiteral("Т-Банк Бизнес"),
         QStringLiteral("Счета и выписки"),
         QStringLiteral("Официальный T-API · только чтение"),
         connected_bank_ids_.contains(QStringLiteral("tbank-business"))
             ? QStringLiteral("Подключено")
             : QStringLiteral("Требуется токен"),
         connected_bank_ids_.contains(QStringLiteral("tbank-business")),
         true},
        {QStringLiteral("alfa-api"),
         QStringLiteral("Альфа-Банк API"),
         QStringLiteral("Счета и операции"),
         QStringLiteral("Официальный B2C API · только чтение"),
         connected_bank_ids_.contains(QStringLiteral("alfa-api"))
             ? QStringLiteral("Подключено")
             : QStringLiteral("Требуется доступ"),
         connected_bank_ids_.contains(QStringLiteral("alfa-api")),
         true},
        {QStringLiteral("modulbank"),
         QStringLiteral("Модульбанк"),
         QStringLiteral("Счета и операции"),
         QStringLiteral("Официальный API · только чтение"),
         connected_bank_ids_.contains(QStringLiteral("modulbank"))
             ? QStringLiteral("Подключено")
             : QStringLiteral("Требуется токен"),
         connected_bank_ids_.contains(QStringLiteral("modulbank")),
         true},
        {QStringLiteral("sber-retail"),
         QStringLiteral("Сбер"),
         QStringLiteral("Карты, счета и операции"),
         QStringLiteral("Телефон + код · локальный адаптер"),
         QStringLiteral("Нужен адаптер"),
         false,
         false},
        {QStringLiteral("t-invest"),
         QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Счета, позиции и операции"),
         QStringLiteral("Официальный read-only API"),
         connected_bank_ids_.contains(QStringLiteral("t-invest"))
             ? QStringLiteral("Подключено")
             : QStringLiteral("Требуется токен"),
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
    });
}

void AppController::refreshZenPluginAutoUpdateSchedule() {
    private_->zen_auto_update_timer.stop();
    private_->next_zen_auto_update_at = {};

    if (!zenPluginAutoUpdate() || !private_->zen_descriptor) {
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
    if (!zenPluginAutoUpdate() || !private_->zen_descriptor) {
        return;
    }

    const auto now = QDateTime::currentDateTimeUtc();
    auto next = from.toUTC().addSecs(zenAutoUpdateIntervalSeconds);
    auto secondsUntilNext = now.secsTo(next);
    if (secondsUntilNext < 10) {
        secondsUntilNext = 10;
        next = now.addSecs(secondsUntilNext);
    }

    private_->next_zen_auto_update_at = next;
    const auto milliseconds =
        std::min<qint64>(secondsUntilNext * 1000LL,
                         std::numeric_limits<int>::max());
    private_->zen_auto_update_timer.start(static_cast<int>(milliseconds));
}

void AppController::refreshTBankAutoUpdateSchedule() {
    private_->tbank_auto_update_timer.stop();
    private_->next_tbank_auto_update_at = {};
    if (!tbankAutoUpdate() || !tbank_connected_) {
        return;
    }
    if (const auto lastSync = settingsDateTime(
            QStringLiteral("tbank/lastSyncAt"))) {
        scheduleTBankAutoUpdateAfter(*lastSync);
    }
}

void AppController::scheduleTBankAutoUpdateAfter(const QDateTime& from) {
    if (!tbankAutoUpdate() || !tbank_connected_) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    auto next = from.toUTC().addSecs(zenAutoUpdateIntervalSeconds);
    auto secondsUntilNext = now.secsTo(next);
    if (secondsUntilNext < 10) {
        secondsUntilNext = 10;
        next = now.addSecs(secondsUntilNext);
    }
    private_->next_tbank_auto_update_at = next;
    const auto milliseconds = std::min<qint64>(
        secondsUntilNext * 1000LL, std::numeric_limits<int>::max());
    private_->tbank_auto_update_timer.start(static_cast<int>(milliseconds));
}

void AppController::refreshAlfaAutoUpdateSchedule() {
    private_->alfa_auto_update_timer.stop();
    private_->next_alfa_auto_update_at = {};
    if (!alfaAutoUpdate() || !alfa_connected_) {
        return;
    }
    if (const auto lastSync = settingsDateTime(QStringLiteral("alfa/lastSyncAt"))) {
        scheduleAlfaAutoUpdateAfter(*lastSync);
    }
}

void AppController::scheduleAlfaAutoUpdateAfter(const QDateTime& from) {
    if (!alfaAutoUpdate() || !alfa_connected_) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    auto next = from.toUTC().addSecs(zenAutoUpdateIntervalSeconds);
    auto secondsUntilNext = now.secsTo(next);
    if (secondsUntilNext < 10) {
        secondsUntilNext = 10;
        next = now.addSecs(secondsUntilNext);
    }
    private_->next_alfa_auto_update_at = next;
    const auto milliseconds = std::min<qint64>(
        secondsUntilNext * 1000LL, std::numeric_limits<int>::max());
    private_->alfa_auto_update_timer.start(static_cast<int>(milliseconds));
}

void AppController::refreshVtbAutoUpdateSchedule() {
    private_->vtb_auto_update_timer.stop();
    private_->next_vtb_auto_update_at = {};
    if (!vtbAutoUpdate() || !vtb_connected_) {
        return;
    }
    if (const auto lastSync = settingsDateTime(QStringLiteral("vtb/lastSyncAt"))) {
        scheduleVtbAutoUpdateAfter(*lastSync);
    }
}

void AppController::scheduleVtbAutoUpdateAfter(const QDateTime& from) {
    if (!vtbAutoUpdate() || !vtb_connected_) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    auto next = from.toUTC().addSecs(zenAutoUpdateIntervalSeconds);
    auto secondsUntilNext = now.secsTo(next);
    if (secondsUntilNext < 10) {
        secondsUntilNext = 10;
        next = now.addSecs(secondsUntilNext);
    }
    private_->next_vtb_auto_update_at = next;
    const auto milliseconds = std::min<qint64>(
        secondsUntilNext * 1000LL, std::numeric_limits<int>::max());
    private_->vtb_auto_update_timer.start(static_cast<int>(milliseconds));
}

void AppController::loadDemoData() {
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
         QStringLiteral("684 300 ₽"), QStringLiteral("29,5%"), QStringLiteral("+18,7%"), true},
        {QStringLiteral("SU26238"), QStringLiteral("ОФЗ 26238"),
         QStringLiteral("532 600 ₽"), QStringLiteral("23,0%"), QStringLiteral("+7,2%"), true},
        {QStringLiteral("SBER"), QStringLiteral("Сбербанк"),
         QStringLiteral("408 200 ₽"), QStringLiteral("17,6%"), QStringLiteral("+24,1%"), true},
        {QStringLiteral("CNY"), QStringLiteral("Китайский юань"),
         QStringLiteral("321 500 ₽"), QStringLiteral("13,9%"), QStringLiteral("−2,4%"), false},
        {QStringLiteral("LQDT"), QStringLiteral("Ликвидность"),
         QStringLiteral("372 300 ₽"), QStringLiteral("16,0%"), QStringLiteral("+13,8%"), true},
    });

    net_worth_series_ = {3260000, 3380000, 3510000, 3470000, 3720000, 3890000,
                         4050000, 4210000, 4380000, 4520000, 4690000, 4826450};
    import_status_ = QStringLiteral("Выберите выписку для локального предварительного просмотра");
    sync_status_ = QStringLiteral("Т-Инвестиции: демонстрационные данные");
    emit dashboardChanged();
    emit importStatusChanged();
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
        import_status_ = QStringLiteral("Нет импортированных выписок");
        sync_status_ =
            QStringLiteral("Синхронизированные банковские данные восстановлены");
        emit importStatusChanged();
        emit syncStatusChanged();
        return;
    }
    demo_mode_ = false;
    accounts_.replace({});
    transactions_.replace({});
    categories_.replace({});
    holdings_.replace({});
    net_worth_series_.clear();
    live_net_worth_ = QStringLiteral("0 ₽");
    live_report_income_ = QStringLiteral("0 ₽");
    live_report_spending_ = QStringLiteral("0 ₽");
    live_savings_ = QStringLiteral("0 ₽");
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    emit dashboardChanged();
    emit importStatusChanged();
    emit syncStatusChanged();
    emit demoModeChanged();
}

void AppController::lock() {
    if (tbank_login_active_) {
        cancelTBankLogin();
    }
    if (alfa_login_active_ || private_->alfa_sync_running) {
        private_->alfa_login->cancel();
        alfa_login_active_ = false;
        private_->alfa_sync_running = false;
        alfa_login_status_ = QStringLiteral("Вход или синхронизация остановлены экраном приватности");
        emit alfaConnectionChanged();
    }
    if (vtb_login_active_ || private_->vtb_sync_running) {
        private_->vtb_login->cancel();
        vtb_login_active_ = false;
        private_->vtb_sync_running = false;
        vtb_login_status_ =
            QStringLiteral("Вход или синхронизация остановлены экраном приватности");
        emit vtbConnectionChanged();
    }
    if (zen_plugin_running_) {
        cancelZenPlugin();
    }
    if (private_->investment_reply) {
        disconnect(private_->investment_reply, nullptr, this, nullptr);
        private_->investment_reply->abort();
        private_->investment_reply->deleteLater();
        private_->investment_reply.clear();
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
        private_->tbank_sync_running = false;
        emit tbankConnectionChanged();
    }
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

void AppController::previewImport(const QUrl& source) {
    const QFileInfo file(source.toLocalFile());
    if (!file.exists() || !file.isFile()) {
        import_status_ = QStringLiteral("Не удалось открыть выбранный файл");
    } else {
        using namespace wealthtracker::connectors;
        const ImportSource import_source{
            .filePath = file.absoluteFilePath(),
            .displayName = file.fileName(),
            .content = {},
        };
        const auto connector =
            selectBestImportConnector(makeBuiltInImportConnectors(), import_source);
        if (!connector) {
            import_status_ =
                QStringLiteral("%1: формат выписки не распознан").arg(file.fileName());
        } else {
            ImportOptions options;
            options.previewRowLimit = 100;
            const auto preview = connector->preview(import_source, options);
            if (!preview) {
                import_status_ =
                    QStringLiteral("%1: %2")
                        .arg(file.fileName(), preview.error().message);
            } else {
                import_status_ =
                    QStringLiteral("%1 · %2 · операций: %3 · предупреждений: %4")
                        .arg(file.fileName(),
                             connector->manifest().displayName)
                        .arg(preview->sample.transactions.size())
                        .arg(preview->sample.warnings.size());
                if (preview->requiresConfirmation) {
                    import_status_ += QStringLiteral(" · требуется проверка");
                }
            }
        }
    }
    emit importStatusChanged();
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
    if (connectorId == QStringLiteral("t-invest")
        && private_->investment_reply) {
        disconnect(private_->investment_reply, nullptr, this, nullptr);
        private_->investment_reply->abort();
        private_->investment_reply->deleteLater();
        private_->investment_reply.clear();
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
    using namespace wealthtracker::connectors;
    if (private_->investment_reply) {
        sync_status_ = QStringLiteral("Т-Инвестиции уже проверяются");
        emit syncStatusChanged();
        return;
    }
    if (!private_->credentials || !investment_connected_) {
        sync_status_ = QStringLiteral("Сначала добавьте read-only токен");
        emit syncStatusChanged();
        return;
    }

    auto secret =
        private_->credentials->get(QStringLiteral("t-invest-readonly"));
    if (!secret || !*secret) {
        sync_status_ =
            secret ? QStringLiteral("Токен Т-Инвест не найден")
                   : secret.error();
        emit syncStatusChanged();
        return;
    }
    QByteArray token = std::move(**secret);
    auto request = private_->investment.discoverAccounts(token);
    token.fill('\0');
    if (!request) {
        sync_status_ = request.error().message;
        emit syncStatusChanged();
        return;
    }

    sync_status_ = QStringLiteral("Получаем инвестиционные счета…");
    emit syncStatusChanged();
    QPointer<QNetworkReply> reply = *request;
    private_->investment_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (!reply) {
            private_->investment_reply.clear();
            return;
        }
        private_->investment_reply.clear();
        if (reply->error() != QNetworkReply::NoError) {
            sync_status_ =
                QStringLiteral("Ошибка Т-Инвест: %1").arg(reply->errorString());
            emit syncStatusChanged();
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        const auto accounts =
            wealthtracker::connectors::TInvestClient::parseAccountsResponse(body);
        if (!accounts || accounts->accounts.isEmpty()) {
            sync_status_ =
                accounts ? QStringLiteral("Инвестиционные счета не найдены")
                         : accounts.error().message;
            emit syncStatusChanged();
            reply->deleteLater();
            return;
        }
        sync_status_ =
            QStringLiteral("Счета доступны: %1. Данные получены только для чтения.")
                .arg(accounts->accounts.size());
        emit syncStatusChanged();
        reply->deleteLater();
    });
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
            configuration->institution,
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
            configuration->institution,
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
            configuration->institution,
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
            private_->bank_batches.insert(connectorId, std::move(batch));
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
            sync_status_ =
                QStringLiteral("%1: %2").arg(institution, error.message);
            emit syncStatusChanged();
        },
        this);
    private_->bank_sync_coordinators.insert(connectorId, coordinator);
    coordinator->start(*discovery);
}

void AppController::rebuildBankModels() {
    struct DatedTransaction {
        TransactionRow row;
        QDateTime sortTime;
        QString sortKey;
    };
    struct SourceTransaction {
        QString connectorId;
        QString institution;
        const wealthtracker::connectors::NormalizedTransaction* transaction{};
    };

    QVector<AccountRow> accountRows;
    QVector<OwnedTransaction> ownedTransactions;
    QVector<SourceTransaction> sourceTransactions;
    QVector<DatedTransaction> transactionRows;
    QHash<QString, std::int64_t> categorySpending;
    std::int64_t rubNetWorth = 0;
    std::int64_t reportIncome = 0;
    std::int64_t reportSpending = 0;
    bool netWorthValid = true;
    bool incomeValid = true;
    bool spendingValid = true;
    bool categoriesValid = true;
    const auto today = QDate::currentDate();
    const ReportingPeriod reportingPeriod{reporting_from_, reporting_to_};

    for (auto bank = private_->bank_batches.cbegin(); bank != private_->bank_batches.cend();
         ++bank) {
        const auto configuration = bankConfiguration(bank.key());
        auto institution =
            configuration ? configuration->institution : retailBankInstitution(bank.key());
        if (institution.isEmpty()) {
            institution = private_->bank_institutions.value(bank.key());
        }
        if (institution.isEmpty()) {
            continue;
        }
        QHash<QString, Money> balances;
        for (const auto& snapshot : bank->balances) {
            balances.insert(snapshot.accountExternalId, snapshot.balance);
            if (snapshot.balance.currency == QStringLiteral("RUB")) {
                netWorthValid &= checkedAdd(rubNetWorth, snapshot.balance.minorUnits);
            }
        }
        for (const auto& account : bank->accounts) {
            const auto balance = balances.value(account.externalId, Money{0, account.currency});
            accountRows.push_back({
                account.name,
                institution,
                account.kind,
                formatMinorUnits(balance.minorUnits),
                currencySymbol(balance.currency),
                connectorAccent(bank.key()),
            });
        }
        for (const auto& transaction : bank->transactions) {
            sourceTransactions.push_back({bank.key(), institution, &transaction});
            ownedTransactions.push_back(
                {bank.key(), transaction.accountExternalId, transaction.externalId,
                 transaction.postingDate, transaction.occurredAt, transaction.amount.minorUnits,
                 transaction.amount.currency,
                 transaction.kind ==
                     wealthtracker::connectors::NormalizedTransactionKind::InternalTransfer});
        }
    }

    const auto transferIndexes = findOwnAccountTransfers(std::span<const OwnedTransaction>{
        ownedTransactions.constData(), static_cast<std::size_t>(ownedTransactions.size())});
    for (qsizetype index = 0; index < sourceTransactions.size(); ++index) {
        const auto& source = sourceTransactions.at(index);
        const auto& transaction = *source.transaction;
        if (!reportingPeriod.contains(transaction.postingDate)) {
            continue;
        }
        auto title = transaction.merchant.trimmed();
        if (title.isEmpty()) {
            title = transaction.description.trimmed();
        }
        if (title.isEmpty()) {
            title = QStringLiteral("Операция");
        }
        auto subtitle = transaction.description.trimmed();
        if (subtitle.isEmpty() || subtitle == title) {
            subtitle = source.institution;
        }
        const bool internalTransfer = transferIndexes.contains(index);
        const auto category = internalTransfer ? QStringLiteral("Внутренний перевод")
                              : transaction.bankCategory.trimmed().isEmpty()
                                  ? QStringLiteral("Без категории")
                                  : transaction.bankCategory.trimmed();
        const auto sortTime = transaction.occurredAt.value_or(
            QDateTime(transaction.postingDate, QTime(0, 0), QTimeZone::UTC));
        const auto dateText = formatTransactionDate(transaction.postingDate, today);
        const auto sortKey = QStringLiteral("%1\n%2\n%3")
                                 .arg(source.connectorId,
                                      transaction.accountExternalId,
                                      transaction.externalId);
        transactionRows.push_back({
            {title, subtitle, category, signedMoney(transaction.amount), dateText,
             transaction.amount.minorUnits >= 0, internalTransfer},
            sortTime,
            sortKey,
        });

        if (transaction.amount.currency != QStringLiteral("RUB") || internalTransfer) {
            continue;
        }
        if (transaction.amount.minorUnits >= 0) {
            incomeValid &= checkedAdd(reportIncome, transaction.amount.minorUnits);
            continue;
        }
        if (transaction.amount.minorUnits == std::numeric_limits<std::int64_t>::min()) {
            spendingValid = false;
            categoriesValid = false;
            continue;
        }
        const auto spending = -transaction.amount.minorUnits;
        spendingValid &= checkedAdd(reportSpending, spending);
        auto categoryTotal = categorySpending.value(category);
        if (checkedAdd(categoryTotal, spending)) {
            categorySpending.insert(category, categoryTotal);
        } else {
            categoriesValid = false;
        }
    }

    std::sort(transactionRows.begin(), transactionRows.end(),
              [](const DatedTransaction& left, const DatedTransaction& right) {
                  if (left.sortTime != right.sortTime) {
                      return left.sortTime > right.sortTime;
                  }
                  return left.sortKey < right.sortKey;
              });
    QVector<TransactionRow> sortedTransactions;
    sortedTransactions.reserve(transactionRows.size());
    for (auto& transaction : transactionRows) {
        sortedTransactions.push_back(std::move(transaction.row));
    }

    QVector<std::pair<QString, std::int64_t>> sortedCategories;
    sortedCategories.reserve(categorySpending.size());
    for (auto category = categorySpending.cbegin(); category != categorySpending.cend();
         ++category) {
        sortedCategories.emplace_back(category.key(), category.value());
    }
    std::sort(sortedCategories.begin(), sortedCategories.end(),
              [](const auto& left, const auto& right) {
                  if (left.second != right.second) {
                      return left.second > right.second;
                  }
                  return left.first < right.first;
              });
    static const QStringList categoryColors{QStringLiteral("#2563EB"), QStringLiteral("#7C3AED"),
                                            QStringLiteral("#059669"), QStringLiteral("#D97706"),
                                            QStringLiteral("#64748B")};
    QVector<CategoryRow> categoryRows;
    if (spendingValid && categoriesValid) {
        categoryRows.reserve(sortedCategories.size());
        for (qsizetype index = 0; index < sortedCategories.size(); ++index) {
            const auto& [name, amount] = sortedCategories.at(index);
            const auto share = reportSpending > 0
                                   ? static_cast<double>(amount) /
                                         static_cast<double>(reportSpending)
                                   : 0.0;
            categoryRows.push_back(
                {name, QStringLiteral("%1 ₽").arg(formatMinorUnits(amount)), share,
                 categoryColors.at(index % categoryColors.size())});
        }
    }

    std::sort(accountRows.begin(), accountRows.end(),
              [](const AccountRow& left, const AccountRow& right) {
                  return std::tie(left.institution, left.name, left.type) <
                         std::tie(right.institution, right.name, right.type);
              });

    auto savings = reportIncome;
    const bool savingsValid = incomeValid && spendingValid &&
                              checkedAdd(savings, -reportSpending);

    accounts_.replace(std::move(accountRows));
    transactions_.replace(std::move(sortedTransactions));
    categories_.replace(std::move(categoryRows));
    holdings_.replace({});
    net_worth_series_.clear();
    live_net_worth_ = netWorthValid
                          ? QStringLiteral("%1 ₽").arg(formatMinorUnits(rubNetWorth))
                          : QStringLiteral("—");
    live_report_income_ = incomeValid
                              ? QStringLiteral("%1 ₽").arg(formatMinorUnits(reportIncome))
                              : QStringLiteral("—");
    live_report_spending_ = spendingValid
                                ? QStringLiteral("%1 ₽").arg(formatMinorUnits(reportSpending))
                                : QStringLiteral("—");
    live_savings_ = savingsValid
                        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(savings))
                        : QStringLiteral("—");
    const bool demoChanged = demo_mode_;
    demo_mode_ = false;
    emit dashboardChanged();
    if (demoChanged) {
        emit demoModeChanged();
    }
}

bool AppController::inspectZenPluginBundle(const QUrl& source) {
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
        private_->zen_descriptor ? zenStateCredentialKey(*private_->zen_descriptor)
                                 : QString{};
    const auto newStateKey = zenStateCredentialKey(*descriptor);
    if (!previousStateKey.isEmpty() && previousStateKey != newStateKey) {
        QSettings settings;
        settings.remove(QStringLiteral("zenPlugin/preferences"));
        settings.remove(QStringLiteral("zenPlugin/lastSyncAt"));
        settings.setValue(QStringLiteral("zenPlugin/autoUpdate"), false);
    }

    private_->zen_descriptor = std::move(*descriptor);
    zen_plugin_id_ = private_->zen_descriptor->pluginId;
    zen_plugin_bundle_path_ =
        private_->zen_descriptor->bundlePath;
    zen_plugin_institution_ =
        zen_plugin_id_ == QStringLiteral("altabanka-rs")
        ? QStringLiteral("Alta Banka (Сербия)")
        : zen_plugin_id_;
    zen_plugin_fields_ =
        zenPluginFieldVariants(*private_->zen_descriptor);
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
    if (zen_plugin_running_ || !private_->zen_descriptor) {
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

    QJsonObject normalizedPreferences;
    QDateTime from =
        QDateTime(QDate::currentDate().addYears(-1), QTime(0, 0),
                  QTimeZone::UTC);
    for (const auto& field :
         private_->zen_descriptor->preferences) {
        auto value =
            preferences.value(field.key, field.defaultValue);
        if (field.kind
            == wealthtracker::connectors::
                ZenPluginPreferenceKind::Checkbox) {
            normalizedPreferences.insert(field.key, value.toBool());
        } else {
            const auto text = value.toString();
            if (field.required && text.trimmed().isEmpty()) {
                sync_status_ =
                    QStringLiteral("Заполните поле «%1»")
                        .arg(field.label);
                emit syncStatusChanged();
                return false;
            }
            normalizedPreferences.insert(field.key, text);
            if (field.key == QStringLiteral("startDate")) {
                auto parsed =
                    QDateTime::fromString(text, Qt::ISODateWithMs);
                if (!parsed.isValid()) {
                    parsed =
                        QDateTime::fromString(text, Qt::ISODate);
                }
                if (parsed.isValid()) {
                    from = parsed.toUTC();
                }
            }
        }
    }

    QJsonObject state;
    const auto stateKey =
        zenStateCredentialKey(*private_->zen_descriptor);
    auto savedState = private_->credentials->get(stateKey);
    if (!savedState) {
        sync_status_ = savedState.error();
        emit syncStatusChanged();
        return false;
    }
    if (*savedState) {
        QByteArray encoded = std::move(**savedState);
        QJsonParseError parseError;
        const auto document =
            QJsonDocument::fromJson(encoded, &parseError);
        encoded.fill('\0');
        if (parseError.error == QJsonParseError::NoError
            && document.isObject()) {
            state = document.object();
        } else {
            const auto removed =
                private_->credentials->remove(stateKey);
            if (!removed) {
                sync_status_ = removed.error();
                emit syncStatusChanged();
                return false;
            }
        }
    }

    const wealthtracker::connectors::ZenPluginRunRequest request{
        .descriptor = *private_->zen_descriptor,
        .preferences = std::move(normalizedPreferences),
        .state = std::move(state),
        .from = from,
        .to = QDateTime::currentDateTimeUtc(),
        .allowedOrigins = {},
    };
    const auto safePreferences =
        persistableZenPreferences(request.descriptor, request.preferences);
    auto startMessage =
        wealthtracker::connectors::makeZenPluginStartMessage(
            request);
    if (!startMessage) {
        sync_status_ = startMessage.error().message;
        emit syncStatusChanged();
        return false;
    }

    const auto pluginId = private_->zen_descriptor->pluginId;
    const auto batchKey =
        QStringLiteral("zen:") + pluginId;
    zen_plugin_institution_ = displayName;
    private_->zen_prompt_id.clear();
    private_->zen_runner = std::make_unique<ZenPluginRunner>(
        pluginId,
        [this](wealthtracker::connectors::ZenPluginPrompt prompt) {
            private_->zen_prompt_id = prompt.requestId;
            zen_plugin_prompt_ = prompt.message;
            zen_plugin_prompt_secret_ = true;
            sync_status_ =
                QStringLiteral("Плагин ждёт одноразовый ответ");
            emit zenPluginChanged();
            emit syncStatusChanged();
        },
        [this, batchKey, stateKey, safePreferences, pluginId,
         displayName](NormalizedBatch batch, QJsonObject newState) {
            auto encoded =
                QJsonDocument(newState).toJson(
                    QJsonDocument::Compact);
            const auto stored =
                private_->credentials->put(stateKey, encoded);
            encoded.fill('\0');
            bool oldStateRemoved = true;
            if (stored
                && !private_->configured_zen_state_key.isEmpty()
                && private_->configured_zen_state_key != stateKey) {
                oldStateRemoved =
                    private_->credentials
                        ->remove(
                            private_->configured_zen_state_key)
                        .has_value();
            }
            if (stored) {
                private_->configured_zen_state_key = stateKey;
            }

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
            rebuildBankModels();

            QSettings settings;
            settings.setValue(
                QStringLiteral("zenPlugin/bundlePath"),
                private_->zen_descriptor->bundlePath);
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
                zenPluginFieldVariants(*private_->zen_descriptor);
            zen_plugin_running_ = false;
            zen_plugin_prompt_.clear();
            private_->zen_prompt_id.clear();
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
        },
        [this, displayName](const QString& error) {
            zen_plugin_running_ = false;
            zen_plugin_prompt_.clear();
            private_->zen_prompt_id.clear();
            sync_status_ =
                QStringLiteral("%1: %2").arg(displayName, error);
            scheduleZenPluginAutoUpdateAfter(
                QDateTime::currentDateTimeUtc());
            refreshBankConnections();
            emit zenPluginChanged();
            emit syncStatusChanged();
        },
        this);

    zen_plugin_running_ = true;
    zen_plugin_prompt_.clear();
    sync_status_ =
        QStringLiteral("%1: запускаем локальный плагин…")
            .arg(displayName);
    if (!private_->zen_runner->start(node, *startMessage)) {
        startMessage->fill('\0');
        private_->zen_runner.reset();
        zen_plugin_running_ = false;
        sync_status_ =
            QStringLiteral("Не удалось подготовить локальный host плагина");
        refreshBankConnections();
        emit zenPluginChanged();
        emit syncStatusChanged();
        return false;
    }
    startMessage->fill('\0');
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
    return true;
}

bool AppController::synchronizeZenPlugin() {
    if (!private_->zen_descriptor) {
        sync_status_ = QStringLiteral("Сначала выберите собранный bundle");
        emit syncStatusChanged();
        return false;
    }
    auto preferences = savedZenPluginPreferences().toVariantMap();
    const auto displayName =
        zen_plugin_institution_.trimmed().isEmpty()
            ? private_->zen_descriptor->pluginId
            : zen_plugin_institution_;
    return startZenPlugin(displayName, preferences);
}

void AppController::setZenPluginAutoUpdate(const bool enabled) {
    if (enabled && !private_->zen_descriptor) {
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
                      .arg(zenAutoUpdateIntervalHours)
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
    if (!private_->zen_runner
        || private_->zen_prompt_id.isEmpty()) {
        return false;
    }
    auto encoded = value.toUtf8();
    auto message =
        wealthtracker::connectors::makeZenPluginInputMessage(
            private_->zen_prompt_id, encoded);
    encoded.fill('\0');
    if (!message) {
        sync_status_ = message.error().message;
        emit syncStatusChanged();
        return false;
    }
    const auto accepted = private_->zen_runner->answer(*message);
    message->fill('\0');
    if (!accepted) {
        return false;
    }
    private_->zen_prompt_id.clear();
    zen_plugin_prompt_.clear();
    sync_status_ =
        QStringLiteral("Ответ передан локальному плагину");
    emit zenPluginChanged();
    emit syncStatusChanged();
    return true;
}

void AppController::cancelZenPlugin() {
    if (private_->zen_runner) {
        private_->zen_runner->cancel();
    }
    zen_plugin_running_ = false;
    zen_plugin_prompt_.clear();
    private_->zen_prompt_id.clear();
    sync_status_ = QStringLiteral("Синхронизация плагина отменена");
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
}

void AppController::removeZenPlugin() {
    cancelZenPlugin();
    if (private_->credentials) {
        QSet<QString> stateKeys;
        if (private_->zen_descriptor) {
            stateKeys.insert(
                zenStateCredentialKey(*private_->zen_descriptor));
        }
        if (!private_->configured_zen_state_key.isEmpty()) {
            stateKeys.insert(private_->configured_zen_state_key);
        }
        for (const auto& stateKey : stateKeys) {
            const auto removed =
                private_->credentials->remove(stateKey);
            if (!removed) {
                sync_status_ = removed.error();
                emit syncStatusChanged();
                return;
            }
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
    private_->zen_descriptor.reset();
    private_->configured_zen_state_key.clear();
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
    if (zen_plugin_running_ || private_->tbank_sync_running || alfa_login_active_ ||
        private_->alfa_sync_running || vtb_login_active_ || private_->vtb_sync_running) {
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
    if (tbank_login_active_) {
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

    tbank_login_active_ = true;
    private_->tbank_login->start(tbankLoginUrl());
    refreshBankConnections();
    emit tbankConnectionChanged();
}

void AppController::reloadTBankLoginPage() {
    if (tbank_login_active_) {
        private_->tbank_login->reload();
    }
}

void AppController::failTBankLogin(QString message) {
    tbank_login_active_ = false;
    tbank_login_status_ = std::move(message);
    sync_status_ = tbank_login_status_;
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();
}

void AppController::cancelTBankLogin() {
    if (!tbank_login_active_) {
        return;
    }
    private_->tbank_login->cancel();
    tbank_login_active_ = false;
    tbank_login_status_ = QStringLiteral("Вход отменён");
    if (!tbank_connected_ && private_->tbank_session_pending_validation
        && private_->credentials) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
        private_->tbank_session_pending_validation = false;
    }
    refreshBankConnections();
    emit tbankConnectionChanged();
}

void AppController::acceptTBankSession(QByteArray session) {
    const auto view = std::string_view{
        session.constData(), static_cast<std::size_t>(session.size())};
    if (!tbank_login_active_) {
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
    tbank_connected_ = false;
    tbank_login_active_ = false;
    private_->tbank_session_pending_validation = true;
    tbank_login_status_ = QStringLiteral(
        "Сессия получена. Проверяем доступ и загружаем операции…");
    refreshBankConnections();
    emit tbankConnectionChanged();

    if (!synchronizeTBank()) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
        private_->tbank_session_pending_validation = false;
        failTBankLogin(sync_status_);
    }
}

bool AppController::synchronizeTBank() {
    using wealthtracker::connectors::ConnectorError;
    using wealthtracker::connectors::ErrorCode;
    using wealthtracker::connectors::TBankRetailClient;

    if (private_->tbank_sync_running || tbank_login_active_) {
        sync_status_ = QStringLiteral("Т-Банк уже выполняет вход или синхронизацию");
        emit syncStatusChanged();
        return false;
    }
    if ((!tbank_connected_ && !private_->tbank_session_pending_validation)
        || !private_->credentials) {
        sync_status_ = QStringLiteral("Сначала войдите в Т-Банк");
        emit syncStatusChanged();
        return false;
    }
    if (zen_plugin_running_ || alfa_login_active_ || private_->alfa_sync_running ||
        vtb_login_active_ || private_->vtb_sync_running) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        emit syncStatusChanged();
        return false;
    }

    auto credential = private_->credentials->get(
        QString::fromLatin1(tbankRetailCredentialKey));
    if (!credential || !*credential) {
        sync_status_ = credential ? QStringLiteral("Сессия Т-Банка не найдена")
                                  : credential.error();
        tbank_connected_ = false;
        private_->tbank_session_pending_validation = false;
        refreshBankConnections();
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    auto session = std::move(**credential);
    auto discovery = private_->tbank_retail.discoverAccounts(session);
    session.fill('\0');
    if (!discovery) {
        sync_status_ = discovery.error().message;
        emit syncStatusChanged();
        return false;
    }

    private_->tbank_auto_update_timer.stop();
    private_->tbank_sync_running = true;
    tbank_login_status_ = QStringLiteral("Загружаем счета и операции…");
    sync_status_ = QStringLiteral("Т-Банк: получаем счета…");
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();

    BankRuntime runtime{
        &private_->tbank_retail,
        QStringLiteral("Т-Банк"),
        1'000,
        [](const QByteArray& body, const QDateTime& observedAt) {
            return TBankRetailClient::parseAccountsResponse(body, observedAt);
        },
        [](const QByteArray& body, const QString& accountId,
           const QByteArray& cursor, int) {
            return TBankRetailClient::parseOperationsResponse(body, accountId, cursor);
        },
    };
    runtime.maximumConcurrentRequests = 1;
    runtime.historyLookbackYears = 2;
    auto* coordinator = new BankSyncCoordinator(
        std::move(runtime),
        [this]() -> std::expected<QByteArray, QString> {
            auto loaded = private_->credentials->get(
                QString::fromLatin1(tbankRetailCredentialKey));
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            if (!*loaded) {
                return std::unexpected(QStringLiteral("Сессия Т-Банка удалена"));
            }
            return std::move(**loaded);
        },
        [this](NormalizedBatch batch) {
            private_->tbank_sync_coordinator.clear();
            private_->tbank_sync_running = false;
            private_->tbank_session_pending_validation = false;
            tbank_connected_ = true;
            const auto accountCount = batch.accounts.size();
            const auto transactionCount = batch.transactions.size();
            const auto warningCount = batch.warnings.size();
            private_->bank_batches.insert(QStringLiteral("tbank-retail"),
                                          std::move(batch));
            rebuildBankModels();
            const auto completedAt = QDateTime::currentDateTimeUtc();
            QSettings{}.setValue(
                QStringLiteral("tbank/lastSyncAt"),
                completedAt.toString(Qt::ISODateWithMs));
            tbank_login_status_ = QStringLiteral(
                "Т-Банк подключён. История запрошена за 2 года; сессия хранится "
                "в системном хранилище.");
            sync_status_ = QStringLiteral(
                "Т-Банк: счетов %1 · операций %2 · период запроса 2 года")
                .arg(accountCount)
                .arg(transactionCount);
            if (warningCount > 0) {
                sync_status_ += QStringLiteral(" · пропущено записей %1").arg(warningCount);
            }
            scheduleTBankAutoUpdateAfter(completedAt);
            refreshBankConnections();
            emit dashboardChanged();
            emit syncStatusChanged();
            emit tbankConnectionChanged();
        },
        [this](ConnectorError error) {
            private_->tbank_sync_coordinator.clear();
            private_->tbank_sync_running = false;
            const bool discardSession =
                private_->tbank_session_pending_validation
                || error.code == ErrorCode::AuthenticationFailed;
            private_->tbank_session_pending_validation = false;
            if (discardSession && private_->credentials) {
                (void)private_->credentials->remove(
                    QString::fromLatin1(tbankRetailCredentialKey));
                tbank_connected_ = false;
                private_->bank_batches.remove(QStringLiteral("tbank-retail"));
                if (!demo_mode_) {
                    rebuildBankModels();
                }
            }
            tbank_login_status_ = error.message;
            sync_status_ = QStringLiteral("Т-Банк: %1").arg(error.message);
            if (tbank_connected_) {
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
    if (private_->tbank_sync_coordinator) {
        private_->tbank_sync_coordinator->cancel();
        private_->tbank_sync_coordinator.clear();
    }
    private_->tbank_sync_running = false;
    private_->tbank_session_pending_validation = false;
    tbank_connected_ = false;
    tbank_login_active_ = false;
    private_->tbank_login->cancel();
    private_->tbank_auto_update_timer.stop();
    private_->next_tbank_auto_update_at = {};
    if (private_->credentials) {
        (void)private_->credentials->remove(
            QString::fromLatin1(tbankRetailCredentialKey));
    }
    QSettings{}.remove(QStringLiteral("tbank"));
    private_->bank_batches.remove(QStringLiteral("tbank-retail"));
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
    if (zen_plugin_running_ || private_->tbank_sync_running || tbank_login_active_ ||
        private_->alfa_sync_running || vtb_login_active_ || private_->vtb_sync_running) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginAlfaLogin() {
    if (!prepareAlfaConnection()) {
        return;
    }
    alfa_login_active_ = true;
    private_->alfa_login->startLogin(alfaLoginUrl());
    refreshBankConnections();
    emit alfaConnectionChanged();
}

void AppController::reloadAlfaLoginPage() {
    if (alfa_login_active_) {
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
    if (!alfa_login_active_) {
        return;
    }
    private_->alfa_login->cancel();
    alfa_login_active_ = false;
    alfa_login_status_ = QStringLiteral("Вход отменён");
    refreshBankConnections();
    emit alfaConnectionChanged();
}

bool AppController::synchronizeAlfa() {
    if (private_->alfa_sync_running || alfa_login_active_) {
        sync_status_ = QStringLiteral("Альфа-Банк уже выполняет вход или синхронизацию");
        emit syncStatusChanged();
        return false;
    }
    if (!alfa_connected_) {
        sync_status_ = QStringLiteral("Сначала войдите в Альфа-Банк");
        emit syncStatusChanged();
        return false;
    }
    if (zen_plugin_running_ || private_->tbank_sync_running || tbank_login_active_ ||
        vtb_login_active_ || private_->vtb_sync_running) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        emit syncStatusChanged();
        return false;
    }

    private_->alfa_auto_update_timer.stop();
    private_->alfa_sync_running = true;
    alfa_login_status_ = QStringLiteral("Загружаем счета и операции…");
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
    private_->alfa_login->disconnectBank();
    private_->alfa_sync_running = false;
    alfa_connected_ = false;
    alfa_login_active_ = false;
    private_->alfa_auto_update_timer.stop();
    private_->next_alfa_auto_update_at = {};
    QSettings{}.remove(QStringLiteral("alfa"));
    private_->bank_batches.remove(QStringLiteral("alfa-retail"));
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
    if (zen_plugin_running_ || private_->tbank_sync_running || tbank_login_active_ ||
        private_->alfa_sync_running || alfa_login_active_ ||
        private_->vtb_sync_running || vtb_login_active_) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginVtbLogin() {
    if (!prepareVtbConnection()) {
        return;
    }
    vtb_login_active_ = true;
    private_->vtb_login->startLogin(vtbLoginUrl());
    refreshBankConnections();
    emit vtbConnectionChanged();
}

void AppController::reloadVtbLoginPage() {
    if (vtb_login_active_) {
        private_->vtb_login->reload();
    }
}

void AppController::cancelVtbLogin() {
    if (!vtb_login_active_) {
        return;
    }
    private_->vtb_login->cancel();
    vtb_login_active_ = false;
    vtb_login_status_ = QStringLiteral("Вход отменён");
    refreshBankConnections();
    emit vtbConnectionChanged();
}

bool AppController::synchronizeVtb() {
    if (private_->vtb_sync_running || vtb_login_active_) {
        sync_status_ = QStringLiteral("ВТБ уже выполняет вход или синхронизацию");
        emit syncStatusChanged();
        return false;
    }
    if (!vtb_connected_) {
        sync_status_ = QStringLiteral("Сначала войдите в ВТБ");
        emit syncStatusChanged();
        return false;
    }
    if (zen_plugin_running_ || private_->tbank_sync_running || tbank_login_active_ ||
        private_->alfa_sync_running || alfa_login_active_) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        emit syncStatusChanged();
        return false;
    }

    private_->vtb_auto_update_timer.stop();
    private_->vtb_sync_running = true;
    vtb_login_status_ = QStringLiteral("Загружаем счета и операции…");
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
    private_->vtb_login->disconnectBank();
    private_->vtb_sync_running = false;
    vtb_connected_ = false;
    vtb_login_active_ = false;
    private_->vtb_auto_update_timer.stop();
    private_->next_vtb_auto_update_at = {};
    QSettings{}.remove(QStringLiteral("vtb"));
    private_->bank_batches.remove(QStringLiteral("vtb-retail"));
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
            "CLIENT. Cookies удаляются после входа или отмены. Встроенный "
            "C++-коннектор выполняет только запросы счетов и операций.");
    } else if (connectorId == QStringLiteral("alfa-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход Альфа-Банка внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает только страница банка. Альфа-Онлайн использует "
            "браузерную сессию: приложение не читает её cookies, заголовки или "
            "хранилище и получает из страницы только ограниченный снимок счетов, "
            "остатков и операций. Сессия остаётся в защищённом хранилище "
            "системного WebView для автообновления и удаляется при отключении.");
    } else if (connectorId == QStringLiteral("vtb-retail")) {
        connection_message_ = QStringLiteral(
            "WealthTracker показывает официальный вход ВТБ внутри "
            "приложения через системный WebView. Телефон, пароль и код "
            "обрабатывает только страница банка. Приложение не читает cookies, "
            "поля входа или браузерное хранилище. Оно вызывает через клиент "
            "авторизованной страницы только разрешённые GET-маршруты счетов, "
            "остатков и истории в пределах жёстких лимитов. Сессия остаётся в "
            "WebView и удаляется при отключении.");
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
