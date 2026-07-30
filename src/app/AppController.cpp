#include "app/AppController.hpp"

#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>

#include <cstddef>
#include <span>

import wt.connectors;
import wt.platform;

namespace wealthtracker::app {

class AppControllerPrivate final {
public:
    AppControllerPrivate()
        : credentials(wealthtracker::platform::make_system_credential_store(
              "org.wealthtracker.desktop")),
          investment(network) {}

    std::unique_ptr<wealthtracker::platform::CredentialStore> credentials;
    QNetworkAccessManager network;
    wealthtracker::connectors::TInvestClient investment;
};

AppController::AppController(QObject* parent)
    : QObject(parent),
      accounts_(this),
      transactions_(this),
      categories_(this),
      holdings_(this),
      private_(std::make_unique<AppControllerPrivate>()) {
    if (private_->credentials && private_->credentials->supported()) {
        auto database_key = private_->credentials->get("database-key");
        if (!database_key &&
            database_key.error().code ==
                wealthtracker::platform::CredentialErrorCode::not_found) {
            const auto initialized =
                wealthtracker::platform::VaultCrypto::initialize();
            auto generated =
                wealthtracker::platform::VaultCrypto::generate_database_key();
            if (initialized && generated) {
                (void)private_->credentials->put(
                    "database-key",
                    generated->bytes());
            }
        }
        investment_connected_ =
            static_cast<bool>(private_->credentials->get("t-invest-readonly"));
    }
    loadDemoData();
}

AppController::~AppController() = default;

QAbstractItemModel* AppController::accounts() noexcept { return &accounts_; }
QAbstractItemModel* AppController::transactions() noexcept { return &transactions_; }
QAbstractItemModel* AppController::categories() noexcept { return &categories_; }
QAbstractItemModel* AppController::holdings() noexcept { return &holdings_; }
QVariantList AppController::netWorthSeries() const { return net_worth_series_; }

QString AppController::netWorth() const {
    return demo_mode_ ? QStringLiteral("4 826 450 ₽") : QStringLiteral("0 ₽");
}

QString AppController::monthlyIncome() const {
    return demo_mode_ ? QStringLiteral("284 000 ₽") : QStringLiteral("0 ₽");
}

QString AppController::monthlySpending() const {
    return demo_mode_ ? QStringLiteral("146 830 ₽") : QStringLiteral("0 ₽");
}

QString AppController::savingsRate() const {
    return demo_mode_ ? QStringLiteral("48,3%") : QStringLiteral("—");
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
    demo_mode_ = false;
    accounts_.replace({});
    transactions_.replace({});
    categories_.replace({});
    holdings_.replace({});
    net_worth_series_.clear();
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Т-Инвестиции не подключены");
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
    auto database_key = private_->credentials->get("database-key");
    if (!database_key ||
        database_key->size() !=
            wealthtracker::platform::VaultCrypto::database_key_size) {
        return false;
    }
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
    if (!private_->credentials || !private_->credentials->supported()) {
        sync_status_ =
            QStringLiteral("Системное хранилище секретов недоступно");
        emit syncStatusChanged();
        return false;
    }

    QByteArray encoded = token.trimmed().toUtf8();
    if (encoded.size() < 20) {
        encoded.fill('\0');
        sync_status_ = QStringLiteral("Токен выглядит слишком коротким");
        emit syncStatusChanged();
        return false;
    }
    const auto* data = reinterpret_cast<const std::byte*>(encoded.constData());
    const auto result = private_->credentials->put(
        "t-invest-readonly",
        std::span<const std::byte>(data, static_cast<std::size_t>(encoded.size())));
    encoded.fill('\0');
    if (!result) {
        sync_status_ = QString::fromStdString(result.error().message);
        emit syncStatusChanged();
        return false;
    }

    investment_connected_ = true;
    sync_status_ = QStringLiteral("Токен сохранён в системном хранилище");
    emit investmentConnectedChanged();
    emit syncStatusChanged();
    return true;
}

void AppController::removeInvestmentToken() {
    if (private_->credentials) {
        const auto removed =
            private_->credentials->remove("t-invest-readonly");
        if (!removed &&
            removed.error().code !=
                wealthtracker::platform::CredentialErrorCode::not_found) {
            sync_status_ = QString::fromStdString(removed.error().message);
            emit syncStatusChanged();
            return;
        }
    }
    investment_connected_ = false;
    sync_status_ = QStringLiteral("Т-Инвестиции отключены");
    emit investmentConnectedChanged();
    emit syncStatusChanged();
}

void AppController::synchronizeInvestments() {
    using namespace wealthtracker::connectors;
    if (!private_->credentials || !investment_connected_) {
        sync_status_ = QStringLiteral("Сначала добавьте read-only токен");
        emit syncStatusChanged();
        return;
    }

    auto secret = private_->credentials->get("t-invest-readonly");
    if (!secret) {
        sync_status_ = QString::fromStdString(secret.error().message);
        emit syncStatusChanged();
        return;
    }
    QByteArray token(
        reinterpret_cast<const char*>(secret->data()),
        static_cast<qsizetype>(secret->size()));
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

}  // namespace wealthtracker::app
