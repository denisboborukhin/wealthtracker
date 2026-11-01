#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

import wt.connectors;
import wt.ozon_browser;
import wt.ozon_login;

namespace {

QString project(const QString& page, const QString& stateProperties = {}) {
    QJSEngine engine;
    const auto initialized = engine.evaluate(QStringLiteral(
        "var globalThis=this; var location={origin:'https://finance.ozon.ru',pathname:'/lk'};"
        "globalThis.__wealthtrackerOzonSync={status:'ready',cancelled:false,installmentDebt:null,page:") + page + stateProperties + QStringLiteral("};"));
    if (initialized.isError()) return {};
    return engine.evaluate(wealthtracker::app::ozon_financial_projection_script()).toString();
}

QString mainPage(const QString& amount = QStringLiteral("12345")) {
    return QStringLiteral(R"JSON({widgets:[{type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'MainAccountData',balanceCents:)JSON") + amount + QStringLiteral("}}]}");
}

const QDateTime observedAt = QDateTime::fromString(QStringLiteral("2026-11-01T12:00:00Z"), Qt::ISODate);

} // namespace

TEST_CASE("Ozon permits its verified SSO origin only for login navigation") {
    using namespace wealthtracker::app;
    CHECK(is_ozon_login_url(QUrl("https://finance.ozon.ru/m/lk/main")));
    CHECK_FALSE(is_ozon_login_url(QUrl("https://sso.ozon.ru/")));
    CHECK(is_ozon_login_navigation_url(QUrl("https://sso.ozon.ru/")));
    CHECK(is_ozon_login_navigation_url(QUrl("https://finance.ozon.ru/m/lk/main")));
    for (const auto& text : {"http://sso.ozon.ru/", "https://sub.sso.ozon.ru/",
        "https://sso.ozon.ru.example.com/", "https://id.ozon.ru/",
        "https://sso.ozon.ru:8443/", "https://user@sso.ozon.ru/"}) {
        CHECK_FALSE(is_ozon_login_navigation_url(QUrl(QString::fromLatin1(text))));
    }
}

TEST_CASE("Ozon financial projection validates the verified main-account schema") {
    using namespace wealthtracker::app;
    auto page = mainPage();
    SECTION("numeric widget version") {}
    SECTION("string widget version") { page.replace("version:1", "version:'1'"); }
    const auto parsed = parse_ozon_financial_projection(project(page), observedAt);
    REQUIRE(parsed);
    REQUIRE(parsed->stage == OzonProjectionStage::Ready);
    REQUIRE(parsed->batch);
    REQUIRE(parsed->batch->accounts.size() == 1);
    CHECK(parsed->batch->accounts.front().externalId == QStringLiteral("ozon-main"));
    CHECK(parsed->batch->balances.front().balance.minorUnits == 12345);
    CHECK(parsed->batch->balances.front().balance.currency == QStringLiteral("RUB"));
}

TEST_CASE("Ozon projection rejects malformed recognized balances and incomplete pages") {
    using namespace wealthtracker::app;
    QString page;
    SECTION("fractional minor units") { page = mainPage(QStringLiteral("1.5")); }
    SECTION("unsafe integer") { page = mainPage(QStringLiteral("9007199254740992")); }
    SECTION("numeric string") { page = mainPage(QStringLiteral("'12345'")); }
    SECTION("duplicate main balance") {
        page = QStringLiteral("{widgets:[{type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'MainAccountData',balanceCents:1}},{type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'MainAccountData',balanceCents:2}}]}");
    }
    SECTION("degraded dashboard") { page = QStringLiteral("{error:{code:'unavailable'},widgets:[]}"); }
    SECTION("no financial widgets") { page = QStringLiteral("{widgets:[]}"); }
    SECTION("incorrect data type") { page = QStringLiteral("{widgets:[{type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'OtherData',balanceCents:1}}]}"); }
    SECTION("unsupported version") { page = mainPage().replace("version:1", "version:2"); }
    SECTION("missing version") { page = mainPage().replace("version:1,", ""); }
    const auto parsed = parse_ozon_financial_projection(project(page), observedAt);
    REQUIRE(parsed);
    CHECK(parsed->stage == OzonProjectionStage::Invalid);
    CHECK_FALSE(parsed->batch);
}

