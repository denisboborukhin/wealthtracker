#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QPersistentModelIndex>

using wealthtracker::app::AccountModel;
using wealthtracker::app::AccountRow;
using wealthtracker::app::ConnectionModel;
using wealthtracker::app::TransactionModel;
using wealthtracker::app::TransactionRow;

TEST_CASE("holdings sort exact numeric values and keep sorting after refresh") {
    wealthtracker::app::HoldingModel model;
    const QVector<wealthtracker::app::HoldingRow> rows{
        {"ZZ", "Alpha", "9 000 ₽", "—", "—", true, 900000, "RUB"},
        {"AA", "Zulu", "100 ₽", "—", "—", true, 10000, "RUB"},
        {"BB", "Beta", "−1 ₽", "—", "—", false, -100, "RUB"},
        {"CC", "Gamma", "10 $", "—", "—", true, 1000, "USD"},
    };
    const auto ticker = [&](int index) {
        return model.data(model.index(index), wealthtracker::app::HoldingModel::TickerRole).toString();
    };
    model.replace(rows);
    CHECK(ticker(0) == "AA");
    model.setSort("name", false);
    CHECK(ticker(0) == "ZZ");
    model.setSort("value", false);
    CHECK(ticker(0) == "BB");
    CHECK(ticker(1) == "AA");
    CHECK(ticker(2) == "ZZ");
    CHECK(ticker(3) == "CC");
    model.setSort("value", true);
    CHECK(ticker(0) == "ZZ");
    CHECK(ticker(2) == "BB");
    model.replace(rows);
    CHECK(ticker(0) == "ZZ");
    model.setSort("unrecognized", false);
    CHECK(ticker(0) == "ZZ");
    model.replace({});
    CHECK(model.rowCount() == 0);
}

TEST_CASE("account model exposes stable QML roles") {
    AccountModel model;
    model.replace({
        AccountRow{
            .name = QStringLiteral("Карта"),
            .institution = QStringLiteral("Т-Банк"),
            .type = QStringLiteral("Дебетовая карта"),
            .balance = QStringLiteral("1 000"),
            .currency = QStringLiteral("₽"),
            .accent = QStringLiteral("#F9C74F"),
        },
    });

    REQUIRE(model.rowCount() == 1);
    const auto index = model.index(0, 0);
    CHECK(model.data(index, AccountModel::NameRole).toString() ==
          QStringLiteral("Карта"));
    CHECK(model.data(index, AccountModel::InstitutionRole).toString() ==
          QStringLiteral("Т-Банк"));
    CHECK(model.roleNames().value(AccountModel::BalanceRole) == "balance");
}
TEST_CASE("transaction model keeps income state distinct from amount text") {
    TransactionModel model;
    model.replace({
        TransactionRow{
            .title = QStringLiteral("Зарплата"),
            .subtitle = QStringLiteral("Работодатель"),
            .category = QStringLiteral("Доход"),
            .amount = QStringLiteral("+100 000 ₽"),
            .date = QStringLiteral("Сегодня"),
            .income = true,
        },
    });

    const auto index = model.index(0, 0);
    CHECK(model.data(index, TransactionModel::IncomeRole).toBool());
    CHECK_FALSE(model.data(index, TransactionModel::TransferRole).toBool());
    CHECK(model.data(index, TransactionModel::AmountRole).toString() ==
          QStringLiteral("+100 000 ₽"));
}

TEST_CASE("connection model exposes connector availability separately from state") {
    ConnectionModel model;
    model.replace({
        {QStringLiteral("tbank-retail"), QStringLiteral("Т-Банк"),
         QStringLiteral("Accounts"), QStringLiteral("Local plugin"),
         QStringLiteral("Plugin required"), false, false},
        {QStringLiteral("t-invest"), QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Portfolio"), QStringLiteral("Read-only API"),
         QStringLiteral("Connected"), true, true},
    });

    REQUIRE(model.rowCount() == 2);
    const auto roles = model.roleNames();
    CHECK(model.data(model.index(0), ConnectionModel::ConnectorIdRole).toString()
          == QStringLiteral("tbank-retail"));
    CHECK_FALSE(model.data(model.index(0), ConnectionModel::AvailableRole).toBool());
    CHECK(model.data(model.index(1), ConnectionModel::ConnectedRole).toBool());
    CHECK(roles.value(ConnectionModel::AvailableRole) == QByteArrayLiteral("available"));
}

TEST_CASE("connection updates preserve delegates while a click handler is active") {
    ConnectionModel model;
    model.replace({
        {QStringLiteral("tbank-retail"), QStringLiteral("Т-Банк"),
         QStringLiteral("Accounts"), QStringLiteral("Browser login"),
         QStringLiteral("Login required"), false, true},
        {QStringLiteral("t-invest"), QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Portfolio"), QStringLiteral("Read-only API"),
         QStringLiteral("Token required"), false, true},
    });

    const QPersistentModelIndex clickedRow(model.index(0, 0));
    int resetCount = 0;
    int changedCount = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset,
                     [&resetCount] { ++resetCount; });
    QObject::connect(
        &model, &QAbstractItemModel::dataChanged,
        [&changedCount](const QModelIndex&, const QModelIndex&,
                        const QList<int>& roles) {
            ++changedCount;
            CHECK(roles.contains(ConnectionModel::StatusRole));
        });

    model.replace({
        {QStringLiteral("tbank-retail"), QStringLiteral("Т-Банк"),
         QStringLiteral("Accounts"), QStringLiteral("Browser login"),
         QStringLiteral("Opening login"), false, true},
        {QStringLiteral("t-invest"), QStringLiteral("Т-Инвестиции"),
         QStringLiteral("Portfolio"), QStringLiteral("Read-only API"),
         QStringLiteral("Token required"), false, true},
    });

    CHECK(resetCount == 0);
    CHECK(changedCount == 1);
    REQUIRE(clickedRow.isValid());
    CHECK(model.data(clickedRow, ConnectionModel::ConnectorIdRole).toString()
          == QStringLiteral("tbank-retail"));
    CHECK(model.data(clickedRow, ConnectionModel::StatusRole).toString()
          == QStringLiteral("Opening login"));
}
