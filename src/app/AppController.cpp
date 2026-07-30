#include "app/AppController.hpp"

#include <QDate>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <utility>

import wt.connectors;
import wt.platform;

namespace wealthtracker::app {
namespace {

using wealthtracker::connectors::ConnectorResult;
using wealthtracker::connectors::ExternalAccount;
using wealthtracker::connectors::ILiveConnector;
using wealthtracker::connectors::LiveSyncRequest;
using wealthtracker::connectors::Money;
using wealthtracker::connectors::NormalizedBatch;

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
    if (connectorId == QStringLiteral("alfa-api")) {
        return QStringLiteral("#EF4444");
    }
    if (connectorId == QStringLiteral("tbank-business")) {
        return QStringLiteral("#F9C74F");
    }
    return QStringLiteral("#2563EB");
}

struct BankRuntime {
    ILiveConnector* connector = nullptr;
    QString institution;
    int pageSize = 50;
    std::function<ConnectorResult<NormalizedBatch>(
        const QByteArray&, const QDateTime&)> parseAccounts;
    std::function<ConnectorResult<NormalizedBatch>(
        const QByteArray&, const QString&, const QByteArray&, int)> parsePage;
};

class BankSyncCoordinator final : public QObject {
public:
    using LoadCredential =
        std::function<std::expected<QByteArray, QString>()>;
    using Completed = std::function<void(NormalizedBatch)>;
    using Failed = std::function<void(QString)>;

    BankSyncCoordinator(BankRuntime runtime, LoadCredential loadCredential,
                        Completed completed, Failed failed, QObject* parent)
        : QObject(parent),
          runtime_(std::move(runtime)),
          load_credential_(std::move(loadCredential)),
          completed_(std::move(completed)),
          failed_(std::move(failed)) {}

    void start(QNetworkReply* discoveryReply) {
        QPointer<QNetworkReply> reply = discoveryReply;
        connect(discoveryReply, &QNetworkReply::finished, this,
                [this, reply] {
                    if (!reply) {
                        fail(QStringLiteral(
                            "Запрос счетов был отменён до завершения"));
                        finishIfReady();
                        return;
                    }
                    const auto networkError = reply->error();
                    const auto errorText = reply->errorString();
                    const QByteArray body = reply->readAll();
                    reply->deleteLater();
                    if (networkError != QNetworkReply::NoError) {
                        fail(QStringLiteral("Ошибка получения счетов: %1")
                                 .arg(errorText));
                        finishIfReady();
                        return;
                    }

                    auto parsed =
                        runtime_.parseAccounts(body,
                                               QDateTime::currentDateTimeUtc());
                    if (!parsed) {
                        fail(parsed.error().message);
                        finishIfReady();
                        return;
                    }
                    batch_.accounts = std::move(parsed->accounts);
                    batch_.balances = std::move(parsed->balances);
                    batch_.warnings = std::move(parsed->warnings);
                    if (batch_.accounts.isEmpty()) {
                        fail(QStringLiteral("Банк не вернул доступных счетов"));
                        finishIfReady();
                        return;
                    }

                    scheduling_initial_pages_ = true;
                    for (const auto& account : batch_.accounts) {
                        requestPage(account.externalId, {});
                    }
                    scheduling_initial_pages_ = false;
                    finishIfReady();
                });
    }

private:
    static constexpr int maximumPagesPerAccount = 250;

