#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QProcess>
#include <QStandardPaths>
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
    CHECK(script.contains(QStringLiteral("assets.includes(reader.url)")));
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

TEST_CASE("Ozon expires pre-login reads and permits a fresh authenticated attempt") {
    QJSEngine engine;
    REQUIRE_FALSE(engine.evaluate(QStringLiteral(R"JS(
      var globalThis=this; var location={origin:'https://finance.ozon.ru'};
      var aborted=0; var now=10000; Date.now=function(){return now};
      var old={status:'running',cancelled:false,attempt:1,startedAt:0,
        controller:{abort:function(){aborted++}}};
      globalThis.__wealthtrackerOzonSync=old;
    )JS")).isError());
    const auto run = [&] { return engine.evaluate(wealthtracker::app::ozon_expire_pending_read_script()); };
    engine.evaluate("now=9999");
    CHECK_FALSE(run().toBool());
    CHECK(engine.evaluate("aborted").toInt() == 0);
    engine.evaluate("now=10000");
    CHECK(run().toBool());
    CHECK(engine.evaluate("aborted").toInt() == 1);
    CHECK(engine.evaluate("old.cancelled").toBool());
    CHECK(engine.evaluate("__wealthtrackerOzonSync.status").toString() == "waiting");
    CHECK(engine.evaluate("__wealthtrackerOzonSync.retryAt").toInt() == 10500);
    CHECK_FALSE(run().toBool());
    engine.evaluate("__wealthtrackerOzonSync={status:'ready',startedAt:0,cancelled:false}");
    CHECK_FALSE(run().toBool());
    engine.evaluate("__wealthtrackerOzonSync=old;old.cancelled=false;location.origin='https://sso.ozon.ru'");
    CHECK_FALSE(run().toBool());
}

TEST_CASE("Ozon selects only verified main-page modules and their declared read export") {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) SKIP("Node.js is required for the async dashboard reader fixture");
    QString asset = QStringLiteral("BKxA_2Oi.js");
    QString exportName = QStringLiteral("Mi");
    bool known = true;
    SECTION("Current module") { asset = "De9NCFy0.js"; exportName = "Ai"; }
    SECTION("Earlier verified module") {}
    SECTION("Previous verified module") { asset = "B6d1cMwe.js"; exportName = "ki"; }
    SECTION("Unknown module is never imported") { asset = "unknown.js"; known = false; }
    const auto setup = QStringLiteral(R"JS(
      var globalThis=this;
      var location={origin:'https://finance.ozon.ru'};
      var document={scripts:[]}; var calls=0; var imported='';
      var AbortController=function(){this.signal={};this.abort=function(){}};
      var performance={getEntriesByType:function(){return [{name:
        'https://cdn2.ozone.ru/s3/bozon-fe-main/release/apps/main/_app/immutable/chunks/%1'}]}};
      var handler=function(){calls++;return Promise.reject({code:'OZON-READ'})};
      handler.url='mainPage';handler.declaration={operationName:'GetMainPage',operationType:'query'};
      var fixtureModule={};fixtureModule['%2']=handler;
    )JS").arg(asset,exportName);
    auto script = wealthtracker::app::ozon_sync_start_script();
    script.replace(QStringLiteral("import("), QStringLiteral("__import("));
    QProcess process;
    process.start(node, {QStringLiteral("-e"), setup
        + QStringLiteral("var __import=function(url){imported=url;return Promise.resolve(fixtureModule)};\n")
        + script + QStringLiteral(";setImmediate(()=>process.stdout.write(JSON.stringify({calls,imported})))")});
    REQUIRE(process.waitForFinished(5000));
    INFO(process.readAllStandardError().toStdString());
    REQUIRE(process.exitCode() == 0);
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    CHECK(result["calls"].toInt() == (known ? 1 : 0));
    if (known) CHECK(result["imported"].toString().endsWith(asset));
    else CHECK(result["imported"].toString().isEmpty());
}

TEST_CASE("Ozon bounds credit reads after validated dashboard access") {
    using namespace wealthtracker::app;
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) SKIP("Node.js is required for the async dashboard reader fixture");
    QString phase;
    QString reason;
    SECTION("Credit module import stalls") { phase = "module"; reason = "OZON-CREDIT-MODULE"; }
    SECTION("Credit context request stalls") { phase = "context"; reason = "OZON-CREDIT-CONTEXT"; }
    SECTION("Credit context body stalls") { phase = "body"; reason = "OZON-CREDIT-CONTEXT"; }
    SECTION("Credit inventory read stalls") { phase = "read"; reason = "OZON-CREDIT-READ"; }
    SECTION("Credit inventory succeeds") {}
    const auto setup = QStringLiteral(R"JS(
      const phase = '%1'; let now = 1000; Date.now = () => now;
      const location = {origin:'https://finance.ozon.ru',pathname:'/lk'};
      const document = {scripts:[]};
      const mainUrl = 'https://cdn2.ozone.ru/s3/bozon-fe-main/release/apps/main/_app/immutable/chunks/De9NCFy0.js';
      const performance = {getEntriesByType:() => [{name:mainUrl}]};
      let release; const pending = new Promise(resolve => {release = resolve});
      let mainCalls = 0, contextCalls = 0, creditCalls = 0, contextReady = false;
      const handler = () => {mainCalls++;return Promise.resolve({isOk:() => true,
        value:{mainPage:{widgets:[{type:'MAIN_ACCOUNT',version:1,status:'READY',
          data:{__typename:'MainAccountData',balanceCents:12345}}]}}})};
      handler.url = 'mainPage'; handler.declaration = {operationName:'GetMainPage',operationType:'query'};
      const debtResponse = {isOk:() => true,value:{me:{client:{installment:{userInfo:{debt:{cents:500}}}}}}};
      const creditReader = () => {
        if (!contextReady) throw Error('Missing credit context');
        creditCalls++; return phase === 'read' ? pending.then(() => debtResponse) : Promise.resolve(debtResponse);
      };
      creditReader.url = 'installment/info';
      creditReader.declaration = {operationName:'InstallmentInfoV4',operationType:'query'};
      const creditModule = {qu:{_internal_setSsrStuff:() => {contextReady = true}},
        Cs:{create:() => ({getClientHandler:() => creditReader})}};
      const __import = url => url === mainUrl ? Promise.resolve({Ai:handler})
        : phase === 'module' ? pending.then(() => creditModule) : Promise.resolve(creditModule);
      const fetch = url => {
        contextCalls++; let sent = false;
        const response = {ok:true,redirected:false,url,body:{getReader:() => ({
          read:() => {
            if (phase === 'body') return pending.then(() => ({done:true}));
            if (sent) return Promise.resolve({done:true});
            sent = true; return Promise.resolve({done:false,value:new TextEncoder().encode('{}')});
          },releaseLock:() => {},cancel:() => Promise.resolve()
        })}};
        return phase === 'context' ? pending.then(() => response) : Promise.resolve(response);
      };
    )JS").arg(phase);
    auto script = ozon_sync_start_script();
    script.replace(QStringLiteral("import("), QStringLiteral("__import("));
    const auto inspect = QStringLiteral(R"JS(
      ;setImmediate(() => {
        const before = {status:__wealthtrackerOzonSync.status,reason:__wealthtrackerOzonSync.reasonCode};
        now += 11000;
        const expired = %1;
        release();
        setImmediate(() => process.stdout.write(JSON.stringify({before,expired,mainCalls,contextCalls,
          creditCalls,projection:JSON.parse(%2)})));
      });
    )JS").arg(ozon_expire_pending_read_script(), ozon_financial_projection_script());
    QProcess process;
    process.start(node, {QStringLiteral("-e"), setup + script + inspect});
    REQUIRE(process.waitForFinished(5000));
    INFO(process.readAllStandardError().toStdString());
    REQUIRE(process.exitCode() == 0);
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    CHECK(result["mainCalls"].toInt() == 1);
    const auto projection = result["projection"].toObject();
    if (phase.isEmpty()) {
        CHECK(result["contextCalls"].toInt() == 1);
        CHECK(result["creditCalls"].toInt() == 1);
        CHECK_FALSE(result["expired"].toBool());
        const auto parsed = parse_ozon_financial_projection(
            QString::fromUtf8(QJsonDocument(projection).toJson()), observedAt);
        REQUIRE(parsed);
        REQUIRE(parsed->stage == OzonProjectionStage::Ready);
        REQUIRE(parsed->batch);
        REQUIRE(parsed->batch->accounts.size() == 2);
        CHECK(parsed->batch->balances.back().balance.minorUnits == -500);
    } else {
        CHECK(result["before"].toObject()["status"].toString() == "running");
        CHECK(result["before"].toObject()["reason"].toString() == reason);
        CHECK(result["expired"].toBool());
        CHECK(projection["stage"].toString() == "invalid");
        CHECK(projection["reasonCode"].toString() == reason);
        CHECK(projection["accounts"].toArray().isEmpty());
    }
}