TEST_CASE("Ozon chooses the savings aggregate over the single-product presentation") {
    using namespace wealthtracker::app;
    const auto page = QStringLiteral(R"JS({widgets:[{type:'OTHER',widgets:[
        {type:'SAVINGS_B2C_SINGLE_PRODUCT',version:1,status:'READY',data:{__typename:'SavingsB2cSingleProductData',title:'Вклад',balance:{cents:100}}},
        {type:'SAVINGS_B2C_SINGLE_PRODUCT',version:1,status:'READY',data:{__typename:'SavingsB2cSingleProductData',title:'Счёт',balance:{cents:200}}},
        {type:'SAVINGS_B2C_SUMMARY',version:1,status:'READY',data:{__typename:'SavingsB2cSummaryData',title:'Накопления',activeProductsTotalAmount:{cents:300}}}
    ]}]})JS");
    const auto parsed = parse_ozon_financial_projection(project(page), observedAt);
    REQUIRE(parsed);
    REQUIRE(parsed->batch);
    REQUIRE(parsed->batch->balances.size() == 1);
    CHECK(parsed->batch->balances.front().balance.minorUnits == 300);
    CHECK(parsed->batch->accounts.front().externalId == QStringLiteral("ozon-savings"));
}

TEST_CASE("Ozon parser rejects forged canonical financial data") {
    using namespace wealthtracker::app;
    auto object = QJsonDocument::fromJson(project(mainPage()).toUtf8()).object();
    auto accounts = object.value(QStringLiteral("accounts")).toArray();
    auto account = accounts.at(0).toObject();
    SECTION("integer overflow") { account.insert(QStringLiteral("balanceMinor"), QStringLiteral("9223372036854775808")); }
    SECTION("precision loss") { account.insert(QStringLiteral("balanceMinor"), QStringLiteral("9007199254740992")); }
    SECTION("fraction") { account.insert(QStringLiteral("balanceMinor"), QStringLiteral("1.25")); }
    SECTION("unexpected account identifier") { account.insert(QStringLiteral("id"), QStringLiteral("invented")); }
    SECTION("unexpected currency") { account.insert(QStringLiteral("currency"), QStringLiteral("USD")); }
    SECTION("control character in name") { account.insert(QStringLiteral("name"), QStringLiteral("Main\nAccount")); }
    accounts[0] = account;
    object.insert(QStringLiteral("accounts"), accounts);
    CHECK_FALSE(parse_ozon_financial_projection(QString::fromUtf8(QJsonDocument(object).toJson()), observedAt));
}

TEST_CASE("Ozon projection identifies only the verified sign-in route as authentication") {
    using namespace wealthtracker::app;
    QJSEngine engine;
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("var globalThis=this; var location={origin:'https://finance.ozon.ru',pathname:'/apps/auth/signin'};")).isError());
    const auto parsed = parse_ozon_financial_projection(engine.evaluate(ozon_financial_projection_script()).toString(), observedAt);
    REQUIRE(parsed);
    CHECK(parsed->stage == OzonProjectionStage::AuthenticationRequired);
    CHECK_FALSE(parsed->batch);
}