    void requestPage(const QString& accountId, const QByteArray& cursor) {
        const auto requestKey =
            accountId + QLatin1Char('\n') + QString::fromUtf8(cursor);
        if (requested_pages_.contains(requestKey)) {
            fail(QStringLiteral(
                "Банк повторил курсор страницы; синхронизация остановлена"));
            return;
        }
        const auto pages = pages_per_account_.value(accountId);
        if (pages >= maximumPagesPerAccount) {
            fail(QStringLiteral(
                "История превысила безопасный лимит страниц"));
            return;
        }
        requested_pages_.insert(requestKey);
        pages_per_account_.insert(accountId, pages + 1);

        auto token = load_credential_();
        if (!token) {
            fail(token.error());
            return;
        }
        const LiveSyncRequest syncRequest{
            .accountExternalId = accountId,
            .cursor = cursor,
            .from = QDateTime(
                QDate::currentDate().addYears(-1), QTime(0, 0),
                QTimeZone::UTC),
            .pageSize = runtime_.pageSize,
        };
        auto requests = runtime_.connector->synchronize(syncRequest, *token);
        token->fill('\0');
        if (!requests) {
            fail(requests.error().message);
            return;
        }
        if (requests->isEmpty()) {
            fail(QStringLiteral(
                "Коннектор не создал запрос истории операций"));
            return;
        }

        for (QNetworkReply* rawReply : *requests) {
            ++pending_pages_;
            QPointer<QNetworkReply> reply = rawReply;
            connect(rawReply, &QNetworkReply::finished, this,
                    [this, reply, accountId, cursor] {
                        QByteArray nextCursor;
                        if (!reply) {
                            fail(QStringLiteral(
                                "Запрос истории был отменён до завершения"));
                        } else {
                            const auto networkError = reply->error();
                            const auto errorText = reply->errorString();
                            const QByteArray body = reply->readAll();
                            reply->deleteLater();
                            if (networkError != QNetworkReply::NoError) {
                                fail(QStringLiteral(
                                         "Ошибка получения операций: %1")
                                         .arg(errorText));
                            } else {
                                auto parsed = runtime_.parsePage(
                                    body, accountId, cursor,
                                    runtime_.pageSize);
                                if (!parsed) {
                                    fail(parsed.error().message);
                                } else {
                                    batch_.transactions +=
                                        std::move(parsed->transactions);
                                    batch_.warnings +=
                                        std::move(parsed->warnings);
                                    nextCursor =
                                        std::move(parsed->nextCursor);
                                }
                            }
                        }
                        --pending_pages_;
                        if (!nextCursor.isEmpty() && first_error_.isEmpty()) {
                            requestPage(accountId, nextCursor);
                        }
                        finishIfReady();
                    });
        }
    }

    void fail(const QString& message) {
        if (first_error_.isEmpty()) {
            first_error_ = message;
        }
    }

    void finishIfReady() {
        if (scheduling_initial_pages_ || pending_pages_ != 0) {
            return;
        }
        if (!first_error_.isEmpty()) {
            failed_(first_error_);
        } else {
            completed_(std::move(batch_));
        }
        deleteLater();
    }

    BankRuntime runtime_;
    LoadCredential load_credential_;
    Completed completed_;
    Failed failed_;
    NormalizedBatch batch_;
    QHash<QString, int> pages_per_account_;
    QSet<QString> requested_pages_;
    QString first_error_;
    int pending_pages_ = 0;
    bool scheduling_initial_pages_ = false;
};

}  // namespace

class AppControllerPrivate final {
public:
    explicit AppControllerPrivate(
        std::unique_ptr<CredentialBackend> credentialBackend)
        : credentials(std::move(credentialBackend)),
          investment(network),
          alfa(network),
          tbank_business(network),
          modulbank(network) {}

    std::unique_ptr<CredentialBackend> credentials;
    QNetworkAccessManager network;
    wealthtracker::connectors::TInvestClient investment;
    wealthtracker::connectors::AlfaBankClient alfa;
    wealthtracker::connectors::TBankBusinessClient tbank_business;
    wealthtracker::connectors::ModulbankClient modulbank;
    QHash<QString, wealthtracker::connectors::NormalizedBatch> bank_batches;
};

AppController::AppController(QObject* parent)
    : AppController(std::make_unique<SystemCredentialBackend>(), parent) {}

