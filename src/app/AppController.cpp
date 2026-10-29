#include "app/AppController.hpp"

#include <QDate>
#include <QDir>
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
import wt.balance_history_view;
import wt.crypto_wallet;
import wt.storage;
import wt.investment_sync;
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

constexpr int autoUpdateIntervalHours = 6;
constexpr int autoUpdateIntervalSeconds =
    autoUpdateIntervalHours * 60 * 60;

QDateTime scheduleAutoUpdate(QTimer& timer, const QDateTime& from,
                            const int intervalSeconds = autoUpdateIntervalSeconds) {
    const auto now = QDateTime::currentDateTimeUtc();
    auto next = from.toUTC().addSecs(intervalSeconds);
    auto secondsUntilNext = now.secsTo(next);
    if (secondsUntilNext < 10) {
        secondsUntilNext = 10;
        next = now.addSecs(secondsUntilNext);
    }
    const auto milliseconds = std::min<qint64>(
        secondsUntilNext * 1000LL, std::numeric_limits<int>::max());
    timer.start(static_cast<int>(milliseconds));
    return next;
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
    std::optional<wealthtracker::storage::Database> history_database;
    std::vector<wealthtracker::storage::BalanceCapture> balance_history;
    QString history_error;
    std::vector<wealthtracker::storage::CryptoWallet> crypto_wallets;
    QHash<QString, QVariantMap> crypto_states;
    QPointer<CryptoWalletSync> crypto_sync;
    QStringList crypto_pending;
    QTimer crypto_timer;
    QString crypto_status;
    bool crypto_running = false;
    int crypto_failures = 0;
    QHash<QString, QPointer<BankSyncCoordinator>> bank_sync_coordinators;
    QPointer<InvestmentSyncCoordinator> investment_sync;
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
    QStringList pending_retail_restores;
};

