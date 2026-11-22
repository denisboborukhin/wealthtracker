module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include "app/BrowserSession.hpp"
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QUuid>
#include <QtWebView/QWebView>

#include <expected>
#include <functional>
#include <utility>

}

export module wt.cashback_sync;

import wt.cashback;
import wt.alfa_cashback;
import wt.ozon_cashback;

export namespace wealthtracker::app {

struct CashbackSnapshot {
    CashbackBook categories;
    QSet<QDate> months;
    QSet<QDate> selectionRequired;
    QSet<QString> upperBounds;
    QMap<QDate,QDateTime> observedAt;

    void merge(const QString& bank, const CashbackSnapshot& incoming, const QDateTime& refreshedAt) {
        const auto current = CashbackBook::monthStart(refreshedAt.date());
        categories.replaceMonths(incoming.categories,bank,incoming.months,current);
        months.unite(incoming.months);
        months.removeIf([&](const QDate& month) { return month < current || month > current.addMonths(1); });
        selectionRequired.removeIf([&](const QDate& month) { return incoming.months.contains(month) || !months.contains(month); });
        selectionRequired.unite(incoming.selectionRequired);
        selectionRequired.removeIf([&](const QDate& month) { return !months.contains(month); });
        upperBounds.removeIf([&](const QString& key) {
            const auto month = QDate::fromString(key.left(10),Qt::ISODate);
            return incoming.months.contains(month) || !months.contains(month);
        });
        upperBounds.unite(incoming.upperBounds);
        upperBounds.removeIf([&](const QString& key) { return !months.contains(QDate::fromString(key.left(10),Qt::ISODate)); });
        for (const auto& month : incoming.months) if (months.contains(month)) observedAt.insert(month,refreshedAt);
        for (auto it = observedAt.begin(); it != observedAt.end();) {
            if (!months.contains(it.key())) it = observedAt.erase(it);
            else ++it;
        }
    }

