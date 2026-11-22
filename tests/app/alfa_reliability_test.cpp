#include <catch2/catch_test_macros.hpp>

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

import wt.alfa_browser;

namespace {

void initializeAlfaPage(QJSEngine& engine) {
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        globalThis.location = { href: 'https://web.alfabank.ru/dashboard' }
        globalThis.URL = function() {
            return { protocol: 'https:', hostname: 'web.alfabank.ru', port: '',
                     username: '', password: '', origin: 'https://web.alfabank.ru',
                     pathname: '/dashboard' }
        }
        globalThis.document = { scripts: [] }
    )JS"));
    REQUIRE_FALSE(setup.isError());
}

} // namespace

TEST_CASE("Alfa projection requires exact monetary values") {
    QJSEngine engine;
    initializeAlfaPage(engine);
    QString amount;
    QString expected;
    SECTION("empty numeric fields are not zero balances") {
        amount = QStringLiteral(R"({currency: 'RUB'})");
    }
    SECTION("fractional minor units are rejected") {
        amount = QStringLiteral(R"({value: '1.005', currency: 'RUB'})");
    }
    SECTION("integer minor units preserve their value") {
        amount = QStringLiteral(R"({value: '125050', minorUnits: 100, currency: 'RUB'})");
        expected = QStringLiteral("125050");
    }
    SECTION("large exact decimals do not round through binary floating point") {
        amount = QStringLiteral(R"({value: '70368744177664.01', currency: 'RUB'})");
        expected = QStringLiteral("7036874417766401");
    }
    REQUIRE_FALSE(engine.evaluate(QStringLiteral(
        "globalThis.initialState = { layoutData: { accounts: [{ id: 'account', amount: %1 }] } }")
        .arg(amount)).isError());
    const auto evaluated = engine.evaluate(wealthtracker::app::alfa_financial_projection_script());
    REQUIRE_FALSE(evaluated.isError());
    const auto root = QJsonDocument::fromJson(evaluated.toString().toUtf8()).object();
    const auto accounts = root.value(QStringLiteral("accounts")).toArray();
    if (expected.isEmpty()) {
        CHECK(accounts.isEmpty());
    } else {
        REQUIRE(accounts.size() == 1);
        CHECK(accounts.at(0).toObject().value(QStringLiteral("balanceMinor")).toString() == expected);
    }
}

TEST_CASE("Alfa balances do not depend on history state or dashboard transactions") {
    QJSEngine engine;
    initializeAlfaPage(engine);
    REQUIRE_FALSE(engine.evaluate(QStringLiteral(R"JS(
        globalThis.initialState = { layoutData: { accounts: [
            { id: 'first', amount: { value: '125.50', currency: 'RUB' } },
            { id: 'second', amount: { value: '10.00', currency: 'USD' } }
        ], operations: [{ id: 'ignored', accountId: 'first',
            amount: { value: '5.00', currency: 'RUB' },
            operationDate: '2026-10-15T10:00:00Z' }] } }
        globalThis.__wealthtrackerAlfaHistory = { status: 'failed' }
    )JS")).isError());
    const auto projected = engine.evaluate(wealthtracker::app::alfa_financial_projection_script());
    REQUIRE_FALSE(projected.isError());
    const auto object = QJsonDocument::fromJson(projected.toString().toUtf8()).object();
    CHECK(object.value(QStringLiteral("stage")).toString() == QStringLiteral("ready"));
    CHECK(object.value(QStringLiteral("accounts")).toArray().size() == 2);
    CHECK(object.value(QStringLiteral("transactions")).toArray().isEmpty());
}