void AppController::initializeBalanceHistory(const QString& path) {
    // Injected controllers opt in with an explicit path; tests never open the user's vault.
    if (path.isEmpty()) return;
    private_->history_error = QStringLiteral("История недоступна: не удалось открыть защищённое хранилище.");
    if (!private_->credentials || !private_->credentials->supported()) return;
    const auto keyName = QStringLiteral("balance-history-key");
    auto loaded = private_->credentials->get(keyName);
    if (!loaded) return;
    if (!*loaded) {
        if (QFileInfo::exists(path)) {
            private_->history_error = QStringLiteral("История недоступна: ключ сохранённой базы не найден.");
            return;
        }
        auto generated = wealthtracker::platform::VaultCrypto::generate_database_key();
        if (!generated) return;
        QByteArray key(reinterpret_cast<const char*>(generated->data()),
                       static_cast<qsizetype>(generated->size()));
        if (!private_->credentials->put(keyName, key)) {
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
    auto wallets = repository.wallets();
    if (!wallets) return;
    private_->crypto_wallets = std::move(*wallets);
    private_->balance_history = std::move(*captures);
    private_->history_database = std::move(*database);
    private_->history_error.clear();
}

void AppController::recordBalanceHistory(const QString& connectorId) {
    if (!private_->history_database) return;
    const auto batch = private_->bank_batches.constFind(connectorId);
    if (batch == private_->bank_batches.cend()) return;
    const auto configuration = bankConfiguration(connectorId);
    auto institution = configuration ? configuration->institution : retailBankInstitution(connectorId);
    if (institution.isEmpty()) institution = private_->bank_institutions.value(connectorId);
    std::vector<wealthtracker::storage::HistoryBalance> balances;
    // A missing balance is unknown, never zero. Do not save an incomplete observation.
    bool complete = batch->balances.size() == batch->accounts.size();
    for (const auto& account : batch->accounts) {
        const auto matches = [&](const auto& balance) {
            return balance.accountExternalId == account.externalId && balance.balance.currency == account.currency;
        };
        if (std::count_if(batch->balances.cbegin(), batch->balances.cend(), matches) != 1) {
            complete = false;
            break;
        }
        const auto found = std::find_if(batch->balances.cbegin(), batch->balances.cend(), matches);
        balances.push_back({account.externalId.toStdString(), account.name.toStdString(),
                            account.currency.toStdString(), found->balance.minorUnits});
    }
    if (!complete) {
        private_->history_error = QStringLiteral("История не обновлена: получены не все остатки по счетам.");
        emit balanceHistoryChanged();
        return;
    }
    const auto capturedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    wealthtracker::storage::BalanceHistoryRepository repository(*private_->history_database);
    auto saved = repository.append(connectorId.toStdString(), institution.toStdString(),
                                   capturedAt.toStdString(), balances);
    if (!saved) {
        private_->history_error = QStringLiteral("Не удалось сохранить историю остатков. Предыдущие записи сохранены.");
    } else {
        private_->balance_history.push_back({*saved, connectorId.toStdString(), institution.toStdString(),
                                            capturedAt.toStdString(), std::move(balances)});
        private_->history_error.clear();
    }
    emit balanceHistoryChanged();
}

QVariantList AppController::cryptoWallets() const {
    QVariantList rows;
    for (const auto& wallet : private_->crypto_wallets) {
        const auto address = QString::fromStdString(wallet.address);
        auto row = private_->crypto_states.value(address);
        row.insert(QStringLiteral("id"), address);
        row.insert(QStringLiteral("address"), address);
        row.insert(QStringLiteral("name"), QString::fromStdString(wallet.name));
        if (!row.contains(QStringLiteral("holdings"))) row.insert(QStringLiteral("holdings"), QVariantList{});
        if (!row.contains(QStringLiteral("value"))) {
            row.insert(QStringLiteral("value"), QStringLiteral("—"));
            row.insert(QStringLiteral("unpricedCount"), 0);
            for (const auto& capture : private_->balance_history) {
                if (capture.connection_id != "ethereum:" + wallet.address) continue;
                for (const auto& balance : capture.balances) {
                    if (balance.currency != "USDT") continue;
                    row.insert(QStringLiteral("value"), usdt_label(balance.amount_minor));
                    row.insert(QStringLiteral("lastSync"), QDateTime::fromString(QString::fromStdString(capture.captured_at), Qt::ISODateWithMs)
                        .toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
                    if (!row.contains(QStringLiteral("status"))) row.insert(QStringLiteral("status"),
                        QStringLiteral("Оценка из истории. Обновите для получения списка токенов."));
                }
            }
        }
        rows.push_back(row);
    }
    return rows;
}

QString AppController::cryptoStatus() const { return private_->crypto_status; }
bool AppController::cryptoSyncRunning() const { return private_->crypto_running; }

bool AppController::addCryptoWallet(const QString& name, const QString& value) {
    if (locked_ || demo_mode_ || private_->crypto_running) return false;
    const auto address = value.trimmed().toLower();
    const auto label = name.trimmed();
    if (!valid_ethereum_address(address) || label.isEmpty() || label.toUtf8().size() > 80 || label.contains(QChar::Null)) {
        private_->crypto_status = QStringLiteral("Укажите короткое название и публичный EVM-адрес: 0x и 40 шестнадцатеричных символов.");
        emit cryptoWalletsChanged();
        return false;
    }
    if (!private_->history_database) {
        private_->crypto_status = QStringLiteral("Защищённое хранилище недоступно. Кошелёк не сохранён.");
        emit cryptoWalletsChanged();
        return false;
    }
    storage::BalanceHistoryRepository repository(*private_->history_database);
    if (!repository.save_wallet(address.toStdString(), label.toStdString())) {
        private_->crypto_status = QStringLiteral("Не удалось сохранить кошелёк.");
        emit cryptoWalletsChanged();
        return false;
    }
    auto found = std::find_if(private_->crypto_wallets.begin(), private_->crypto_wallets.end(),
        [&](const auto& wallet) { return wallet.address == address.toStdString(); });
    if (found == private_->crypto_wallets.end()) private_->crypto_wallets.push_back({address.toStdString(), label.toStdString()});
    else found->name = label.toStdString();
    private_->crypto_status = QStringLiteral("Кошелёк сохранён. Обнаруживаем сети Rabby…");
    emit cryptoWalletsChanged();
    refreshCryptoWallets();
    return true;
}

bool AppController::removeCryptoWallet(const QString& address) {
    if (locked_ || demo_mode_ || private_->crypto_running || !private_->history_database) return false;
    storage::BalanceHistoryRepository repository(*private_->history_database);
    if (!repository.remove_wallet(address.toStdString())) {
        private_->crypto_status = QStringLiteral("Не удалось удалить подключение кошелька.");
        emit cryptoWalletsChanged();
        return false;
    }
    std::erase_if(private_->crypto_wallets, [&](const auto& wallet) { return wallet.address == address.toStdString(); });
    private_->crypto_states.remove(address);
    private_->crypto_status = QStringLiteral("Подключение кошелька удалено. История сохранена.");
    emit cryptoWalletsChanged();
    return true;
}

bool AppController::refreshCryptoWallets() {
    if (locked_ || demo_mode_ || private_->crypto_running) return false;
    if (private_->crypto_wallets.empty()) {
        private_->crypto_status = QStringLiteral("Добавьте публичный EVM-адрес кошелька.");
        emit cryptoWalletsChanged();
        return false;
    }
    private_->crypto_pending.clear();
    for (const auto& wallet : private_->crypto_wallets) private_->crypto_pending.push_back(QString::fromStdString(wallet.address));
    private_->crypto_running = true;
    private_->crypto_failures = 0;
    private_->crypto_status = QStringLiteral("Обновляем кошельки в сетях EVM…");
    emit cryptoWalletsChanged();
    startNextCryptoWallet();
    return true;
}

void AppController::stopCryptoRefresh() {
    if (!private_->crypto_running) return;
    private_->crypto_pending.clear();
    if (private_->crypto_sync) {
        private_->crypto_sync->cancel();
        private_->crypto_sync->deleteLater();
        private_->crypto_sync.clear();
    }
    private_->crypto_running = false;
    private_->crypto_status = QStringLiteral("Обновление кошельков остановлено. Предыдущие данные сохранены.");
    emit cryptoWalletsChanged();
}

void AppController::startNextCryptoWallet() {
    if (!private_->crypto_running || private_->crypto_sync) return;
    if (private_->crypto_pending.isEmpty()) {
        private_->crypto_running = false;
        private_->crypto_status = private_->crypto_failures == 0
            ? QStringLiteral("Кошельки обновлены. Автообновление — каждые 6 часов, пока приложение открыто.")
            : QStringLiteral("Не удалось обновить кошельков: %1. Предыдущие данные сохранены.").arg(private_->crypto_failures);
        emit cryptoWalletsChanged();
        return;
    }
    const auto address = private_->crypto_pending.takeFirst();
    const auto advance = [this] {
        if (private_->crypto_sync) private_->crypto_sync->deleteLater();
        private_->crypto_sync.clear();
        emit cryptoWalletsChanged();
        QTimer::singleShot(0, this, [this] { startNextCryptoWallet(); });
    };
    auto* coordinator = new CryptoWalletSync(*private_->network,
        [this, address, advance](CryptoPortfolio portfolio) {
            const auto now = QDateTime::currentDateTimeUtc();
            auto& state = private_->crypto_states[address];
            state = {{QStringLiteral("value"), usdt_label(portfolio.usdtMinor)},
                {QStringLiteral("holdings"), portfolio.holdings}, {QStringLiteral("unpricedCount"), portfolio.unpricedCount},
                {QStringLiteral("supportedChains"), portfolio.supportedChains}, {QStringLiteral("networks"), portfolio.usedChains},
                {QStringLiteral("lastSync"), now.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))},
                {QStringLiteral("status"), QStringLiteral("Оценённая часть активов по данным Rabby.")}};
            const auto wallet = std::find_if(private_->crypto_wallets.begin(), private_->crypto_wallets.end(),
                [&](const auto& item) { return item.address == address.toStdString(); });
            if (wallet != private_->crypto_wallets.end() && private_->history_database) {
                const auto connection = "ethereum:" + wallet->address;
                const auto institution = "EVM · " + wallet->name;
                std::vector<storage::HistoryBalance> balances{{wallet->address, wallet->name + " · оценённые активы", "USDT", portfolio.usdtMinor}};
                const auto capturedAt = now.toString(Qt::ISODateWithMs).toStdString();
                storage::BalanceHistoryRepository repository(*private_->history_database);
                const auto saved = repository.append(connection, institution, capturedAt, balances);
                if (saved) {
                    private_->balance_history.push_back({*saved, connection, institution, capturedAt, std::move(balances)});
                } else {
                    state[QStringLiteral("status")] = QStringLiteral("Оценка обновлена, но историю сохранить не удалось.");
                    ++private_->crypto_failures;
                }
                emit balanceHistoryChanged();
            }
            advance();
        }, [this, address, advance](QString error) {
            private_->crypto_states[address][QStringLiteral("status")] = std::move(error);
            ++private_->crypto_failures;
            advance();
        }, this);
    private_->crypto_sync = coordinator;
    coordinator->start(address);
}

QVariantList AppController::balanceHistoryAccounts() const {
    return balance_history_accounts(private_->balance_history);
}

QVariantList AppController::balanceHistoryConnections() const {
    return balance_history_connections(private_->balance_history);
}

QStringList AppController::balanceHistoryCurrencies() const {
    return balance_history_currencies(private_->balance_history);
}

QString AppController::balanceHistoryError() const { return private_->history_error; }

QVariantMap AppController::balanceHistoryChart(const QString& accountId, const QString& currency) const {
    return balance_history_chart(private_->balance_history, accountId, currency);
}

QVariantMap AppController::filteredBalanceHistoryChart(const QString& accountId,
    const QString& currency, const QStringList& connectionIds, const bool restrictConnections,
    const QString& from, const QString& to, const bool daily) const {
    const auto start = QDate::fromString(from, QStringLiteral("dd.MM.yyyy"));
    const auto end = QDate::fromString(to, QStringLiteral("dd.MM.yyyy"));
    if ((!from.isEmpty() && !start.isValid()) || (!to.isEmpty() && !end.isValid())) {
        return {{QStringLiteral("error"), QStringLiteral("Укажите даты в формате ДД.ММ.ГГГГ.")}};
    }
    return balance_history_chart(private_->balance_history, accountId, currency,
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
    initializeBalanceHistory(historyPath);
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
            private_->alfa_sync_running = false;
            alfa_login_active_ = false;
            alfa_connected_ = true;
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
        },
        [this](AlfaLoginFailure failure) {
            private_->alfa_sync_running = false;
            alfa_login_active_ = false;
            if (private_->bank_batches.contains(QStringLiteral("alfa-retail"))) {
                failure.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
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
            private_->vtb_sync_running = false;
            vtb_login_active_ = false;
            vtb_connected_ = true;
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
        },
        [this](VtbLoginFailure failure) {
            private_->vtb_sync_running = false;
            vtb_login_active_ = false;
            if (private_->bank_batches.contains(QStringLiteral("vtb-retail"))) {
                failure.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
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
                    if ((locked_ || tbank_login_active_ || !synchronizeTBank()) &&
                        tbankAutoUpdate() && tbank_connected_) {
                        private_->next_tbank_auto_update_at = scheduleAutoUpdate(
                            private_->tbank_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit tbankConnectionChanged();
                    }
                }
            });
    private_->alfa_auto_update_timer.setSingleShot(true);
    connect(&private_->alfa_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (alfaAutoUpdate() && alfa_connected_ &&
                    !private_->alfa_sync_running) {
                    if ((locked_ || alfa_login_active_ || !synchronizeAlfa()) &&
                        alfaAutoUpdate() && alfa_connected_) {
                        private_->next_alfa_auto_update_at = scheduleAutoUpdate(
                            private_->alfa_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit alfaConnectionChanged();
                    }
                }
            });
    private_->vtb_auto_update_timer.setSingleShot(true);
    connect(&private_->vtb_auto_update_timer, &QTimer::timeout, this,
            [this] {
                if (vtbAutoUpdate() && vtb_connected_ &&
                    !private_->vtb_sync_running) {
                    if ((locked_ || vtb_login_active_ || !synchronizeVtb()) &&
                        vtbAutoUpdate() && vtb_connected_) {
                        private_->next_vtb_auto_update_at = scheduleAutoUpdate(
                            private_->vtb_auto_update_timer, QDateTime::currentDateTimeUtc(), 10);
                        emit vtbConnectionChanged();
                    }
                }
            });
    refreshZenPluginAutoUpdateSchedule();
    refreshTBankAutoUpdateSchedule();
    refreshAlfaAutoUpdateSchedule();
    refreshVtbAutoUpdateSchedule();
    refreshBankConnections();
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    if (tbank_connected_) private_->pending_retail_restores.push_back(QStringLiteral("tbank"));
    if (alfa_connected_) private_->pending_retail_restores.push_back(QStringLiteral("alfa"));
    if (vtb_connected_) private_->pending_retail_restores.push_back(QStringLiteral("vtb"));
    const auto resumeRestoration = [this] {
        if (!private_->pending_retail_restores.isEmpty()) {
            QTimer::singleShot(0, this, &AppController::restoreRetailConnections);
        }
    };
    connect(this, &AppController::tbankConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::alfaConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::vtbConnectionChanged, this, resumeRestoration);
    connect(this, &AppController::zenPluginChanged, this, resumeRestoration);
    connect(this, &AppController::lockedChanged, this, resumeRestoration);
    resumeRestoration();
    private_->crypto_timer.setInterval(autoUpdateIntervalSeconds * 1000);
    connect(&private_->crypto_timer, &QTimer::timeout, this, [this] {
        if (!locked_ && !demo_mode_ && !private_->crypto_running &&
            !private_->crypto_wallets.empty()) refreshCryptoWallets();
    });
    private_->crypto_timer.start();
    if (!private_->crypto_wallets.empty()) {
        QTimer::singleShot(0, this, [this] { refreshCryptoWallets(); });
    }
}

void AppController::restoreRetailConnections() {
    if (locked_ || zen_plugin_running_ || tbank_login_active_ || alfa_login_active_ ||
        vtb_login_active_) {
        return;
    }
    while (!private_->pending_retail_restores.isEmpty()) {
        const auto bank = private_->pending_retail_restores.takeFirst();
        if (bank == QStringLiteral("tbank") && tbank_connected_) (void)synchronizeTBank();
        if (bank == QStringLiteral("alfa") && alfa_connected_) (void)synchronizeAlfa();
        if (bank == QStringLiteral("vtb") && vtb_connected_) (void)synchronizeVtb();
    }
}

AppController::~AppController() {
    if (private_->crypto_sync) private_->crypto_sync->cancel();
}

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
    return demo_mode_ ? QStringLiteral("2 318 900 ₽") : live_portfolio_value_;
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
         QStringLiteral("Счета и остатки"),
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
         QStringLiteral("Счета и остатки"),
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
         QStringLiteral("Счета и остатки"),
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
         QStringLiteral("Счета и остатки"),
         QStringLiteral("Телефон + код · локальный адаптер"),
         QStringLiteral("Нужен адаптер"),
         false,
         false},
        {QStringLiteral("t-invest"),
         QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Счета, остатки и позиции"),
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

    private_->next_zen_auto_update_at =
        scheduleAutoUpdate(private_->zen_auto_update_timer, from);
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
    private_->next_tbank_auto_update_at =
        scheduleAutoUpdate(private_->tbank_auto_update_timer, from);
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
    private_->next_alfa_auto_update_at =
        scheduleAutoUpdate(private_->alfa_auto_update_timer, from);
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
    private_->next_vtb_auto_update_at =
        scheduleAutoUpdate(private_->vtb_auto_update_timer, from);
}

void AppController::loadDemoData() {
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
    live_portfolio_value_ = QStringLiteral("0 ₽");
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
    stopCryptoRefresh();
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

    // A new token must not be combined with responses obtained using the old token.
    if (connectorId == QStringLiteral("t-invest") && private_->investment_sync) {
        private_->investment_sync->cancel();
        private_->investment_sync.clear();
    }
    if (auto coordinator = private_->bank_sync_coordinators.take(connectorId)) {
        coordinator->cancel();
    }
    private_->bank_batches.remove(connectorId);
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
            private_->bank_batches.insert(QStringLiteral("t-invest"), std::move(batch));
            recordBalanceHistory(QStringLiteral("t-invest"));
            rebuildBankModels();
            emit syncStatusChanged();
        },
        [this](const QString& message) {
            private_->investment_sync.clear();
            sync_status_ = QStringLiteral("Ошибка Т-Инвест: %1").arg(message);
            emit syncStatusChanged();
        }, this);
    private_->investment_sync = coordinator;
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
            private_->bank_batches.insert(connectorId, std::move(batch));
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
    QVector<HoldingRow> holdingRows;
    std::int64_t portfolioValue = 0;
    bool portfolioValid = true;
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
                if (bank.key() == QStringLiteral("t-invest")) {
                    portfolioValid &= checkedAdd(portfolioValue, snapshot.balance.minorUnits);
                }
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
        for (const auto& position : bank->positions) {
            holdingRows.push_back({QStringLiteral("—"), position.instrumentId,
                QStringLiteral("%1 %2").arg(formatMinorUnits(position.currentValue.minorUnits),
                                            currencySymbol(position.currentValue.currency)),
                QStringLiteral("—"), QStringLiteral("—"), true});
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
    holdings_.replace(std::move(holdingRows));
    live_portfolio_value_ = portfolioValid
        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(portfolioValue)) : QStringLiteral("—");
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
            recordBalanceHistory(batchKey);
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
        vtb_login_active_) {
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
        "Сессия получена. Загружаем счета и остатки…");
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
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    if ((!tbank_connected_ && !private_->tbank_session_pending_validation)
        || !private_->credentials) {
        sync_status_ = QStringLiteral("Сначала войдите в Т-Банк");
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || alfa_login_active_ || vtb_login_active_) {
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
        tbank_connected_ = false;
        private_->tbank_session_pending_validation = false;
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
        sync_status_ = discovery.error().message;
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    }

    private_->tbank_auto_update_timer.stop();
    private_->tbank_sync_running = true;
    tbank_login_status_ = QStringLiteral("Обновляем остатки по счетам…");
    sync_status_ = QStringLiteral("Т-Банк: получаем счета…");
    refreshBankConnections();
    emit syncStatusChanged();
    emit tbankConnectionChanged();

    BankRuntime runtime{
        nullptr,
        1'000,
        [](const QByteArray& body, const QDateTime& observedAt) {
            return TBankRetailClient::parseAccountsResponse(body, observedAt);
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
            private_->tbank_sync_running = false;
            private_->tbank_session_pending_validation = false;
            tbank_connected_ = true;
            const auto accountCount = batch.accounts.size();
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
        },
        [this](ConnectorError error) {
            private_->tbank_sync_coordinator.clear();
            private_->tbank_sync_running = false;
            const bool authenticationRejected = error.code == ErrorCode::AuthenticationFailed;
            const bool validatedSession = private_->tbank_session_pending_validation;
            private_->tbank_session_pending_validation = false;
            if (authenticationRejected) {
                // A rejected request must not rewrite the Keychain during startup.
                // Keep the saved session until replacement or explicit disconnect,
                // but stop publishing data or scheduling refreshes with it.
                tbank_connected_ = false;
            }
            if (private_->bank_batches.contains(QStringLiteral("tbank-retail"))) {
                error.message += QStringLiteral(" Показаны последние успешно загруженные остатки.");
            }
            if (validatedSession && !authenticationRejected) {
                tbank_connected_ = true;
                tbank_login_status_ =
                    QStringLiteral("Т-Банк подключён, но синхронизация не завершена: %1")
                        .arg(error.message);
            } else {
                tbank_login_status_ = error.message;
            }
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
    private_->tbank_login->disconnectBank();
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
    if (zen_plugin_running_ || tbank_login_active_ || alfa_login_active_ || vtb_login_active_) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginAlfaLogin() {
    if (!prepareAlfaConnection()) {
        return;
    }
    private_->alfa_sync_running = false;
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
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }
    if (!alfa_connected_) {
        sync_status_ = QStringLiteral("Сначала войдите в Альфа-Банк");
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || tbank_login_active_ || vtb_login_active_) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        alfa_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit alfaConnectionChanged();
        return false;
    }

    private_->alfa_auto_update_timer.stop();
    private_->alfa_sync_running = true;
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
    if (zen_plugin_running_ || tbank_login_active_ || alfa_login_active_ || vtb_login_active_) {
        return reportFailure(QStringLiteral("Сначала завершите текущую синхронизацию"));
    }
    refreshBankConnections();
    return true;
}

void AppController::beginVtbLogin() {
    if (!prepareVtbConnection()) {
        return;
    }
    private_->vtb_sync_running = false;
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
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }
    if (!vtb_connected_) {
        sync_status_ = QStringLiteral("Сначала войдите в ВТБ");
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }
    if (zen_plugin_running_ || tbank_login_active_ || alfa_login_active_) {
        sync_status_ = QStringLiteral("Сначала завершите текущую синхронизацию");
        vtb_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit vtbConnectionChanged();
        return false;
    }

    private_->vtb_auto_update_timer.stop();
    private_->vtb_sync_running = true;
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