    [[nodiscard]] QVariantList rows(const QString& bank, const QDate& month) const {
        auto result = categories.rows(bank, month);
        for (auto& value : result) {
            auto row = value.toMap();
            if (upperBounds.contains(month.toString(Qt::ISODate) + QLatin1Char('/') + row.value(QStringLiteral("name")).toString())) {
                row[QStringLiteral("percent")] = QStringLiteral("до ") + row.value(QStringLiteral("percent")).toString();
                value = row;
            }
        }
        return result;
    }
};

// Only explicitly covered months replace a manual fallback. An absent month is
// unknown; an explicitly covered empty month has no selected categories.
[[nodiscard]] std::expected<CashbackSnapshot, QString> parse_cashback_projection(
    const QString& bank, const QByteArray& body, const QDate& today) {
    const auto invalid = [] { return std::unexpected(QStringLiteral(
        "Банк передал категории кэшбэка в неизвестном формате. Предыдущие данные сохранены.")); };
    if (!CashbackBook::validBank(bank) || !today.isValid() || body.isEmpty() || body.size() > 128 * 1024)
        return invalid();
    const auto document = QJsonDocument::fromJson(body);
    const auto root = document.object();
    if (!document.isObject() || root.value(QStringLiteral("version")) != 1 ||
        root.value(QStringLiteral("stage")) != QStringLiteral("ready") ||
        !root.value(QStringLiteral("months")).isArray()) return invalid();
    const auto months = root.value(QStringLiteral("months")).toArray();
    if (months.isEmpty() || months.size() > 2) return invalid();
    CashbackSnapshot snapshot;
    const auto current = CashbackBook::monthStart(today);
    for (const auto& value : months) {
        if (!value.isObject()) return invalid();
        const auto object = value.toObject();
        const auto monthText = object.value(QStringLiteral("month")).toString();
        const auto month = QDate::fromString(monthText, QStringLiteral("yyyy-MM-dd"));
        if ((month != current && month != current.addMonths(1)) ||
            month.toString(QStringLiteral("yyyy-MM-dd")) != monthText || snapshot.months.contains(month) ||
            !object.value(QStringLiteral("categories")).isArray()) return invalid();
        const auto categories = object.value(QStringLiteral("categories")).toArray();
        if (categories.size() > CashbackBook::maximumCategories) return invalid();
        const auto selectionRequired = object.value(QStringLiteral("selectionRequired"));
        if (!selectionRequired.isBool() || (selectionRequired.toBool() && !categories.isEmpty())) return invalid();
        if (selectionRequired.toBool()) snapshot.selectionRequired.insert(month);
        QSet<QString> names;
        for (const auto& category : categories) {
            if (!category.isObject()) return invalid();
            const auto row = category.toObject();
            const auto name = row.value(QStringLiteral("name")).toString();
            const auto rate = row.value(QStringLiteral("percent"));
            const auto upperBound = row.value(QStringLiteral("upperBound"));
            if (!upperBound.isBool() || name != name.trimmed() || !rate.isString() || names.contains(name.toCaseFolded()) ||
                !snapshot.categories.set(bank, monthText, name, rate.toString(), today)) return invalid();
            if (upperBound.toBool()) snapshot.upperBounds.insert(monthText + QLatin1Char('/') + name);
            names.insert(name.toCaseFolded());
        }
        snapshot.months.insert(month);
    }
    return snapshot;
}

// Saved months are validated relative to their own first month so a restart
// after month rollover can still restore the original observation unchanged.
[[nodiscard]] std::expected<CashbackSnapshot, QString> parse_cashback_snapshot(
    const QString& bank, const QByteArray& body) {
    const auto invalid = [] { return std::unexpected(QStringLiteral(
        "Не удалось прочитать сохранённый кэшбэк банка. Данные не изменены.")); };
    if (!CashbackBook::validBank(bank) || body.isEmpty() || body.size() > 128 * 1024)
        return invalid();
    const auto document = QJsonDocument::fromJson(body);
    const auto root = document.object();
    if (!document.isObject() || root.size() != 3 || root.value(QStringLiteral("version")) != 1 ||
        root.value(QStringLiteral("bank")) != bank || !root.value(QStringLiteral("months")).isArray())
        return invalid();
    const auto months = root.value(QStringLiteral("months")).toArray();
    if (months.isEmpty() || months.size() > 2) return invalid();
    QDate first;
    QMap<QDate,QDateTime> observed;
    QJsonArray projected;
    for (const auto& value : months) {
        if (!value.isObject()) return invalid();
        auto object = value.toObject();
        if (object.size() != 4 || !object.value(QStringLiteral("observedAt")).isString()) return invalid();
        const auto month = QDate::fromString(object.value(QStringLiteral("month")).toString(), Qt::ISODate);
        const auto timestamp = object.take(QStringLiteral("observedAt")).toString();
        const auto at = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
        if (!month.isValid() || !at.isValid() ||
            at.toUTC().toString(Qt::ISODateWithMs) != timestamp || observed.contains(month)) return invalid();
        if (!first.isValid() || month < first) first = month;
        observed.insert(month, at);
        const auto categories = object.value(QStringLiteral("categories"));
        if (!categories.isArray()) return invalid();
        for (const auto& category : categories.toArray()) {
            if (!category.isObject() || category.toObject().size() != 3) return invalid();
        }
        projected.append(object);
    }
    const auto projection = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("stage"), QStringLiteral("ready")}, {QStringLiteral("months"), projected}})
        .toJson(QJsonDocument::Compact);
    auto snapshot = parse_cashback_projection(bank, projection, first);
    if (!snapshot) return invalid();
    snapshot->observedAt = std::move(observed);
    return snapshot;
}

