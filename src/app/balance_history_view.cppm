module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

}

export module wt.balance_history_view;

import wt.core.money;
import wt.storage.balance_history;

namespace wealthtracker::app {
namespace {

using BigAmount = boost::multiprecision::cpp_int;

QString bigAmountLabel(const BigAmount& amount, const QString& currency) {
    const BigAmount magnitude = amount < 0 ? -amount : amount;
    const BigAmount whole = magnitude / 100;
    const BigAmount fraction = magnitude % 100;
    return QStringLiteral("%1%2.%3 %4").arg(amount < 0 ? QStringLiteral("−") : QString{})
        .arg(QString::fromStdString(whole.str())).arg(fraction.convert_to<unsigned>(), 2, 10, QLatin1Char('0')).arg(currency);
}

QString accountKey(const storage::BalanceCapture& capture, const storage::HistoryBalance& balance) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{
        QString::fromStdString(capture.connection_id), QString::fromStdString(balance.account_id),
        QString::fromStdString(balance.currency)}).toJson(QJsonDocument::Compact));
}

QString amountLabel(const std::int64_t amount, const QString& currency) {
    // Avoid negating INT64_MIN and never convert monetary values to floating point.
    const auto magnitude = amount < 0 ? std::uint64_t(-(amount + 1)) + 1 : std::uint64_t(amount);
    return QStringLiteral("%1%2.%3 %4")
        .arg(amount < 0 ? QStringLiteral("−") : QString{})
        .arg(magnitude / 100).arg(magnitude % 100, 2, 10, QLatin1Char('0')).arg(currency);
}

bool validBalance(const storage::HistoryBalance& balance) {
    const auto currency = core::Currency::parse(balance.currency);
    return !balance.account_id.empty() && (balance.currency == "USDT" || (currency && currency->code() == balance.currency));
}

struct AccountAmount {
    QString label;
    BigAmount amount;
};

struct Sample {
    qint64 time;
    std::int64_t amount;
    int accountCount;
    qint64 observedTime;
    bool baseline = false;
    QMap<QString, AccountAmount> accounts;
    bool carried = false;
};

}  // namespace
}  // namespace wealthtracker::app