TEST_CASE("Ozon script reuses only the observed dashboard software and bank read handler") {
    using namespace wealthtracker::app;
    const auto& script = ozon_sync_start_script();
    CHECK(script.contains(QStringLiteral("/bozon-fe-main/release/apps/main/_app/immutable/chunks/")));
    CHECK(script.contains(QStringLiteral("B6d1cMwe.js")));
    CHECK(script.contains(QStringLiteral("assets.includes(expected)")));
    CHECK(script.contains(QStringLiteral("handler.url !== 'mainPage'")));
    CHECK(script.contains(QStringLiteral("operationType !== 'query'")));
    CHECK(script.contains(QStringLiteral("handler.call({}, {signal:state.controller.signal})")));
    CHECK(script.contains(QStringLiteral("globalThis[key] === state && !state.cancelled")));
    CHECK_FALSE(script.contains(QStringLiteral("document.cookie")));
    CHECK_FALSE(script.contains(QStringLiteral("localStorage")));
    CHECK_FALSE(script.contains(QStringLiteral("sessionStorage")));
    CHECK(script.count(QStringLiteral("fetch(")) == 1);
    CHECK(script.contains(QStringLiteral("/apps/credit/_mf/context")));
    CHECK(script.contains(QStringLiteral("credit.qu._internal_setSsrStuff(context)")));
    CHECK(script.contains(QStringLiteral("mode:'same-origin',redirect:'error'")));
    CHECK(script.contains(QStringLiteral("length > 2 * 1024 * 1024")));
    CHECK(script.contains(QStringLiteral("DHsJdLKo.js")));
    CHECK(script.contains(QStringLiteral("getClientHandler('installment/info')")));
    CHECK(script.contains(QStringLiteral("operationName !== 'InstallmentInfoV4'")));
    CHECK_FALSE(script.contains(QStringLiteral("approvedLimit.cents")));
}

TEST_CASE("Ozon includes verified MKK debt as a negative credit balance") {
    using namespace wealthtracker::app;
    const auto page = QStringLiteral(R"JS({widgets:[
      {type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'MainAccountData',balanceCents:100}},
      {type:'SAVINGS_B2C_SUMMARY',version:1,status:'READY',data:{__typename:'SavingsB2cSummaryData',title:'Накопления',activeProductsTotalAmount:{cents:200}}},
      {type:'MKK_INSTALLMENT',version:1,status:'READY',data:{__typename:'MkkInstallmentData',mkkInstallmentStatus:'INSTALLMENT_LIMIT_PROGRESS',mkkLimitCents:999999,mkkApprovedLimitCents:9999999}}
    ]})JS");
    const auto parsed = parse_ozon_financial_projection(project(page, QStringLiteral(",installmentDebt:500")), observedAt);
    REQUIRE(parsed);
    REQUIRE(parsed->batch);
    REQUIRE(parsed->batch->accounts.size() == 3);
    CHECK(parsed->batch->accounts.back().externalId == QStringLiteral("ozon-installment"));
    CHECK(parsed->batch->accounts.back().kind == QStringLiteral("credit"));
    CHECK(parsed->batch->balances.back().balance.minorUnits == -500);
    qint64 total = 0;
    for (const auto& balance : parsed->batch->balances) total += balance.balance.minorUnits;
    CHECK(total == -200);
}

TEST_CASE("Ozon never publishes dashboard balances without required MKK debt") {
    using namespace wealthtracker::app;
    auto page = mainPage();
    page.chop(2);
    page += QStringLiteral(",{type:'MKK_INSTALLMENT',version:1,status:'READY',data:{__typename:'MkkInstallmentData',mkkInstallmentStatus:'INSTALLMENT_LIMIT_PROGRESS'}}]}");
    QString extra;
    SECTION("debt read missing") { extra = QStringLiteral(",installmentDebt:undefined"); }
    SECTION("explicit absence contradicts active widget") { extra = QStringLiteral(",installmentDebt:null"); }
    SECTION("negative debt") { extra = QStringLiteral(",installmentDebt:-1"); }
    SECTION("fractional minor units") { extra = QStringLiteral(",installmentDebt:1.5"); }
    SECTION("unsafe debt") { extra = QStringLiteral(",installmentDebt:9007199254740992"); }
    SECTION("numeric string") { extra = QStringLiteral(",installmentDebt:'500'"); }
    SECTION("unknown MKK state") { page.replace("INSTALLMENT_LIMIT_PROGRESS", "UNKNOWN_FUTURE_STATUS"); }
    SECTION("different bank installment product") { page.replace("MKK_INSTALLMENT", "BANK_INSTALLMENT"); }
    const auto parsed = parse_ozon_financial_projection(project(page, extra), observedAt);
    REQUIRE(parsed);
    CHECK(parsed->stage == OzonProjectionStage::Invalid);
    CHECK_FALSE(parsed->batch);
}