[[nodiscard]] std::expected<QByteArray, QString> serialize_cashback_snapshot(
    const QString& bank, const CashbackSnapshot& snapshot) {
    const auto invalid = [] { return std::unexpected(QStringLiteral(
        "Не удалось сохранить кэшбэк банка: неполные данные.")); };
    if (!CashbackBook::validBank(bank) || snapshot.months.isEmpty() || snapshot.months.size() > 2 ||
        snapshot.observedAt.size() != snapshot.months.size()) return invalid();
    for (const auto& month : snapshot.selectionRequired)
        if (!snapshot.months.contains(month)) return invalid();
    QJsonArray months;
    QSet<QString> upperBounds;
    qsizetype categoryCount = 0;
    // QMap iteration gives deterministic month order without depending on QSet.
    for (auto it = snapshot.observedAt.cbegin(); it != snapshot.observedAt.cend(); ++it) {
        if (!snapshot.months.contains(it.key()) || !it.value().isValid()) return invalid();
        const auto month = it.key().toString(Qt::ISODate);
        QJsonArray categories;
        for (const auto& value : snapshot.categories.rows(bank, it.key())) {
            const auto row = value.toMap();
            const auto name = row.value(QStringLiteral("name")).toString();
            auto percent = row.value(QStringLiteral("percent")).toString();
            percent.chop(2); // CashbackBook rows end in " %".
            const auto key = month + QLatin1Char('/') + name;
            const bool upper = snapshot.upperBounds.contains(key);
            if (upper) upperBounds.insert(key);
            categories.append(QJsonObject{{QStringLiteral("name"), name},
                {QStringLiteral("percent"), percent}, {QStringLiteral("upperBound"), upper}});
            ++categoryCount;
        }
        months.append(QJsonObject{{QStringLiteral("month"), month},
            {QStringLiteral("selectionRequired"), snapshot.selectionRequired.contains(it.key())},
            {QStringLiteral("categories"), categories},
            {QStringLiteral("observedAt"), it.value().toUTC().toString(Qt::ISODateWithMs)}});
    }
    const auto allCategories = QJsonDocument::fromJson(snapshot.categories.serialize()).object()
        .value(QStringLiteral("categories")).toArray();
    if (allCategories.size() != categoryCount || upperBounds != snapshot.upperBounds) return invalid();
    auto body = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("bank"), bank}, {QStringLiteral("months"), months}}).toJson(QJsonDocument::Compact);
    if (!parse_cashback_snapshot(bank, body)) return invalid();
    return body;
}

// VTB's own category module (mf_1757_categories 4.12.18) uses this exact GET.
// The separately offered /bonus/categories list is deliberately not read.
[[nodiscard]] QString vtb_cashback_start_script(const QDate& today, const QString& runId) {
    static const QString script = QStringLiteral(R"JS((() => {
  const url = new URL(globalThis.location.href)
  if (url.origin !== 'https://online.vtb.ru' || url.username || url.password) return 'wrong-origin'
  const key = '__wealthtrackerCashback'
  if (globalThis[key]?.controller) globalThis[key].controller.abort()
  const state = globalThis[key] = {id:'%5', status:'loading', active:true, controller:new AbortController()}
  const current = '%1'
  const next = '%2'
  const monthNames = ['январь','февраль','март','апрель','май','июнь','июль','август','сентябрь','октябрь','ноябрь','декабрь']
  const expected = [monthNames[%3], monthNames[%4]]
  const fail = () => { if (state.active) state.status = 'failed' }
  Promise.resolve().then(() => {
    const container = globalThis.mf_1488_core
    if (!container || typeof container.get !== 'function') throw new Error()
    return container.get('./HttpClient')
  }).then(factory => {
    if (!state.active) throw new Error()
    const client = typeof factory === 'function' ? factory()?.http : null
    if (!client || typeof client.get !== 'function') throw new Error()
    return client.get('/msa/api-gw/private/loya/loya-web/v2/bonus/month/categories', {
      headers:{Accept:'application/json','X-Source':'VTB_ONLINE'}, signal:state.controller.signal
    })
  }).then(response => {
    if (!state.active) return
    const serverTime = Date.parse(response?.headers?.date)
    if (!Number.isFinite(serverTime) ||
        new Date(serverTime + 3 * 60 * 60 * 1000).toISOString().slice(0,7) + '-01' !== current) throw new Error()
    const data = response?.data
    if (!data || typeof data !== 'object' ||
        new TextEncoder().encode(JSON.stringify(data)).byteLength > 512 * 1024 ||
        !Array.isArray(data.categories) || data.categories.length !== 3) throw new Error()
    const months = [1,2].map((index, position) => {
      const month = data.categories[index]
      if (!month || typeof month.month !== 'string' || month.month.trim().toLowerCase() !== expected[position] ||
          typeof month.choice !== 'boolean' || !Array.isArray(month.categories) || month.categories.length > 32) throw new Error()
      const categories = month.choice ? [] : month.categories.map(category => {
        if (!category || typeof category.displayName !== 'string' ||
            typeof category.displayPercent !== 'string') throw new Error()
        const name = category.displayName.trim()
        const rate = category.displayPercent.trim().match(/^(до\s+)?([0-9]{1,3}(?:[.,][0-9]{1,2})?)\s*%$/i)
        if (!name || name.length > 100 || /[\u0000-\u001f\u007f-\u009f]/.test(name) || !rate) throw new Error()
        return {name:name,percent:rate[2],upperBound:Boolean(rate[1])}
      })
      return {month:position === 0 ? current : next, selectionRequired:month.choice,categories:categories}
    })
    state.result = {version:1,stage:'ready',months:months}
    state.status = 'ready'
  }).catch(fail)
  return state.status
})())JS");
    const auto current = CashbackBook::monthStart(today);
    return script.arg(current.toString(QStringLiteral("yyyy-MM-dd")),
        current.addMonths(1).toString(QStringLiteral("yyyy-MM-dd")))
        .arg(current.month() - 1).arg(current.addMonths(1).month() - 1).arg(runId);
}

