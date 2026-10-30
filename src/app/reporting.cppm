module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include <QDate>
#include <QLocale>
#include <QRegularExpression>
#include <QString>

#include <expected>

}

export module wt.reporting;

export namespace wealthtracker::app {

struct ReportingPeriod {
    QDate from;
    QDate to;

    [[nodiscard]] bool contains(const QDate& date) const noexcept {
        return date.isValid() && date >= from && date <= to;
    }
};

[[nodiscard]] ReportingPeriod defaultReportingPeriod(const QDate& today) {
    const auto validToday = today.isValid() ? today : QDate::currentDate();
    return {validToday.addMonths(-1).addDays(1), validToday};
}

[[nodiscard]] QString reportingInputDate(const QDate& date) {
    return date.toString(QStringLiteral("dd.MM.yyyy"));
}

[[nodiscard]] QString reportingPeriodLabel(const ReportingPeriod& period) {
    const QLocale locale(QLocale::Russian, QLocale::Russia);
    return QStringLiteral("%1 — %2").arg(locale.toString(period.from, QStringLiteral("d MMM yyyy")),
                                         locale.toString(period.to, QStringLiteral("d MMM yyyy")));
}

[[nodiscard]] std::expected<ReportingPeriod, QString> parseReportingPeriod(const QString& fromText,
                                                                           const QString& toText) {
    static const QRegularExpression format(QStringLiteral(R"(^\d{2}\.\d{2}\.\d{4}$)"));
    const auto fromValue = fromText.trimmed();
    const auto toValue = toText.trimmed();
    if (!format.match(fromValue).hasMatch() || !format.match(toValue).hasMatch()) {
        return std::unexpected(QStringLiteral("Укажите обе даты в формате ДД.ММ.ГГГГ"));
    }
    const auto from = QDate::fromString(fromValue, QStringLiteral("dd.MM.yyyy"));
    const auto to = QDate::fromString(toValue, QStringLiteral("dd.MM.yyyy"));
    if (!from.isValid() || !to.isValid()) {
        return std::unexpected(QStringLiteral("Указана несуществующая дата"));
    }
    if (from > to) {
        return std::unexpected(QStringLiteral("Начало периода должно быть не позже конца"));
    }
    return ReportingPeriod{from, to};
}

} // namespace wealthtracker::app