export namespace wealthtracker::app {

struct BalanceHistoryFilter {
    bool restrictConnections = false;
    QStringList connectionIds;
    QDate from;
    QDate to;
    bool daily = false;
};

[[nodiscard]] QVariantList balance_history_connections(const std::vector<storage::BalanceCapture>& captures) {
    QMap<QString, QVariant> connections;
    for (const auto& capture : captures) {
        const auto id = QString::fromStdString(capture.connection_id);
        connections[id] = QVariantMap{{QStringLiteral("id"), id},
            {QStringLiteral("label"), QString::fromStdString(capture.institution)}};
    }
    return connections.values();
}

[[nodiscard]] QVariantList balance_history_accounts(const std::vector<storage::BalanceCapture>& captures) {
    QMap<QString, QVariant> accounts;
    for (const auto& capture : captures) {
        for (const auto& balance : capture.balances) {
            if (capture.connection_id.empty() || !validBalance(balance)) continue;
            const auto key = accountKey(capture, balance);
            accounts[key] = QVariantMap{{QStringLiteral("id"), key},
                {QStringLiteral("connectionId"), QString::fromStdString(capture.connection_id)},
                {QStringLiteral("label"), QString::fromStdString(balance.name) + QStringLiteral(" · ") +
                    QString::fromStdString(capture.institution)},
                {QStringLiteral("currency"), QString::fromStdString(balance.currency)}};
        }
    }
    return accounts.values();
}

[[nodiscard]] QStringList balance_history_currencies(const std::vector<storage::BalanceCapture>& captures) {
    QSet<QString> currencies;
    for (const auto& capture : captures)
        for (const auto& balance : capture.balances)
            if (validBalance(balance)) currencies.insert(QString::fromStdString(balance.currency));
    auto result = currencies.values();
    result.sort();
    return result;
}

[[nodiscard]] QVariantMap balance_history_chart(const std::vector<storage::BalanceCapture>& captures,
                                                const QString& accountId, const QString& currency,
                                                const BalanceHistoryFilter& filter = {}) {
    QVariantMap result{{QStringLiteral("points"), QVariantList{}},
        {QStringLiteral("minimumLabel"), QString{}}, {QStringLiteral("maximumLabel"), QString{}},
        {QStringLiteral("firstDate"), QString{}}, {QStringLiteral("lastDate"), QString{}},
        {QStringLiteral("lastValue"), QString{}}, {QStringLiteral("observationCount"), 0},
        {QStringLiteral("accountCount"), 0}, {QStringLiteral("connectionCount"), 0}, {QStringLiteral("currency"), currency},
        {QStringLiteral("compositionZeroY"), 0.0}, {QStringLiteral("shareBasis"), QStringLiteral("total")},
        {QStringLiteral("openingValue"), QString{}}, {QStringLiteral("changeValue"), QString{}},
        {QStringLiteral("changePercent"), QString{}}, {QStringLiteral("baselineCarried"), false},
        {QStringLiteral("openingObservedAt"), QString{}},
        {QStringLiteral("lastObservedAt"), QString{}},
        {QStringLiteral("error"), QString{}}, {QStringLiteral("series"), QVariantList{}},
        {QStringLiteral("compositionAvailable"), false}, {QStringLiteral("compositionNote"), QString{}},
        {QStringLiteral("compositionMinimumLabel"), QString{}},
        {QStringLiteral("compositionMaximumLabel"), QString{}}};
    const auto failure = [&](const QString& message) {
        auto failed = result;
        failed[QStringLiteral("error")] = message;
        return failed;
    };
    if (filter.from.isValid() && filter.to.isValid() && filter.from > filter.to)
        return failure(QStringLiteral("Начало периода должно быть не позже окончания."));
    const bool daily = filter.daily && accountId.isEmpty();
    const auto finalDay = daily && (!filter.to.isValid() || filter.to > QDate::currentDate())
        ? QDate::currentDate() : filter.to;
    const auto rangeStart = filter.from.isValid()
        ? filter.from.startOfDay().toMSecsSinceEpoch() : std::numeric_limits<qint64>::min();
    const auto rangeEnd = finalDay.isValid()
        ? finalDay.addDays(1).startOfDay().toMSecsSinceEpoch() : std::numeric_limits<qint64>::max();
    if (captures.empty()) return result;
    struct Event { const storage::BalanceCapture* capture; qint64 time; };
    std::vector<Event> events;
    for (const auto& capture : captures) {
        if (filter.restrictConnections && !filter.connectionIds.contains(QString::fromStdString(capture.connection_id)))
            continue;
        const auto date = QDateTime::fromString(QString::fromStdString(capture.captured_at), Qt::ISODateWithMs);
        if (capture.connection_id.empty() || !date.isValid())
            return failure(QStringLiteral("Некорректная запись истории остатков."));
        if (date.toMSecsSinceEpoch() >= rangeEnd) continue;
        QSet<QString> keys;
        for (const auto& balance : capture.balances) {
            const auto key = accountKey(capture, balance);
            if (!validBalance(balance) || keys.contains(key))
                return failure(QStringLiteral("Некорректная запись истории остатков."));
            keys.insert(key);
        }
        events.push_back({&capture, date.toMSecsSinceEpoch()});
    }
    const auto currencies = balance_history_currencies(captures);
    if (currencies.isEmpty()) return result;
    if (!currencies.contains(currency))
        return failure(QStringLiteral("Нет истории в выбранной валюте."));
    std::stable_sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.time != right.time ? left.time < right.time : left.capture->id < right.capture->id;
    });
    QMap<QString, const storage::BalanceCapture*> latest;
    std::vector<Sample> samples;
    bool observed = false;
    int realObservations = 0;
    for (const auto& event : events) {
        const auto& capture = *event.capture;
        latest[QString::fromStdString(capture.connection_id)] = &capture;
        std::int64_t amount = 0;
        int count = 0;
        QMap<QString, AccountAmount> accounts;
        if (accountId.isEmpty()) {
            BigAmount total = 0;
            for (const auto* current : latest) {
                for (const auto& balance : current->balances) {
                    if (QString::fromStdString(balance.currency) != currency) continue;
                    const auto value = balance.amount_minor;
                    total += value;
                    ++count;
                    auto& connection = accounts[QString::fromStdString(current->connection_id)];
                    connection.label = QString::fromStdString(current->institution);
                    connection.amount += value;
                }
            }
            if (total > std::numeric_limits<std::int64_t>::max() || total < std::numeric_limits<std::int64_t>::min())
                return failure(QStringLiteral("Сумма остатков превышает допустимый диапазон."));
            amount = total.convert_to<std::int64_t>();
            observed = observed || count > 0;
            if (!observed) continue;
        } else {
            for (const auto& balance : capture.balances) {
                if (accountKey(capture, balance) == accountId && QString::fromStdString(balance.currency) == currency) {
                    amount = balance.amount_minor;
                    count = 1;
                    accounts[accountId] = {QString::fromStdString(balance.name) + QStringLiteral(" · ") +
                        QString::fromStdString(capture.institution), amount};
                    break;
                }
            }
            if (count == 0) continue;
        }
        if (event.time >= rangeStart) ++realObservations;
        Sample sample{event.time, amount, count, event.time, false, std::move(accounts)};
        if (!samples.empty() && samples.back().time == event.time) samples.back() = std::move(sample);
        else samples.push_back(std::move(sample));
    }
    std::vector<Sample> visible;
    for (const auto& sample : samples) {
        if (sample.time >= rangeEnd) break;
        if (sample.time < rangeStart) {
            auto baseline = sample;
            baseline.time = rangeStart;
            baseline.baseline = true;
            visible = {std::move(baseline)};
        } else {
            if (sample.time == rangeStart) visible.clear();
            visible.push_back(sample);
        }
    }
    samples = std::move(visible);
    if (samples.empty()) return result;
    const bool openingUnknown = filter.from.isValid() && samples.front().time > rangeStart;
    if (daily) {
        const auto firstDay = QDateTime::fromMSecsSinceEpoch(samples.front().time).date();
        if (firstDay > finalDay) return result;
        if (firstDay.daysTo(finalDay) >= 36600)
            return failure(QStringLiteral("Выберите период не более 36 600 дней."));
        std::vector<Sample> days;
        std::size_t index = 0;
        for (auto day = firstDay; day <= finalDay; day = day.addDays(1)) {
            const auto nextDay = day.addDays(1).startOfDay().toMSecsSinceEpoch();
            while (index + 1 < samples.size() && samples[index + 1].time < nextDay) ++index;
            auto sample = samples[index];
            sample.time = day.startOfDay().toMSecsSinceEpoch();
            sample.carried = QDateTime::fromMSecsSinceEpoch(sample.observedTime).date() < day;
            sample.baseline = days.empty() && sample.baseline;
            days.push_back(std::move(sample));
        }
        samples = std::move(days);
    }
    const auto [minimum, maximum] = std::minmax_element(samples.begin(), samples.end(),
        [](const Sample& left, const Sample& right) { return left.amount < right.amount; });
    const auto start = samples.front().time;
    const auto end = samples.back().time;
    QVariantList points;
    for (const auto& sample : samples) {
        const auto x = end == start ? 0.5 :
            static_cast<double>(std::uint64_t(sample.time) - std::uint64_t(start)) /
            static_cast<double>(std::uint64_t(end) - std::uint64_t(start));
        const auto y = maximum->amount == minimum->amount ? 0.5 :
            static_cast<double>(std::uint64_t(sample.amount) - std::uint64_t(minimum->amount)) /
            static_cast<double>(std::uint64_t(maximum->amount) - std::uint64_t(minimum->amount));
        points.append(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("label"), amountLabel(sample.amount, currency)},
            {QStringLiteral("baseline"), sample.baseline},
            {QStringLiteral("carried"), sample.carried},
            {QStringLiteral("dateLabel"), QDateTime::fromMSecsSinceEpoch(sample.time).toString(QStringLiteral("dd.MM.yyyy"))},
            {QStringLiteral("timestamp"), QDateTime::fromMSecsSinceEpoch(sample.observedTime).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))}});
    }
    result[QStringLiteral("points")] = points;
    QMap<QString, QString> labels;
    BigAmount axisMinimum = 0;
    BigAmount axisMaximum = 0;
    for (const auto& sample : samples) {
        BigAmount positive = 0;
        BigAmount negative = 0;
        for (auto account = sample.accounts.cbegin(); account != sample.accounts.cend(); ++account) {
            labels[account.key()] = account->label;
            if (account->amount < 0) negative += account->amount;
            else positive += account->amount;
        }
        if (negative < axisMinimum) axisMinimum = negative;
        if (positive > axisMaximum) axisMaximum = positive;
    }
    const bool negativeBalance = axisMinimum < 0;
    const BigAmount axisSpan = axisMaximum - axisMinimum;
    const auto coordinate = [&](const BigAmount& value) {
        const BigAmount offset = value - axisMinimum;
        return axisSpan == 0 ? 0.0 : offset.convert_to<double>() / axisSpan.convert_to<double>();
    };
    result[QStringLiteral("compositionZeroY")] = coordinate(BigAmount{0});
    result[QStringLiteral("shareBasis")] = negativeBalance ? QStringLiteral("absolute") : QStringLiteral("total");
    if (negativeBalance) result[QStringLiteral("compositionNote")] = QStringLiteral(
        "Положительные остатки показаны выше нуля, отрицательные — ниже. Доли рассчитаны по сумме модулей остатков подключений.");
    QMap<QString, QVariantList> accountPoints;
    QSet<QString> known;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        auto point = points.at(static_cast<qsizetype>(index)).toMap();
        point[QStringLiteral("compositionY")] = coordinate(BigAmount{sample.amount});
        points[static_cast<qsizetype>(index)] = point;
        BigAmount positive = 0;
        BigAmount negative = 0;
        BigAmount gross = 0;
        for (const auto& account : sample.accounts) gross += account.amount < 0 ? -account.amount : account.amount;
        for (auto account = labels.cbegin(); account != labels.cend(); ++account) {
            const auto current = sample.accounts.constFind(account.key());
            if (current != sample.accounts.cend()) known.insert(account.key());
            if (!known.contains(account.key())) continue;
            const BigAmount value = current == sample.accounts.cend() ? BigAmount{0} : current->amount;
            const BigAmount magnitude = value < 0 ? -value : value;
            BigAmount lower;
            BigAmount upper;
            if (value < 0) {
                upper = negative;
                negative += value;
                lower = negative;
            } else {
                lower = positive;
                positive += value;
                upper = positive;
            }
            QString share = QStringLiteral("—");
            if (gross > 0) {
                const BigAmount rounded = (magnitude * 10000 + gross / 2) / gross;
                const BigAmount whole = rounded / 100;
                const BigAmount fraction = rounded % 100;
                share = QString::fromStdString(whole.str());
                if (fraction != 0) {
                    auto decimals = QStringLiteral("%1").arg(fraction.convert_to<unsigned>(), 2, 10, QLatin1Char('0'));
                    if (decimals.endsWith(QLatin1Char('0'))) decimals.chop(1);
                    share += QLatin1Char('.') + decimals;
                }
                share += QStringLiteral(" %");
            }
            accountPoints[account.key()].append(QVariantMap{
                {QStringLiteral("x"), point.value(QStringLiteral("x"))},
                {QStringLiteral("lower"), coordinate(lower)}, {QStringLiteral("upper"), coordinate(upper)},
                {QStringLiteral("valueSign"), value < 0 ? -1 : value > 0 ? 1 : 0},
                {QStringLiteral("label"), bigAmountLabel(value, currency)},
                {QStringLiteral("shareLabel"), share},
                {QStringLiteral("timestamp"), point.value(QStringLiteral("timestamp"))},
                {QStringLiteral("dateLabel"), point.value(QStringLiteral("dateLabel"))},
                {QStringLiteral("carried"), sample.carried}, {QStringLiteral("baseline"), sample.baseline}});
        }
    }
    QMap<QString, int> colorIndices;
    const auto options = accountId.isEmpty() ? balance_history_connections(captures) : balance_history_accounts(captures);
    for (const auto& option : options)
        colorIndices.insert(option.toMap().value(QStringLiteral("id")).toString(), static_cast<int>(colorIndices.size()));
    QVariantList series;
    for (auto account = labels.cbegin(); account != labels.cend(); ++account) {
        const auto& values = accountPoints[account.key()];
        series.append(QVariantMap{{QStringLiteral("id"), account.key()},
            {QStringLiteral("label"), account.value()}, {QStringLiteral("colorIndex"), colorIndices.value(account.key())},
            {QStringLiteral("points"), values},
            {QStringLiteral("lastValue"), values.back().toMap().value(QStringLiteral("label"))}});
    }
    result[QStringLiteral("series")] = series;
    result[QStringLiteral("points")] = points;
    result[QStringLiteral("compositionAvailable")] = !series.isEmpty();
    result[QStringLiteral("compositionMinimumLabel")] = bigAmountLabel(axisMinimum, currency);
    result[QStringLiteral("compositionMaximumLabel")] = bigAmountLabel(axisMaximum, currency);
    result[QStringLiteral("connectionCount")] = static_cast<int>(samples.back().accounts.size());
    result[QStringLiteral("minimumLabel")] = amountLabel(minimum->amount, currency);
    result[QStringLiteral("maximumLabel")] = amountLabel(maximum->amount, currency);
    result[QStringLiteral("firstDate")] = QDateTime::fromMSecsSinceEpoch(start).toString(QStringLiteral("dd.MM.yyyy"));
    result[QStringLiteral("lastDate")] = QDateTime::fromMSecsSinceEpoch(end).toString(QStringLiteral("dd.MM.yyyy"));
    result[QStringLiteral("lastValue")] = amountLabel(samples.back().amount, currency);
    result[QStringLiteral("observationCount")] = daily ? realObservations :
        static_cast<int>(std::count_if(samples.begin(), samples.end(),
            [](const Sample& sample) { return !sample.baseline; }));
    result[QStringLiteral("accountCount")] = samples.back().accountCount;
    result[QStringLiteral("openingValue")] = amountLabel(samples.front().amount, currency);
    result[QStringLiteral("openingObservedAt")] = QDateTime::fromMSecsSinceEpoch(samples.front().observedTime)
        .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
    result[QStringLiteral("baselineCarried")] = samples.front().baseline;
    result[QStringLiteral("openingUnknown")] = openingUnknown;
    result[QStringLiteral("lastObservedAt")] = QDateTime::fromMSecsSinceEpoch(samples.back().observedTime)
        .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
    const auto change = core::Decimal::from_integer(samples.back().amount).subtract(
        core::Decimal::from_integer(samples.front().amount));
    if (change) {
        const auto amount = change->divide(core::Decimal::from_integer(100), 2);
        if (amount) result[QStringLiteral("changeValue")] = QString::fromStdString(amount->to_string()) + " " + currency;
        if (samples.front().amount > 0) {
            const auto percentage = change->multiply(core::Decimal::from_integer(100));
            if (percentage) {
                const auto ratio = percentage->divide(core::Decimal::from_integer(samples.front().amount), 2);
                if (ratio) result[QStringLiteral("changePercent")] = QString::fromStdString(ratio->to_string()) + " %";
            }
        }
    }
    return result;
}

}  // namespace wealthtracker::app