[[nodiscard]] QString cashback_projection_script(const QString& bank,const QString& runId) {
    static const QString script = QStringLiteral(R"JS((() => {
  const url = new URL(globalThis.location.href)
  if (url.origin !== '%1' || url.username || url.password) return ''
  const state = globalThis.__wealthtrackerCashback
  if (!state || !state.active || state.id !== '%2') return ''
  if (state.status === 'failed' && state.reasonCode === 'OZON-STARS') return 'ozon-stars'
  if ('%1' === 'https://finance.ozon.ru' && state.status === 'failed') {
    const reasons = ['OZON-MODULE','OZON-READ','OZON-SCHEMA','OZON-LIMIT','OZON-PROGRAM','OZON-CONTEXT']
    const phases = ['IMPORT','CONTEXT','MODE-CONTRACT','MODE-CALL','MODE-VALUE','CASHBACK-CREATE',
      'CASHBACK-CALL','CASHBACK-VALUE','MONTHS','READY']
    if (reasons.includes(state.reasonCode) && phases.includes(state.phase))
      return 'ozon-failed:' + state.reasonCode + ':' + state.phase
  }
  return state.status === 'ready' ? JSON.stringify(state.result) : state.status
})())JS");
    const auto origin = bank == QStringLiteral("alfa") ? QStringLiteral("https://web.alfabank.ru")
        : bank == QStringLiteral("ozon") ? QStringLiteral("https://finance.ozon.ru") : QStringLiteral("https://online.vtb.ru");
    return script.arg(origin,runId);
}

[[nodiscard]] QString vtb_cashback_projection_script(const QString& runId) {
    return cashback_projection_script(QStringLiteral("vtb"),runId);
}

class CashbackSync final : public QObject {
public:
    using Completed = std::function<void(CashbackSnapshot)>;
    using Failed = std::function<void(QString)>;
    CashbackSync(Completed completed, Failed failed, QObject* parent)
        : QObject(parent), completed_(std::move(completed)), failed_(std::move(failed)) {
        poll_.setInterval(250);
        connect(&poll_, &QTimer::timeout, this, [this] { probe(); });
        deadline_.setSingleShot(true);
        connect(&deadline_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("Кэшбэк не обновился за 10 секунд. Предыдущие категории сохранены."));
        });
    }
    ~CashbackSync() override { cancel(); }

    void startVtb(QWebView* page) { begin(page,QStringLiteral("vtb")); }
    void startAlfa(QWebView* page) { begin(page,QStringLiteral("alfa")); }
    void startOzon(QWebView* page) { begin(page,QStringLiteral("ozon")); }