AppController::AppController(std::unique_ptr<CredentialBackend> credentials,
                             QObject* parent)
    : QObject(parent),
      accounts_(this),
      transactions_(this),
      categories_(this),
      holdings_(this),
      bank_connections_(this),
      private_(
          std::make_unique<AppControllerPrivate>(std::move(credentials))) {
    if (private_->credentials && private_->credentials->supported()) {
        auto database_key =
            private_->credentials->get(QStringLiteral("database-key"));
        if (database_key && !*database_key) {
            const auto initialized =
                wealthtracker::platform::VaultCrypto::initialize();
            auto generated =
                wealthtracker::platform::VaultCrypto::generate_database_key();
            if (initialized && generated) {
                QByteArray encoded(
                    reinterpret_cast<const char*>(generated->data()),
                    static_cast<qsizetype>(generated->size()));
                (void)private_->credentials->put(
                    QStringLiteral("database-key"), encoded);
                encoded.fill('\0');
            }
        }
        if (database_key && *database_key) {
            (**database_key).fill('\0');
        }
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
    }
    refreshBankConnections();
    loadDemoData();
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

QString AppController::monthlyIncome() const {
    return demo_mode_ ? QStringLiteral("284 000 ₽") : live_monthly_income_;
}

QString AppController::monthlySpending() const {
    return demo_mode_ ? QStringLiteral("146 830 ₽") : live_monthly_spending_;
}

QString AppController::savingsRate() const {
    return demo_mode_ ? QStringLiteral("48,3%") : live_savings_rate_;
}

QString AppController::portfolioValue() const {
    return demo_mode_ ? QStringLiteral("2 318 900 ₽") : QStringLiteral("0 ₽");
}

QString AppController::importStatus() const { return import_status_; }
QString AppController::syncStatus() const { return sync_status_; }
bool AppController::locked() const noexcept { return locked_; }
bool AppController::demoMode() const noexcept { return demo_mode_; }
bool AppController::investmentConnected() const noexcept {
    return investment_connected_;
}
QString AppController::connectionMessage() const { return connection_message_; }

void AppController::refreshBankConnections() {
    bank_connections_.replace({
        {QStringLiteral("tbank-retail"),
         QStringLiteral("Т-Банк"),
         QStringLiteral("Карты, счета и операции"),
         QStringLiteral("Локальный интерактивный плагин"),
         QStringLiteral("Нет официального API"),
         false,
         false},
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
         QStringLiteral("Локальный интерактивный плагин"),
         QStringLiteral("Нет официального API"),
         false,
         false},
        {QStringLiteral("vtb-retail"),
         QStringLiteral("ВТБ"),
         QStringLiteral("Карты, счета и операции"),
         QStringLiteral("Локальный интерактивный плагин"),
         QStringLiteral("Нет официального API"),
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
    });
}

void AppController::loadDemoData() {
    demo_mode_ = true;
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
         QStringLiteral("Продукты"), QStringLiteral("−4 860 ₽"), QStringLiteral("Сегодня"), false},
        {QStringLiteral("Заработная плата"), QStringLiteral("ООО «Компания»"),
         QStringLiteral("Доход"), QStringLiteral("+240 000 ₽"), QStringLiteral("29 июля"), true},
        {QStringLiteral("Яндекс Go"), QStringLiteral("Поездка"),
         QStringLiteral("Транспорт"), QStringLiteral("−1 170 ₽"), QStringLiteral("29 июля"), false},
        {QStringLiteral("Пополнение брокерского счёта"), QStringLiteral("Внутренний перевод"),
         QStringLiteral("Перевод"), QStringLiteral("−50 000 ₽"), QStringLiteral("28 июля"), false},
        {QStringLiteral("Купон ОФЗ"), QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Инвестиционный доход"), QStringLiteral("+3 480 ₽"), QStringLiteral("27 июля"), true},
        {QStringLiteral("ЖКХ"), QStringLiteral("Единый платёжный документ"),
         QStringLiteral("Дом"), QStringLiteral("−12 640 ₽"), QStringLiteral("25 июля"), false},
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
    live_monthly_income_ = QStringLiteral("0 ₽");
    live_monthly_spending_ = QStringLiteral("0 ₽");
    live_savings_rate_ = QStringLiteral("—");
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
    emit dashboardChanged();
    emit importStatusChanged();
    emit syncStatusChanged();
    emit demoModeChanged();
}

void AppController::lock() {
    if (!locked_) {
        locked_ = true;
        emit lockedChanged();
    }
}

bool AppController::unlock() {
    if (!private_->credentials || !private_->credentials->supported()) {
        return false;
    }
    auto database_key =
        private_->credentials->get(QStringLiteral("database-key"));
    if (!database_key || !*database_key ||
        (**database_key).size() !=
            wealthtracker::platform::VaultCrypto::database_key_size) {
        if (database_key && *database_key) {
            (**database_key).fill('\0');
        }
        return false;
    }
    (**database_key).fill('\0');
    locked_ = false;
    emit lockedChanged();
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
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (!reply) {
            return;
        }
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            sync_status_ =
                QStringLiteral("Ошибка Т-Инвест: %1").arg(reply->errorString());
            emit syncStatusChanged();
            reply->deleteLater();
            return;
        }
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
        [this, institution = configuration->institution](
            const QString& error) {
            sync_status_ =
                QStringLiteral("%1: %2").arg(institution, error);
            emit syncStatusChanged();
        },
        this);
    coordinator->start(*discovery);
}

