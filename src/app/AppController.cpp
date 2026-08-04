#include "app/AppController.hpp"

#include <QDate>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
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
#include <utility>
#include <vector>

import wt.connectors;
import wt.platform;
import wt.read_only_resource;
import wt.tbank_auth;
import wt.tbank_session;

namespace wealthtracker::app {
namespace {

using wealthtracker::connectors::ConnectorResult;
using wealthtracker::connectors::ExternalAccount;
using wealthtracker::connectors::ILiveConnector;
using wealthtracker::connectors::LiveSyncRequest;
using wealthtracker::connectors::Money;
using wealthtracker::connectors::NormalizedBatch;
using wealthtracker::connectors::ZenPluginDescriptor;
using wealthtracker::connectors::ZenPluginEvent;
using wealthtracker::connectors::ZenPluginEventKind;

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
constexpr auto tbankRetailOrigin = "https://www.tbank.ru";
constexpr auto tbankBundleResource =
    ":/org/wealthtracker/runtime/bundles/tbank-retail/tbank-retail.cjs";
constexpr auto tbankPreferencesResource =
    ":/org/wealthtracker/runtime/bundles/tbank-retail/preferences.xml";

std::expected<void, QString> copyResourceFile(const QString& resourcePath,
                                              const QString& destination) {
    QFile source(resourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            QStringLiteral("Не найден встроенный ресурс Т-Банка"));
    }
    const auto contents = source.readAll();
    if (auto installed = install_read_only_resource(destination, contents);
        !installed) {
        return std::unexpected(
            QStringLiteral("Не удалось подготовить встроенный bundle Т-Банка"));
    }
    return {};
}

std::expected<QString, QString> extractedTBankBundlePath() {
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/bundles/tbank-retail-v1");
    if (directory.isEmpty() || !QDir{}.mkpath(directory)) {
        return std::unexpected(
            QStringLiteral("Не удалось создать каталог bundle Т-Банка"));
    }
    const auto bundlePath =
        directory + QStringLiteral("/tbank-retail.cjs");
    const auto preferencesPath =
        directory + QStringLiteral("/preferences.xml");
    if (auto copied = copyResourceFile(
            QString::fromLatin1(tbankBundleResource), bundlePath);
        !copied) {
        return std::unexpected(copied.error());
    }
    if (auto copied = copyResourceFile(
            QString::fromLatin1(tbankPreferencesResource), preferencesPath);
        !copied) {
        return std::unexpected(copied.error());
    }
    return QFileInfo(bundlePath).canonicalFilePath();
}

QNetworkRequest tbankAuthRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                         QByteArrayLiteral("WealthTracker/0.1"));
    request.setTransferTimeout(15'000);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                         QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                         QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    return request;
}

std::expected<QByteArray, QString> tbankAuthResponse(QNetworkReply& reply) {
    const auto status = reply.attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply.error() != QNetworkReply::NoError || status != 200) {
        return std::unexpected(
            QStringLiteral("Не удалось связаться с Т-Банком (сеть %1, HTTP %2)")
                .arg(static_cast<int>(reply.error()))
                .arg(status));
    }
    return reply.readAll();
}

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

class ZenPluginRunner final : public QObject {
public:
    using Prompted =
        std::function<void(wealthtracker::connectors::ZenPluginPrompt)>;
    using Completed = std::function<void(NormalizedBatch, QJsonObject)>;
    using Failed = std::function<void(QString)>;

