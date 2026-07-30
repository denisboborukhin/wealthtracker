#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

using wealthtracker::app::AccountModel;
using wealthtracker::app::AccountRow;
using wealthtracker::app::ConnectionModel;
using wealthtracker::app::TransactionModel;
using wealthtracker::app::TransactionRow;

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
