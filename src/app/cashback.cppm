module;

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <expected>

export module wt.cashback;

export namespace wealthtracker::app {

struct CashbackCategory {
    QString bank;
    QDate month;
    QString name;
    int basisPoints = 0;
};

class CashbackBook {
public:
    static constexpr int maximumCategories = 32;

    [[nodiscard]] static bool validBank(const QString& bank) {
        return bank == QStringLiteral("tbank") || bank == QStringLiteral("alfa") ||
               bank == QStringLiteral("vtb");
    }

    [[nodiscard]] static QDate monthStart(const QDate& date) {
        return QDate(date.year(), date.month(), 1);
    }

    [[nodiscard]] static std::expected<CashbackBook, QString> parse(const QByteArray& bytes) {
        CashbackBook book;
        if (bytes.isEmpty()) return book;
        const auto invalid = [] { return std::unexpected(QStringLiteral(
            "Не удалось прочитать сохранённые категории кэшбэка. Данные не изменены.")); };
        if (bytes.size() > 128 * 1024) return invalid();
        const auto document = QJsonDocument::fromJson(bytes);
        const auto root = document.object();
        if (!document.isObject() || root.value(QStringLiteral("version")) != 1 ||
            !root.value(QStringLiteral("categories")).isArray()) return invalid();
        const auto rows = root.value(QStringLiteral("categories")).toArray();
        if (rows.size() > 3 * 2 * maximumCategories) return invalid();
        QSet<QString> keys;
        for (const auto& row : rows) {
            if (!row.isObject()) return invalid();
            const auto object = row.toObject();
            const auto bank = object.value(QStringLiteral("bank")).toString();
            const auto monthText = object.value(QStringLiteral("month")).toString();
            const auto month = QDate::fromString(monthText, QStringLiteral("yyyy-MM-dd"));
            const auto name = object.value(QStringLiteral("name")).toString();
            const auto rate = object.value(QStringLiteral("basisPoints"));
            const auto key = bank + QLatin1Char('/') + monthText + QLatin1Char('/') + name.toCaseFolded();
            if (!validBank(bank) || !month.isValid() || month.day() != 1 ||
                month.toString(QStringLiteral("yyyy-MM-dd")) != monthText || !validName(name) ||
                !rate.isDouble() || rate.toInt(-1) < 1 || rate.toInt(-1) > 10'000 ||
                rate != rate.toInt(-1) || keys.contains(key)) return invalid();
            const auto count = std::count_if(book.categories_.cbegin(), book.categories_.cend(),
                [&](const auto& item) { return item.bank == bank && item.month == month; });
            if (count >= maximumCategories) return invalid();
            keys.insert(key);
            book.categories_.push_back({bank, month, name, rate.toInt()});
        }
        return book;
    }

    [[nodiscard]] QByteArray serialize() const {
        QJsonArray rows;
        for (const auto& item : categories_) {
            rows.append(QJsonObject{{QStringLiteral("bank"), item.bank},
                {QStringLiteral("month"), item.month.toString(QStringLiteral("yyyy-MM-dd"))},
                {QStringLiteral("name"), item.name}, {QStringLiteral("basisPoints"), item.basisPoints}});
        }
        return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
            {QStringLiteral("categories"), rows}}).toJson(QJsonDocument::Compact);
    }

    [[nodiscard]] std::expected<void, QString> set(const QString& bank, const QString& monthText,
        const QString& category, const QString& percent, const QDate& today) {
        const auto month = QDate::fromString(monthText, QStringLiteral("yyyy-MM-dd"));
        const auto current = monthStart(today);
        const auto name = category.trimmed();
        static const QRegularExpression ratePattern(QStringLiteral("^([0-9]{1,3})(?:[.,]([0-9]{1,2}))?$"));
        const auto match = ratePattern.match(percent.trimmed());
        if (!validBank(bank) || !today.isValid() || monthText != month.toString(QStringLiteral("yyyy-MM-dd")) ||
            (month != current && month != current.addMonths(1)) || !validName(name) || !match.hasMatch()) {
            return std::unexpected(QStringLiteral("Укажите банк, месяц, категорию и процент от 0,01 до 100."));
        }
        const int rate = match.captured(1).toInt() * 100 + match.captured(2).leftJustified(2, QLatin1Char('0')).toInt();
        if (rate < 1 || rate > 10'000) return std::unexpected(QStringLiteral("Процент должен быть от 0,01 до 100."));
        auto found = std::find_if(categories_.begin(), categories_.end(), [&](const auto& item) {
            return item.bank == bank && item.month == month && item.name.compare(name, Qt::CaseInsensitive) == 0;
        });
        if (found != categories_.end()) {
            found->name = name;
            found->basisPoints = rate;
        } else {
            const auto count = std::count_if(categories_.cbegin(), categories_.cend(),
                [&](const auto& item) { return item.bank == bank && item.month == month; });
            if (count >= maximumCategories) return std::unexpected(QStringLiteral("Не более 32 категорий на банк и месяц."));
            categories_.push_back({bank, month, name, rate});
        }
        categories_.removeIf([&](const auto& item) { return item.month < current || item.month > current.addMonths(1); });
        return {};
    }

    bool remove(const QString& bank, const QString& month, const QString& category) {
        return categories_.removeIf([&](const auto& item) {
            return item.bank == bank && item.month.toString(QStringLiteral("yyyy-MM-dd")) == month && item.name == category;
        }) > 0;
    }

    [[nodiscard]] QVariantList rows(const QString& bank, const QDate& month) const {
        QVariantList result;
        for (const auto& item : categories_) {
            if (item.bank != bank || item.month != month) continue;
            auto rate = QStringLiteral("%1.%2").arg(item.basisPoints / 100)
                .arg(item.basisPoints % 100, 2, 10, QLatin1Char('0'));
            while (rate.endsWith(QLatin1Char('0'))) rate.chop(1);
            if (rate.endsWith(QLatin1Char('.'))) rate.chop(1);
            result.push_back(QVariantMap{{QStringLiteral("name"), item.name},
                {QStringLiteral("percent"), rate.replace(QLatin1Char('.'), QLatin1Char(',')) + QStringLiteral(" %")}});
        }
        return result;
    }

private:
    static bool validName(const QString& name) {
        return !name.isEmpty() && name.size() <= 100 && name == name.trimmed() &&
            std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
    }
    QList<CashbackCategory> categories_;
};

} // namespace wealthtracker::app