private:
    void begin(QWebView* page,const QString& bank) {
        if (started_ || !page) return;
        started_ = true;
        page_ = page;
        bank_ = bank;
        origin_ = bank == QStringLiteral("alfa") ? QStringLiteral("https://web.alfabank.ru")
            : bank == QStringLiteral("ozon") ? QStringLiteral("https://finance.ozon.ru") : QStringLiteral("https://online.vtb.ru");
        today_ = QDate::currentDate();
        run_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        active_ = true;
        deadline_.start(10'000);
        pending_ = true;
        const QPointer<CashbackSync> guard(this);
        const auto script = bank == QStringLiteral("alfa") ? alfa_cashback_start_script(today_,run_id_)
            : bank == QStringLiteral("ozon") ? ozon_cashback_start_script(today_,run_id_) : vtb_cashback_start_script(today_,run_id_);
        runBankJavaScript(page_, this, script, [guard](const QVariant& value) {
            if (!guard || !guard->active_) return;
            guard->pending_ = false;
            if (value.toString() == QStringLiteral("wrong-origin")) {
                guard->fail(QStringLiteral("Для чтения кэшбэка требуется официальная страница банка."));
                return;
            }
            guard->poll_.start();
            guard->probe();
        });
    }

public:
    void cancel() {
        if (!active_) return;
        active_ = false;
        poll_.stop();
        deadline_.stop();
        if (page_) page_->runJavaScript(QStringLiteral(R"JS((() => {
          if (globalThis.location.origin !== '%1') return
          const state = globalThis.__wealthtrackerCashback
          if (state?.id === '%2') { state.active = false; state.controller?.abort(); delete state.result }
        })())JS").arg(origin_,run_id_));
    }

private:
    void fail(QString error) {
        if (!active_) return;
        cancel();
        if (failed_) failed_(std::move(error));
    }
    void probe() {
        if (!active_ || pending_) return;
        if (!page_) { fail(QStringLiteral("Страница банка закрыта. Категории сохранены.")); return; }
        pending_ = true;
        const QPointer<CashbackSync> guard(this);
        runBankJavaScript(page_, this, cashback_projection_script(bank_,run_id_), [guard](const QVariant& value) {
            if (!guard || !guard->active_) return;
            guard->pending_ = false;
            const auto result = value.toString();
            if (result == QStringLiteral("loading")) return;
            if (guard->bank_ == QStringLiteral("ozon") && result == QStringLiteral("ozon-stars")) {
                guard->fail(QStringLiteral("В Ozon Банке выбран режим «Звёзды». Предыдущие категории кэшбэка сохранены."));
                return;
            }
            if (guard->bank_ == QStringLiteral("ozon") && result.startsWith(QStringLiteral("ozon-failed:"))) {
                const auto fields = result.split(QLatin1Char(':'));
                const QSet<QString> reasons{QStringLiteral("OZON-MODULE"), QStringLiteral("OZON-READ"),
                    QStringLiteral("OZON-SCHEMA"), QStringLiteral("OZON-LIMIT"), QStringLiteral("OZON-PROGRAM"),
                    QStringLiteral("OZON-CONTEXT")};
                const QSet<QString> phases{QStringLiteral("IMPORT"), QStringLiteral("CONTEXT"), QStringLiteral("MODE-CONTRACT"),
                    QStringLiteral("MODE-CALL"), QStringLiteral("MODE-VALUE"), QStringLiteral("CASHBACK-CREATE"),
                    QStringLiteral("CASHBACK-CALL"), QStringLiteral("CASHBACK-VALUE"), QStringLiteral("MONTHS"), QStringLiteral("READY")};
                if (fields.size() == 3 && reasons.contains(fields[1]) && phases.contains(fields[2])) {
                    guard->fail(QStringLiteral("Кэшбэк Ozon Банка не обновлён (%1, %2). Предыдущие категории сохранены.")
                        .arg(fields[1], fields[2]));
                    return;
                }
            }
            if (result == QStringLiteral("failed")) {
                guard->fail(QStringLiteral("Не удалось получить кэшбэк банка. Предыдущие категории сохранены."));
                return;
            }
            auto parsed = parse_cashback_projection(guard->bank_, result.toUtf8(), guard->today_);
            if (!parsed) { guard->fail(parsed.error()); return; }
            auto snapshot = std::move(*parsed);
            guard->cancel();
            if (guard->completed_) guard->completed_(std::move(snapshot));
        });
    }
    Completed completed_;
    Failed failed_;
    QTimer poll_, deadline_;
    QPointer<QWebView> page_;
    QDate today_;
    QString run_id_;
    QString bank_, origin_;
    bool started_ = false;
    bool active_ = false;
    bool pending_ = false;
};

} // namespace wealthtracker::app