    ZenPluginRunner(QString pluginId, Prompted prompted,
                    Completed completed, Failed failed, QObject* parent)
        : QObject(parent),
          plugin_id_(std::move(pluginId)),
          prompted_(std::move(prompted)),
          completed_(std::move(completed)),
          failed_(std::move(failed)),
          host_file_(QDir::tempPath()
                     + QStringLiteral(
                         "/wealthtracker-zen-host-XXXXXX.mjs")) {
        host_file_.setAutoRemove(true);
        QFile resource(
            QStringLiteral(
                ":/org/wealthtracker/runtime/scripts/zen_plugin_host.mjs"));
        host_ready_ =
            resource.open(QIODevice::ReadOnly) && host_file_.open()
            && host_file_.write(resource.readAll()) > 0
            && host_file_.flush();
        host_file_.close();

        process_.setProcessChannelMode(QProcess::SeparateChannels);
        connect(&process_, &QProcess::readyReadStandardOutput, this,
                [this] { readOutput(); });
        connect(&process_, &QProcess::readyReadStandardError, this,
                [this] { (void)process_.readAllStandardError(); });
        connect(
            &process_,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (!terminal_) {
                    fail(QStringLiteral(
                        "Локальный процесс плагина завершился без результата"));
                }
            });
        connect(&process_, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
                    if (!terminal_) {
                        fail(QStringLiteral(
                            "Не удалось запустить локальный процесс плагина"));
                    }
                });
        timeout_.setSingleShot(true);
        timeout_.setInterval(10 * 60 * 1000);
        connect(&timeout_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral(
                "Плагин превысил лимит времени синхронизации"));
        });
    }

    ~ZenPluginRunner() override {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }

    bool start(const QString& executable, const QByteArray& startMessage) {
        if (!host_ready_ || executable.isEmpty()
            || startMessage.isEmpty()) {
            return false;
        }
        pending_start_message_ = startMessage;
        connect(&process_, &QProcess::started, this, [this] {
            process_.write(pending_start_message_);
            pending_start_message_.fill('\0');
            pending_start_message_.clear();
            timeout_.start();
        });
        process_.start(executable, {host_file_.fileName()});
        return true;
    }

    bool answer(const QByteArray& message) {
        if (terminal_ || prompt_id_.isEmpty()
            || process_.state() != QProcess::Running
            || message.isEmpty()) {
            return false;
        }
        process_.write(message);
        prompt_id_.clear();
        return true;
    }

    void cancel() {
        if (terminal_) {
            return;
        }
        terminal_ = true;
        timeout_.stop();
        pending_start_message_.fill('\0');
        output_.fill('\0');
        process_.kill();
    }