void AppController::rebuildBankModels() {
    struct DatedTransaction {
        TransactionRow row;
        QDateTime sortTime;
    };

    QVector<AccountRow> accountRows;
    QVector<DatedTransaction> transactionRows;
    QHash<QString, std::int64_t> categorySpending;
    std::int64_t rubNetWorth = 0;
    std::int64_t monthlyIncome = 0;
    std::int64_t monthlySpending = 0;
    bool totalsValid = true;
    const auto today = QDate::currentDate();

    for (auto bank = private_->bank_batches.cbegin();
         bank != private_->bank_batches.cend(); ++bank) {
        const auto configuration = bankConfiguration(bank.key());
        if (!configuration) {
            continue;
        }
        QHash<QString, Money> balances;
        for (const auto& snapshot : bank->balances) {
            balances.insert(snapshot.accountExternalId, snapshot.balance);
            if (snapshot.balance.currency == QStringLiteral("RUB")) {
                totalsValid &=
                    checkedAdd(rubNetWorth, snapshot.balance.minorUnits);
            }
        }
        for (const auto& account : bank->accounts) {
            const auto balance = balances.value(
                account.externalId, Money{0, account.currency});
            accountRows.push_back({
                account.name,
                configuration->institution,
                account.kind,
                formatMinorUnits(balance.minorUnits),
                currencySymbol(balance.currency),
                connectorAccent(bank.key()),
            });
        }
        for (const auto& transaction : bank->transactions) {
            auto title = transaction.merchant.trimmed();
            if (title.isEmpty()) {
                title = transaction.description.trimmed();
            }
            if (title.isEmpty()) {
                title = QStringLiteral("Операция");
            }
            auto subtitle = transaction.description.trimmed();
            if (subtitle.isEmpty() || subtitle == title) {
                subtitle = configuration->institution;
            }
            const auto category = transaction.bankCategory.trimmed().isEmpty()
                ? QStringLiteral("Без категории")
                : transaction.bankCategory.trimmed();
            const auto sortTime = transaction.occurredAt.value_or(
                QDateTime(transaction.postingDate, QTime(0, 0),
                          QTimeZone::UTC));
            const auto dateText = transaction.postingDate == today
                ? QStringLiteral("Сегодня")
                : QLocale(QLocale::Russian, QLocale::Russia)
                      .toString(transaction.postingDate,
                                QStringLiteral("d MMMM"));
            transactionRows.push_back({
                {title, subtitle, category, signedMoney(transaction.amount),
                 dateText, transaction.amount.minorUnits >= 0},
                sortTime,
            });

            if (transaction.amount.currency != QStringLiteral("RUB") ||
                transaction.postingDate.year() != today.year() ||
                transaction.postingDate.month() != today.month()) {
                continue;
            }
            if (transaction.amount.minorUnits >= 0) {
                totalsValid &=
                    checkedAdd(monthlyIncome,
                               transaction.amount.minorUnits);
            } else {
                const auto spending = -transaction.amount.minorUnits;
                totalsValid &= checkedAdd(monthlySpending, spending);
                auto categoryTotal = categorySpending.value(category);
                if (checkedAdd(categoryTotal, spending)) {
                    categorySpending.insert(category, categoryTotal);
                } else {
                    totalsValid = false;
                }
            }
        }
    }

    std::sort(transactionRows.begin(), transactionRows.end(),
              [](const DatedTransaction& left,
                 const DatedTransaction& right) {
                  return left.sortTime > right.sortTime;
              });
    QVector<TransactionRow> sortedTransactions;
    sortedTransactions.reserve(transactionRows.size());
    for (auto& transaction : transactionRows) {
        sortedTransactions.push_back(std::move(transaction.row));
    }

    QVector<std::pair<QString, std::int64_t>> sortedCategories;
    sortedCategories.reserve(categorySpending.size());
    for (auto category = categorySpending.cbegin();
         category != categorySpending.cend(); ++category) {
        sortedCategories.emplace_back(category.key(), category.value());
    }
    std::sort(sortedCategories.begin(), sortedCategories.end(),
              [](const auto& left, const auto& right) {
                  return left.second > right.second;
              });
    static const QStringList categoryColors{
        QStringLiteral("#2563EB"), QStringLiteral("#7C3AED"),
        QStringLiteral("#059669"), QStringLiteral("#D97706"),
        QStringLiteral("#64748B")};
    QVector<CategoryRow> categoryRows;
    categoryRows.reserve(sortedCategories.size());
    for (qsizetype index = 0; index < sortedCategories.size(); ++index) {
        const auto& [name, amount] = sortedCategories.at(index);
        const auto share = monthlySpending > 0
            ? static_cast<double>(amount) /
                  static_cast<double>(monthlySpending)
            : 0.0;
        categoryRows.push_back(
            {name, QStringLiteral("%1 ₽").arg(formatMinorUnits(amount)),
             share, categoryColors.at(index % categoryColors.size())});
    }

    accounts_.replace(std::move(accountRows));
    transactions_.replace(std::move(sortedTransactions));
    categories_.replace(std::move(categoryRows));
    holdings_.replace({});
    net_worth_series_.clear();
    live_net_worth_ = totalsValid
        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(rubNetWorth))
        : QStringLiteral("—");
    live_monthly_income_ = totalsValid
        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(monthlyIncome))
        : QStringLiteral("—");
    live_monthly_spending_ = totalsValid
        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(monthlySpending))
        : QStringLiteral("—");
    if (totalsValid && monthlyIncome > 0) {
        auto percentage = QString::number(
            (static_cast<long double>(monthlyIncome - monthlySpending) *
             100.0L) /
                static_cast<long double>(monthlyIncome),
            'f', 1);
        percentage.replace(QLatin1Char('.'), QLatin1Char(','));
        live_savings_rate_ = percentage + QLatin1Char('%');
    } else {
        live_savings_rate_ = QStringLiteral("—");
    }
    const bool demoChanged = demo_mode_;
    demo_mode_ = false;
    emit dashboardChanged();
    if (demoChanged) {
        emit demoModeChanged();
    }
}

void AppController::describeBankConnection(const QString& connectorId) {
    if (connectorId == QStringLiteral("alfa-api")) {
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
