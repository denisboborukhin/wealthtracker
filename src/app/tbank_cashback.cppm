module;

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <expected>
#include <functional>
#include <string_view>
#include <utility>

export module wt.tbank_cashback;

import wt.cashback;
import wt.cashback_sync;
import wt.connectors;

export namespace wealthtracker::app {

struct TBankCashbackContext {
    QJsonArray loyaltyIds;
    QSet<QString> accountIds;
    QMap<QString,QString> accountNames;
};

[[nodiscard]] std::expected<TBankCashbackContext,QString> tbank_cashback_context(const QByteArray& body) {
    const auto invalid = [] { return std::unexpected(QStringLiteral("Т-Банк не передал поддерживаемые программы кэшбэка.")); };
    if (body.isEmpty() || body.size() > 20 * 1024 * 1024) return invalid();
    const auto root = QJsonDocument::fromJson(body).object();
    if (root.value(QStringLiteral("resultCode")) != QStringLiteral("OK") || !root.value(QStringLiteral("payload")).isArray()) return invalid();
    TBankCashbackContext context;
    QSet<QString> ids;
    const auto rows = root.value(QStringLiteral("payload")).toArray();
    if (rows.size() > 10'000) return invalid();
    for (const auto& row : rows) {
        const auto account = row.toObject();
        const auto type = account.value(QStringLiteral("accountType")).toString();
        if (type != QStringLiteral("Current") && type != QStringLiteral("Credit") && type != QStringLiteral("CurrentKidsImported")) continue;
        const auto loyalty = account.value(QStringLiteral("loyaltyId"));
        if (loyalty.isNull() || loyalty.isUndefined() || (loyalty.isString() && loyalty.toString().isEmpty())) continue;
        const auto id = loyalty.toString();
        const auto accountId = account.value(QStringLiteral("id")).toString();
        if (!loyalty.isString() || id.size() > 128 || id.contains(QChar::Null) || accountId.isEmpty() || accountId.size() > 256) return invalid();
        context.accountIds.insert(accountId);
        const auto name = account.value(QStringLiteral("name")).toString().trimmed().left(48);
        context.accountNames.insert(accountId,name.isEmpty()
            ? QStringLiteral("Счёт %1").arg(context.accountIds.size()) : name);
        if (!ids.contains(id)) { context.loyaltyIds.append(id); ids.insert(id); }
        if (ids.size() > 64) return invalid();
    }
    if (context.loyaltyIds.isEmpty()) return invalid();
    return context;
}

[[nodiscard]] std::expected<CashbackSnapshot,QString> parse_tbank_cashback(
    const QByteArray& body, const TBankCashbackContext& context, const QDate& today) {
    const auto invalid = [] { return std::unexpected(QStringLiteral(
        "Т-Банк не передал выбранные категории в ожидаемом формате. Предыдущие данные сохранены.")); };
    if (body.isEmpty() || body.size() > 512 * 1024 || !today.isValid() || context.loyaltyIds.isEmpty()) return invalid();
    const auto document = QJsonDocument::fromJson(body);
    const auto root = document.object();
    if (!document.isObject() || (root.contains(QStringLiteral("resultCode")) && root.value(QStringLiteral("resultCode")) != QStringLiteral("OK"))) return invalid();
    const auto data = root.value(QStringLiteral("payload")).toObject().value(QStringLiteral("data"));
    if (!data.isArray() || data.toArray().size() > 128) return invalid();
    const QTimeZone moscow("Europe/Moscow");
    const auto timestamp = [&](const QJsonValue& value) -> QDateTime {
        if (!value.isString() && !value.isDouble()) return {};
        const auto text = value.toVariant().toString();
        static const QRegularExpression pattern(QStringLiteral("^[0-9]{10}(?:[0-9]{3})?$"));
        if (!pattern.match(text).hasMatch()) return {};
        const auto time = text.toLongLong();
        return QDateTime::fromMSecsSinceEpoch(text.size() == 10 ? time * 1000 : time,moscow);
    };
    const auto current = CashbackBook::monthStart(today);
    struct Candidate { QString name, percent, rate, label; int program = 0; };
    QMap<QDate,QList<Candidate>> selected;
    QMap<QDate,QMap<QString,QString>> programRates;
    CashbackSnapshot snapshot;
    int program = 0;
    for (const auto& value : data.toArray()) {
        if (!value.isObject()) return invalid();
        const auto entry = value.toObject();
        if (entry.value(QStringLiteral("serviceType")) != QStringLiteral("regularEssences")) continue;
        const auto offer = entry.value(QStringLiteral("data")).toObject();
        ++program;
        const auto period = offer.value(QStringLiteral("activePeriod")).toObject();
        const auto from = timestamp(period.value(QStringLiteral("fromMs")));
        const auto to = timestamp(period.value(QStringLiteral("toMs")));
        const auto categories = offer.value(QStringLiteral("essences"));
        const auto accounts = offer.value(QStringLiteral("accountIds"));
        if (!from.isValid() || !to.isValid() || to < from || from.daysTo(to) > 366 ||
            !categories.isArray() || categories.toArray().size() > 128 || !accounts.isArray() ||
            accounts.toArray().isEmpty() || accounts.toArray().size() > 1000) return invalid();
        QStringList accountIds, accountNames;
        for (const auto& account : accounts.toArray()) {
            if (!account.isString() || !context.accountIds.contains(account.toString())) return invalid();
            accountIds.append(account.toString());
            accountNames.append(context.accountNames.value(account.toString(),QStringLiteral("Счёт")));
        }
        accountIds.sort();
        const auto scope = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(accountIds)).toJson(QJsonDocument::Compact))
            + offer.value(QStringLiteral("id")).toVariant().toString();
        for (const auto& month : {current,current.addMonths(1)}) {
            if (to.date() < month || from.date() > month.addMonths(1).addDays(-1)) continue;
            auto& monthRows = selected[month];
            snapshot.months.insert(month);
            for (const auto& item : categories.toArray()) {
                if (!item.isObject()) return invalid();
                const auto category = item.toObject();
                const auto active = category.value(QStringLiteral("isActive"));
                if (!active.isBool()) return invalid();
                if (!active.toBool()) continue;
                const auto name = category.value(QStringLiteral("name")).toString().trimmed();
                const auto rate = category.value(QStringLiteral("percent"));
                if (!rate.isString() && !rate.isDouble()) return invalid();
                CashbackBook validated;
                const auto percent = rate.toVariant().toString();
                if (!validated.set(QStringLiteral("tbank"),month.toString(Qt::ISODate),name,percent,today)) return invalid();
                const auto normalizedRate = validated.rows(QStringLiteral("tbank"),month).front().toMap().value(QStringLiteral("percent")).toString();
                const auto key = scope + QLatin1Char('/') + name.toCaseFolded();
                auto& rates = programRates[month];
                if (rates.contains(key)) {
                    if (rates.value(key) != normalizedRate) return invalid();
                    continue;
                }
                rates.insert(key,normalizedRate);
                monthRows.append({name,percent,normalizedRate,accountNames.join(QStringLiteral(", ")).left(48),program});
                if (monthRows.size() > 256) return invalid();
            }
        }
    }
    for (auto month = selected.cbegin(); month != selected.cend(); ++month) {
        QMap<QString,QSet<QString>> ratesByName;
        for (const auto& category : *month) ratesByName[category.name.toCaseFolded()].insert(category.rate);
        QMap<QString,QString> displayRates;
        for (const auto& category : *month) {
            const auto name = ratesByName.value(category.name.toCaseFolded()).size() > 1
                ? QStringLiteral("%1 · %2 · программа %3").arg(category.name,category.label).arg(category.program)
                : category.name;
            const auto key = name.toCaseFolded();
            if (displayRates.contains(key) && displayRates.value(key) != category.rate) return invalid();
            displayRates.insert(key,category.rate);
            if (!snapshot.categories.set(QStringLiteral("tbank"),month.key().toString(Qt::ISODate),name,category.percent,today)) return invalid();
        }
    }
    if (snapshot.months.isEmpty()) return invalid();
    return snapshot;
}