TEST_CASE("Ozon allows zero MKK debt and skips only verified precontract states") {
    using namespace wealthtracker::app;
    auto page = mainPage();
    page.chop(2);
    page += QStringLiteral(",{type:'MKK_INSTALLMENT',version:'1',status:'READY',data:{__typename:'MkkInstallmentData',mkkInstallmentStatus:'INSTALLMENT_LIMIT_COMPLETED'}}]}");
    QString extra = QStringLiteral(",installmentDebt:0");
    bool precontract = false;
    SECTION("contract with no debt") {}
    SECTION("precontract does not invent a balance") {
        page.replace("INSTALLMENT_LIMIT_COMPLETED", "WAITING_CONFIRM_INSTALLMENT");
        extra.clear();
        precontract = true;
    }
    const auto parsed = parse_ozon_financial_projection(project(page, extra), observedAt);
    REQUIRE(parsed);
    REQUIRE(parsed->batch);
    CHECK(parsed->batch->accounts.size() == (precontract ? 1 : 2));
    if (!precontract) CHECK(parsed->batch->balances.back().balance.minorUnits == 0);
}

TEST_CASE("Ozon native parser rejects a forged positive installment balance") {
    using namespace wealthtracker::app;
    auto object = QJsonDocument::fromJson(project(mainPage()).toUtf8()).object();
    auto accounts = object.value(QStringLiteral("accounts")).toArray();
    accounts.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("ozon-installment")},
        {QStringLiteral("name"), QStringLiteral("Рассрочка")}, {QStringLiteral("kind"), QStringLiteral("credit")},
        {QStringLiteral("currency"), QStringLiteral("RUB")}, {QStringLiteral("balanceMinor"), QStringLiteral("100")}});
    object.insert(QStringLiteral("accounts"), accounts);
    CHECK_FALSE(parse_ozon_financial_projection(QString::fromUtf8(QJsonDocument(object).toJson()), observedAt));
}

TEST_CASE("Ozon reads installment inventory independently of dashboard widgets") {
    using namespace wealthtracker::app;
    const auto parsed = parse_ozon_financial_projection(
        project(mainPage(), QStringLiteral(",installmentDebt:500")), observedAt);
    REQUIRE(parsed);
    REQUIRE(parsed->batch);
    REQUIRE(parsed->batch->accounts.size() == 2);
    CHECK(parsed->batch->accounts.back().externalId == QStringLiteral("ozon-installment"));
    CHECK(parsed->batch->balances.back().balance.minorUnits == -500);
}

TEST_CASE("Ozon requires explicit installment coverage even without a widget") {
    using namespace wealthtracker::app;
    QString extra;
    bool absent = false;
    SECTION("verified explicit absence") { extra = QStringLiteral(",installmentDebt:null"); absent = true; }
    SECTION("query not completed") { extra = QStringLiteral(",installmentDebt:undefined"); }
    const auto parsed = parse_ozon_financial_projection(project(mainPage(), extra), observedAt);
    REQUIRE(parsed);
    CHECK(parsed->stage == (absent ? OzonProjectionStage::Ready : OzonProjectionStage::Invalid));
    if (absent) {
        REQUIRE(parsed->batch);
        CHECK(parsed->batch->accounts.size() == 1);
    } else CHECK_FALSE(parsed->batch);
}