private:
    static constexpr qsizetype maximumOutputBytes =
        8 * 1024 * 1024;

    void readOutput() {
        output_ += process_.readAllStandardOutput();
        if (output_.size() > maximumOutputBytes) {
            fail(QStringLiteral(
                "Ответ плагина превысил безопасный лимит"));
            return;
        }
        qsizetype newline = -1;
        while (!terminal_
               && (newline = output_.indexOf('\n')) >= 0) {
            const auto line = output_.first(newline);
            output_.remove(0, newline + 1);
            auto event =
                wealthtracker::connectors::parseZenPluginEvent(
                    line, plugin_id_);
            if (!event) {
                fail(event.error().message);
                return;
            }
            handleEvent(std::move(*event));
        }
    }

    void handleEvent(ZenPluginEvent event) {
        if (event.kind == ZenPluginEventKind::Prompt) {
            if (!prompt_id_.isEmpty()) {
                fail(QStringLiteral(
                    "Плагин запросил несколько кодов одновременно"));
                return;
            }
            prompt_id_ = event.prompt.requestId;
            prompted_(std::move(event.prompt));
            return;
        }
        terminal_ = true;
        timeout_.stop();
        if (event.kind == ZenPluginEventKind::Result) {
            completed_(std::move(event.batch),
                       std::move(event.state));
        } else {
            failed_(event.message);
        }
    }

    void fail(const QString& message) {
        if (terminal_) {
            return;
        }
        terminal_ = true;
        timeout_.stop();
        pending_start_message_.fill('\0');
        output_.fill('\0');
        process_.kill();
        failed_(message);
    }

    QString plugin_id_;
    QString prompt_id_;
    Prompted prompted_;
    Completed completed_;
    Failed failed_;
    QProcess process_;
    QTimer timeout_;
    QTemporaryFile host_file_;
    QByteArray pending_start_message_;
    QByteArray output_;
    bool host_ready_ = false;
    bool terminal_ = false;
};

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

    ~AppControllerPrivate() {
        tbank_candidate_session.fill('\0');
    }

    std::unique_ptr<CredentialBackend> credentials;
    QNetworkAccessManager network;
    wealthtracker::connectors::TInvestClient investment;
    wealthtracker::connectors::AlfaBankClient alfa;
    wealthtracker::connectors::TBankBusinessClient tbank_business;
    wealthtracker::connectors::ModulbankClient modulbank;
    QHash<QString, wealthtracker::connectors::NormalizedBatch> bank_batches;
    QHash<QString, QString> bank_institutions;
    std::optional<ZenPluginDescriptor> zen_descriptor;
    std::unique_ptr<ZenPluginRunner> zen_runner;
    QTimer zen_auto_update_timer;
    QDateTime next_zen_auto_update_at;
    QString zen_prompt_id;
    QString configured_zen_state_key;
    QNetworkAccessManager tbank_auth_network;
    QPointer<QNetworkReply> tbank_auth_reply;
    QTimer tbank_auth_poll_timer;
    QElapsedTimer tbank_auth_elapsed;
    QUrl tbank_authorization_url;
    QByteArray tbank_candidate_session;
    std::uint64_t tbank_auth_generation = 0;
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
    private_->tbank_auth_poll_timer.setSingleShot(true);
    connect(&private_->tbank_auth_poll_timer, &QTimer::timeout, this,
            &AppController::pollTBankAuthorization);
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

    QSettings settings;
    const auto configuredBundle =
        settings.value(QStringLiteral("zenPlugin/bundlePath"))
            .toString();
    if (!configuredBundle.isEmpty()) {
        auto descriptor =
            wealthtracker::connectors::inspectZenPluginBundle(
                configuredBundle);
        if (descriptor) {
            private_->zen_descriptor = std::move(*descriptor);
            private_->configured_zen_state_key =
                zenStateCredentialKey(*private_->zen_descriptor);
            zen_plugin_id_ = private_->zen_descriptor->pluginId;
            zen_plugin_bundle_path_ =
                private_->zen_descriptor->bundlePath;
            zen_plugin_institution_ =
                settings
                    .value(QStringLiteral("zenPlugin/institution"),
                           zen_plugin_id_)
                    .toString();
            zen_plugin_fields_ =
                zenPluginFieldVariants(*private_->zen_descriptor);
            if (zen_plugin_id_ == QString::fromLatin1(tbankRetailPluginId)
                && private_->credentials
                && private_->credentials->supported()) {
                auto state = private_->credentials->get(
                    private_->configured_zen_state_key);
                if (state && *state) {
                    QByteArray encoded = std::move(**state);
                    const auto document = QJsonDocument::fromJson(encoded);
                    encoded.fill('\0');
                    const auto pluginData = document.object()
                                                .value(QStringLiteral("pluginData"))
                                                .toObject();
                    auto sessionId = pluginData
                                         .value(QStringLiteral("sessionId"))
                                         .toString();
                    auto sessionBytes = sessionId.toUtf8();
                    tbank_connected_ = is_valid_tbank_session_id(
                        std::string_view{
                            sessionBytes.constData(),
                            static_cast<std::size_t>(sessionBytes.size())})
                        && settingsDateTime(
                            QStringLiteral("zenPlugin/lastSyncAt"))
                            .has_value();
                    sessionBytes.fill('\0');
                    sessionId.fill(QChar{'\0'});
                }
            }
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
    refreshZenPluginAutoUpdateSchedule();
    refreshBankConnections();
    import_status_ = QStringLiteral("Нет импортированных выписок");
    sync_status_ = QStringLiteral("Нет синхронизированных подключений");
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

QString AppController::tbankLoginStatus() const {
    return tbank_login_status_;
}

void AppController::refreshBankConnections() {
    const bool tbankBundleSelected = private_->zen_descriptor
        && private_->zen_descriptor->pluginId
            == QString::fromLatin1(tbankRetailPluginId);
    const bool externalBundleSelected = private_->zen_descriptor
        && !tbankBundleSelected;
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
         QStringLiteral("Системный браузер · локальный bundle"),
         tbank_login_active_
             ? QStringLiteral("Вход в банк")
             : zen_plugin_running_ && tbankBundleSelected
                 ? QStringLiteral("Синхронизация")
                 : tbank_connected_
                     ? zenPluginAutoUpdate()
                         ? QStringLiteral("Автообновление")
                         : QStringLiteral("Подключено")
                     : QStringLiteral("Требуется вход"),
         tbank_connected_,
         !nodeExecutable().isEmpty()},
        {QStringLiteral("alfa-retail"),
         QStringLiteral("Альфа-Банк"),
         QStringLiteral("Карты, счета и операции"),
         QStringLiteral("Телефон + код · локальный адаптер"),
         QStringLiteral("Нужен адаптер"),
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
         QStringLiteral("Телефон + код · локальный адаптер"),
         QStringLiteral("Нужен адаптер"),
         false,
         false},
        {QStringLiteral("vtb-retail"),
         QStringLiteral("ВТБ"),
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
        const auto institution =
            configuration ? configuration->institution
                          : private_->bank_institutions.value(bank.key());
        if (institution.isEmpty()) {
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
                institution,
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
                subtitle = institution;
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
    if (private_->zen_descriptor->pluginId
        == QString::fromLatin1(tbankRetailPluginId)) {
        const auto builtInPath = extractedTBankBundlePath();
        if (!builtInPath
            || QFileInfo(private_->zen_descriptor->bundlePath)
                    .canonicalFilePath()
                != *builtInPath) {
            sync_status_ = builtInPath
                ? QStringLiteral("Встроенный bundle Т-Банка имеет неверный путь")
                : builtInPath.error();
            emit syncStatusChanged();
            return false;
        }
        auto verified =
            wealthtracker::connectors::inspectZenPluginBundle(*builtInPath);
        if (!verified
            || verified->pluginId != QString::fromLatin1(tbankRetailPluginId)) {
            sync_status_ = QStringLiteral(
                "Не удалось проверить встроенный bundle Т-Банка");
            emit syncStatusChanged();
            return false;
        }
        private_->zen_descriptor = std::move(*verified);
        zen_plugin_fields_ =
            zenPluginFieldVariants(*private_->zen_descriptor);
    }
    const auto node = nodeExecutable();
    if (node.isEmpty()) {
        sync_status_ =
            QStringLiteral("Для Т-Банка и JavaScript-плагинов нужен Node.js 18+");
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
        .allowedOrigins =
            private_->zen_descriptor->pluginId
                == QString::fromLatin1(tbankRetailPluginId)
            ? QStringList{QString::fromLatin1(tbankRetailOrigin)}
            : QStringList{},
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
            if (pluginId == QString::fromLatin1(tbankRetailPluginId)) {
                tbank_connected_ = true;
                tbank_login_active_ = false;
                private_->tbank_candidate_session.fill('\0');
                private_->tbank_candidate_session.clear();
                private_->tbank_auth_poll_timer.stop();
                private_->tbank_authorization_url = QUrl{};
                private_->tbank_auth_elapsed.invalidate();
                ++private_->tbank_auth_generation;
                tbank_login_status_ = QStringLiteral(
                    "Т-Банк подключён. Сессия сохранена в системном хранилище.");
            }
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
            if (pluginId == QString::fromLatin1(tbankRetailPluginId)) {
                emit tbankConnectionChanged();
            }
        },
        [this, displayName, pluginId, stateKey](const QString& error) {
            zen_plugin_running_ = false;
            zen_plugin_prompt_.clear();
            private_->zen_prompt_id.clear();
            sync_status_ =
                QStringLiteral("%1: %2").arg(displayName, error);
            if (pluginId == QString::fromLatin1(tbankRetailPluginId)) {
                tbank_login_status_ = error;
                if (tbank_login_active_) {
                    tbank_connected_ = false;
                    tbank_login_active_ = false;
                    if (private_->credentials) {
                        (void)private_->credentials->remove(stateKey);
                    }
                } else if (error.contains(
                               QStringLiteral("Сессия Т-Банка истекла"))) {
                    tbank_connected_ = false;
                    if (private_->credentials) {
                        (void)private_->credentials->remove(stateKey);
                    }
                }
                if (error.contains(
                        QStringLiteral("Сессия Т-Банка истекла"))) {
                    tbank_login_status_ = QStringLiteral(
                        "Т-Банк подтвердил вход, но отклонил API-сессию. "
                        "Начните подключение заново.");
                }
            }
            scheduleZenPluginAutoUpdateAfter(
                QDateTime::currentDateTimeUtc());
            refreshBankConnections();
            emit zenPluginChanged();
            emit syncStatusChanged();
            if (pluginId == QString::fromLatin1(tbankRetailPluginId)) {
                emit tbankConnectionChanged();
            }
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
    const bool removingTBank = private_->zen_descriptor
        && private_->zen_descriptor->pluginId
            == QString::fromLatin1(tbankRetailPluginId);
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
    if (removingTBank) {
        tbank_connected_ = false;
        tbank_login_active_ = false;
        private_->tbank_candidate_session.fill('\0');
        private_->tbank_candidate_session.clear();
        private_->tbank_auth_poll_timer.stop();
        private_->tbank_authorization_url = QUrl{};
        private_->tbank_auth_elapsed.invalidate();
        ++private_->tbank_auth_generation;
        tbank_login_status_ = QStringLiteral("Т-Банк отключён");
    }
    if (!demo_mode_) {
        rebuildBankModels();
    }
    sync_status_ =
        QStringLiteral("Конфигурация локального плагина удалена");
    refreshBankConnections();
    emit zenPluginChanged();
    emit syncStatusChanged();
    if (removingTBank) {
        emit tbankConnectionChanged();
    }
}

bool AppController::prepareTBankConnection() {
    const auto reportFailure = [this](QString message) {
        sync_status_ = std::move(message);
        tbank_login_status_ = sync_status_;
        emit syncStatusChanged();
        emit tbankConnectionChanged();
        return false;
    };
    if (zen_plugin_running_) {
        return reportFailure(QStringLiteral(
            "Сначала завершите текущую синхронизацию"));
    }
    const auto path = extractedTBankBundlePath();
    if (!path) {
        return reportFailure(path.error());
    }
    if (!private_->zen_descriptor
        || private_->zen_descriptor->pluginId
            != QString::fromLatin1(tbankRetailPluginId)
        || private_->zen_descriptor->bundlePath != *path) {
        if (!inspectZenPluginBundle(QUrl::fromLocalFile(*path))) {
            return reportFailure(sync_status_);
        }
    }
    zen_plugin_institution_ = QStringLiteral("Т-Банк");
    refreshBankConnections();
    emit zenPluginChanged();
    return true;
}

void AppController::beginTBankLogin() {
    if (!prepareTBankConnection()) {
        return;
    }
    if (!private_->credentials || !private_->credentials->supported()) {
        tbank_login_status_ = QStringLiteral(
            "Системное хранилище секретов недоступно");
        emit tbankConnectionChanged();
        return;
    }

    ++private_->tbank_auth_generation;
    private_->tbank_auth_poll_timer.stop();
    if (private_->tbank_auth_reply) {
        disconnect(private_->tbank_auth_reply, nullptr, this, nullptr);
        private_->tbank_auth_reply->abort();
        private_->tbank_auth_reply->deleteLater();
        private_->tbank_auth_reply.clear();
    }
    private_->tbank_candidate_session.fill('\0');
    private_->tbank_candidate_session.clear();
    private_->tbank_authorization_url = QUrl{};
    private_->tbank_auth_elapsed.invalidate();
    tbank_login_active_ = true;
    tbank_login_status_ = QStringLiteral(
        "Создаём отдельную API-сессию Т-Банка…");
    refreshBankConnections();
    emit tbankConnectionChanged();

    const auto generation = private_->tbank_auth_generation;
    auto* reply = private_->tbank_auth_network.get(
        tbankAuthRequest(tbank_session_start_url()));
    private_->tbank_auth_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        if (generation != private_->tbank_auth_generation
            || private_->tbank_auth_reply != reply) {
            reply->deleteLater();
            return;
        }
        private_->tbank_auth_reply.clear();
        const auto response = tbankAuthResponse(*reply);
        reply->deleteLater();
        if (!response) {
            failTBankLogin(response.error());
            return;
        }
        auto session = parse_tbank_session_start(*response);
        if (!session) {
            failTBankLogin(session.error());
            return;
        }
        auto browserAuthorizationUrl =
            tbank_browser_authorization_url(*session);
        if (!browserAuthorizationUrl) {
            session->fill('\0');
            failTBankLogin(browserAuthorizationUrl.error());
            return;
        }
        private_->tbank_candidate_session = std::move(*session);
        private_->tbank_authorization_url = *browserAuthorizationUrl;
        private_->tbank_auth_elapsed.start();
        if (!QDesktopServices::openUrl(
                private_->tbank_authorization_url)) {
            failTBankLogin(QStringLiteral(
                "Не удалось открыть системный браузер. Проверьте "
                "приложение по умолчанию для HTTPS."));
            return;
        }
        tbank_login_status_ = QStringLiteral(
            "Браузер открыт. Войдите в Т-Банк по телефону и коду; "
            "WealthTracker автоматически проверит API-доступ.");
        private_->tbank_auth_poll_timer.start(2'000);
        emit tbankConnectionChanged();
    });
}

void AppController::openTBankLoginPage() {
    if (!tbank_login_active_
        || private_->tbank_authorization_url.isEmpty()) {
        return;
    }
    if (!QDesktopServices::openUrl(private_->tbank_authorization_url)) {
        failTBankLogin(QStringLiteral(
            "Не удалось открыть системный браузер. Проверьте приложение "
            "по умолчанию для HTTPS."));
    }
}

void AppController::checkTBankLogin() {
    if (tbank_login_active_) {
        pollTBankAuthorization();
    }
}

void AppController::pollTBankAuthorization() {
    if (!tbank_login_active_
        || private_->tbank_candidate_session.isEmpty()
        || private_->tbank_auth_reply) {
        return;
    }
    constexpr qint64 authorizationTimeoutMs = 10 * 60 * 1'000;
    if (!private_->tbank_auth_elapsed.isValid()
        || private_->tbank_auth_elapsed.elapsed() > authorizationTimeoutMs) {
        failTBankLogin(QStringLiteral(
            "Время входа истекло. Начните подключение Т-Банка заново."));
        return;
    }
    const auto pingUrl = tbank_ping_url(private_->tbank_candidate_session);
    if (!pingUrl) {
        failTBankLogin(pingUrl.error());
        return;
    }

    tbank_login_status_ = QStringLiteral(
        "Проверяем, завершён ли вход в системном браузере…");
    emit tbankConnectionChanged();
    const auto generation = private_->tbank_auth_generation;
    auto* reply = private_->tbank_auth_network.get(
        tbankAuthRequest(*pingUrl));
    private_->tbank_auth_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        if (generation != private_->tbank_auth_generation
            || private_->tbank_auth_reply != reply) {
            reply->deleteLater();
            return;
        }
        private_->tbank_auth_reply.clear();
        const auto response = tbankAuthResponse(*reply);
        reply->deleteLater();
        if (!response) {
            tbank_login_status_ = response.error()
                + QStringLiteral(". Повторим проверку автоматически.");
            private_->tbank_auth_poll_timer.start(3'000);
            emit tbankConnectionChanged();
            return;
        }
        const auto access = parse_tbank_access_level(*response);
        if (!access) {
            failTBankLogin(access.error());
            return;
        }
        if (*access == TBankAccessLevel::anonymous) {
            tbank_login_status_ = QStringLiteral(
                "Ожидаем завершения входа в системном браузере…");
            private_->tbank_auth_poll_timer.start(2'000);
            emit tbankConnectionChanged();
            return;
        }

        auto session = std::move(private_->tbank_candidate_session);
        private_->tbank_candidate_session.clear();
        private_->tbank_authorization_url = QUrl{};
        private_->tbank_auth_elapsed.invalidate();
        ++private_->tbank_auth_generation;
        acceptTBankSession(std::move(session));
    });
}

void AppController::failTBankLogin(QString message) {
    ++private_->tbank_auth_generation;
    private_->tbank_auth_poll_timer.stop();
    if (private_->tbank_auth_reply) {
        disconnect(private_->tbank_auth_reply, nullptr, this, nullptr);
        private_->tbank_auth_reply->abort();
        private_->tbank_auth_reply->deleteLater();
        private_->tbank_auth_reply.clear();
    }
    private_->tbank_candidate_session.fill('\0');
    private_->tbank_candidate_session.clear();
    private_->tbank_authorization_url = QUrl{};
    private_->tbank_auth_elapsed.invalidate();
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
    ++private_->tbank_auth_generation;
    private_->tbank_auth_poll_timer.stop();
    if (private_->tbank_auth_reply) {
        disconnect(private_->tbank_auth_reply, nullptr, this, nullptr);
        private_->tbank_auth_reply->abort();
        private_->tbank_auth_reply->deleteLater();
        private_->tbank_auth_reply.clear();
    }
    tbank_login_active_ = false;
    private_->tbank_candidate_session.fill('\0');
    private_->tbank_candidate_session.clear();
    private_->tbank_authorization_url = QUrl{};
    private_->tbank_auth_elapsed.invalidate();
    tbank_login_status_ = QStringLiteral("Вход отменён");
    if (!tbank_connected_ && private_->credentials
        && private_->zen_descriptor
        && private_->zen_descriptor->pluginId
            == QString::fromLatin1(tbankRetailPluginId)) {
        (void)private_->credentials->remove(
            zenStateCredentialKey(*private_->zen_descriptor));
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
    if (!is_valid_tbank_session_id(view)) {
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

    const auto stateKey = zenStateCredentialKey(*private_->zen_descriptor);
    QString sessionValue = QString::fromUtf8(session);
    session.fill('\0');
    const QJsonObject state{
        {QStringLiteral("pluginData"),
         QJsonObject{{QStringLiteral("sessionId"), sessionValue}}},
        {QStringLiteral("cookies"), QJsonArray{}}};
    auto encoded = QJsonDocument(state).toJson(QJsonDocument::Compact);
    sessionValue.fill(QChar{'\0'});
    const auto stored = private_->credentials->put(stateKey, encoded);
    encoded.fill('\0');
    if (!stored) {
        failTBankLogin(stored.error());
        return;
    }

    private_->configured_zen_state_key = stateKey;
    QSettings settings;
    settings.setValue(QStringLiteral("zenPlugin/bundlePath"),
                      private_->zen_descriptor->bundlePath);
    settings.setValue(QStringLiteral("zenPlugin/institution"),
                      QStringLiteral("Т-Банк"));
    settings.remove(QStringLiteral("zenPlugin/lastSyncAt"));
    tbank_connected_ = false;
    tbank_login_status_ = QStringLiteral(
        "Сессия получена. Проверяем доступ и загружаем операции…");
    refreshBankConnections();
    emit tbankConnectionChanged();

    const auto preferences = savedZenPluginPreferences().toVariantMap();
    if (!startZenPlugin(QStringLiteral("Т-Банк"), preferences)) {
        (void)private_->credentials->remove(stateKey);
        failTBankLogin(sync_status_);
    }
}

void AppController::disconnectTBank() {
    if (private_->zen_descriptor
        && private_->zen_descriptor->pluginId
            == QString::fromLatin1(tbankRetailPluginId)) {
        removeZenPlugin();
        return;
    }
    tbank_connected_ = false;
    tbank_login_active_ = false;
    private_->tbank_candidate_session.fill('\0');
    private_->tbank_candidate_session.clear();
    private_->tbank_auth_poll_timer.stop();
    private_->tbank_authorization_url = QUrl{};
    private_->tbank_auth_elapsed.invalidate();
    ++private_->tbank_auth_generation;
    tbank_login_status_ = QStringLiteral("Т-Банк отключён");
    refreshBankConnections();
    emit tbankConnectionChanged();
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
            "WealthTracker создаёт отдельную анонимную API-сессию и открывает "
            "официальный вход Т-Банка в системном браузере. Телефон, пароль "
            "и код остаются в браузере; приложение не читает cookies. Оно "
            "проверяет только выданный идентификатор API-сессии и сохраняет "
            "его в системном хранилище после подтверждения доступа CLIENT. "
            "Локальный MIT-bundle использует сессию только для чтения счетов "
            "и операций.");
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