class TBankCashbackSync final : public QObject {
public:
    using Completed = std::function<void(CashbackSnapshot)>;
    using Failed = std::function<void(QString)>;
    TBankCashbackSync(QNetworkAccessManager& network, Completed completed, Failed failed, QObject* parent)
        : QObject(parent), network_(network), completed_(std::move(completed)), failed_(std::move(failed)) {
        deadline_.setSingleShot(true);
        connect(&deadline_,&QTimer::timeout,this,[this] { fail(); });
    }
    ~TBankCashbackSync() override { cancel(); }
    void start(QByteArray session, TBankCashbackContext context) {
        const auto validSession = wealthtracker::connectors::isValidTBankRetailSessionId(
            std::string_view(session.constData(),static_cast<std::size_t>(session.size())));
        if (started_) { session.fill('\0'); return; }
        started_ = true;
        active_ = true;
        if (!validSession || context.loyaltyIds.isEmpty() || context.loyaltyIds.size() > 64) { session.fill('\0'); fail(); return; }
        context_ = std::move(context);
        today_ = QDate::currentDate();
        QUrl url(QStringLiteral("https://www.tbank.ru/api/ms-gateway/loyalty_api/api/bonuses"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("origin"),QStringLiteral("web,ib5,platform"));
        query.addQueryItem(QStringLiteral("appName"),QStringLiteral("supreme"));
        query.addQueryItem(QStringLiteral("appVersion"),QStringLiteral("0.0.1"));
        query.addQueryItem(QStringLiteral("sessionid"),QString::fromLatin1(session));
        url.setQuery(query);
        session.fill('\0');
        wealthtracker::connectors::HostAllowlist policy({QStringLiteral("www.tbank.ru")});
        auto request = policy.makeRequest(url);
        if (!request) { fail(); return; }
        request->setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
        request->setRawHeader(QByteArrayLiteral("Accept"),QByteArrayLiteral("application/json"));
        request->setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
        request->setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
        request->setAttribute(QNetworkRequest::Http2AllowedAttribute,false);
        request->setAttribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute,512 * 1024);
        request->setTransferTimeout(10'000);
        const auto body = QJsonDocument(QJsonObject{{QStringLiteral("loyaltyIds"),context_.loyaltyIds},
            {QStringLiteral("origin"),QStringLiteral("web,ib5,platform")}}).toJson(QJsonDocument::Compact);
        reply_ = network_.post(*request,body);
        deadline_.start(10'000);
        connect(reply_,&QIODevice::readyRead,this,[this] { read(); });
        connect(reply_,&QNetworkReply::finished,this,[this] {
            if (!active_) return;
            const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 400 && status <= 599) {
                fail(QStringLiteral("Сервис кэшбэка Т-Банка ответил HTTP %1. Предыдущие категории сохранены.").arg(status));
                return;
            }
            if (reply_->error() != QNetworkReply::NoError || status != 200) { fail(); return; }
            read();
            if (!active_) return;
            auto result = parse_tbank_cashback(body_,context_,today_);
            if (!result) { fail(result.error()); return; }
            auto snapshot = std::move(*result);
            cancel();
            if (completed_) completed_(std::move(snapshot));
        });
    }
    void cancel() {
        active_ = false;
        deadline_.stop();
        if (reply_) {
            disconnect(reply_,nullptr,this,nullptr);
            reply_->abort(); reply_->deleteLater(); reply_.clear();
        }
        body_.clear();
    }
private:
    void read() {
        if (!active_ || !reply_ || !reply_->isReadable()) return;
        if (reply_->bytesAvailable() > 512 * 1024 - body_.size()) { fail(); return; }
        body_ += reply_->readAll();
        if (body_.size() > 512 * 1024) fail();
    }
    void fail(QString message = {}) {
        if (!active_) return;
        cancel();
        if (failed_) failed_(message.isEmpty()
            ? QStringLiteral("Не удалось обновить кэшбэк Т-Банка. Предыдущие категории сохранены.") : std::move(message));
    }
    QNetworkAccessManager& network_;
    Completed completed_;
    Failed failed_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    TBankCashbackContext context_;
    QDate today_;
    QByteArray body_;
    bool active_ = false;
    bool started_ = false;
};

} // namespace wealthtracker::app
