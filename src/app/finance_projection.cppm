module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include "app/FinanceModels.hpp"
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
}

export module wt.finance_projection;

import wt.connectors;
import wt.reporting;

namespace wealthtracker::app {
namespace {
using wealthtracker::connectors::Money;
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


} // namespace
} // namespace wealthtracker::app

export namespace wealthtracker::app {

struct ProjectionInstitution {
    QString name;
    QString accent;
};

struct FinanceProjection {
    QVector<AccountRow> accounts;
    QVector<TransactionRow> transactions;
    QVector<CategoryRow> categories;
    QVector<HoldingRow> holdings;
    QString portfolioValue;
    QString netWorth;
    QString income;
    QString spending;
    QString savings;
};

QString formatTransactionDate(const QDate& value, const QDate& today) {
    if (value == today) {
        return QStringLiteral("Сегодня");
    }
    return QLocale(QLocale::Russian, QLocale::Russia)
        .toString(value, QStringLiteral("d MMM yyyy"));
}

// Pure projection: callers provide observation date and all institution metadata.
[[nodiscard]] FinanceProjection projectFinance(
    const QHash<QString, wealthtracker::connectors::NormalizedBatch>& batches,
    const QHash<QString, ProjectionInstitution>& institutions,
    const ReportingPeriod& reportingPeriod, const QDate& today) {
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

    for (auto bank = batches.cbegin(); bank != batches.cend();
         ++bank) {
        const auto details = institutions.value(bank.key());
        const auto& institution = details.name;
        if (institution.isEmpty()) continue;
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
                details.accent,
            });
        }
        for (const auto& position : bank->positions) {
            holdingRows.push_back({position.ticker.isEmpty() ? QStringLiteral("—") : position.ticker,
                position.instrumentName.isEmpty() ? QStringLiteral("Инструмент") : position.instrumentName,
                QStringLiteral("%1 %2").arg(formatMinorUnits(position.currentValue.minorUnits),
                                            currencySymbol(position.currentValue.currency)),
                QStringLiteral("—"), QStringLiteral("—"), true,
                position.currentValue.minorUnits, position.currentValue.currency});
        }
        for (const auto& transaction : bank->transactions) {
            sourceTransactions.push_back({bank.key(), institution, &transaction});
        }
    }

    for (const auto& source : std::as_const(sourceTransactions)) {
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
        const bool internalTransfer = transaction.kind ==
            wealthtracker::connectors::NormalizedTransactionKind::InternalTransfer;
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

    FinanceProjection result;
    result.accounts = std::move(accountRows);
    result.transactions = std::move(sortedTransactions);
    result.categories = std::move(categoryRows);
    result.holdings = std::move(holdingRows);
    result.portfolioValue = portfolioValid
        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(portfolioValue)) : QStringLiteral("—");
    result.netWorth = netWorthValid
                          ? QStringLiteral("%1 ₽").arg(formatMinorUnits(rubNetWorth))
                          : QStringLiteral("—");
    result.income = incomeValid
                              ? QStringLiteral("%1 ₽").arg(formatMinorUnits(reportIncome))
                              : QStringLiteral("—");
    result.spending = spendingValid
                                ? QStringLiteral("%1 ₽").arg(formatMinorUnits(reportSpending))
                                : QStringLiteral("—");
    result.savings = savingsValid
                        ? QStringLiteral("%1 ₽").arg(formatMinorUnits(savings))
                        : QStringLiteral("—");
    return result;
}

} // namespace wealthtracker::app
