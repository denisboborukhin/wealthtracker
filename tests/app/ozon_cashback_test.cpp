#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>

import wt.ozon_cashback;
import wt.cashback_sync;

namespace {
using namespace wealthtracker::app;

void initialize(QJSEngine& engine) {
    const auto initialized = engine.evaluate(QStringLiteral(R"JS(
      var globalThis = this
      globalThis.location = {href:'https://finance.ozon.ru/m/lk/main'}
      globalThis.URL = function(value) {
        const match = String(value).match(/^(https?):\/\/([^/]+)(\/[^?#]*)?(\?[^#]*)?(#.*)?$/)
        if (!match) throw new Error('Invalid URL')
        const credentials = match[2].includes('@')
        this.origin = match[1]+'://'+match[2].split('@').pop()
        this.username = credentials ? 'fixture' : ''; this.password = ''
        this.pathname = match[3] || '/'; this.search = match[4] || ''; this.hash = match[5] || ''
      }
      globalThis.TextEncoder = function() { this.encode = value => ({byteLength:String(value).length}) }
      globalThis.AbortController = function() { this.signal = {}; this.abort = function() {this.aborted=true} }
      globalThis.resourceAssets = []
      globalThis.performance = {getEntriesByType:() => resourceAssets.map(name => ({name:name}))}
      globalThis.document = {scripts:[]}
      globalThis.imports = []; globalThis.requests = []
      globalThis.contextRequests = []; globalThis.contextPending = []
      globalThis.contextInitialized = false; globalThis.deferContext = false
      globalThis.contextOk = true
      globalThis.contextText = JSON.stringify({privateContext:'fixture context'})
      globalThis.fetch = (url,config) => {
        contextRequests.push({url:url,config:config})
        const response = () => ({ok:contextOk,text:() => Promise.resolve(contextText)})
        return deferContext ? new Promise(resolve => contextPending.push(() => resolve(response()))) : Promise.resolve(response())
      }
      globalThis.mode = 'LOYALTY_PROGRAM_KIND_V2_CASHBACK'
      globalThis.failRead = ''; globalThis.defer = false; globalThis.pending = []
      globalThis.payload = {me:{client:{loyaltyV2:{
        currentMonthlyPromotionsV2:{period:'2026-11',isPromotionsSelected:false,atLeastOncePromotionsSelected:true,
          monthlyPromotions:[{status:'ACCEPTED',title:'Кафе',cashbackPromotionParams:{decimalPercent:5.25}},
            {status:'NOT_ACCEPTED',title:'Offered only',cashbackPromotionParams:{decimalPercent:99}},
            {status:'ACTIVE',title:'Unproven active offer',cashbackPromotionParams:{decimalPercent:99}}]},
        futureMonthlyPromotionsV2:{period:'2026-12',isPromotionsSelected:true,
          monthlyPromotions:[{status:'ACCEPTED',title:'Транспорт',cashbackPromotionParams:{decimalPercent:'10'}}]}
      }}}}
      const method = (name,path,respond) => {
        const handler = function() {}
        handler.url = path; handler.declaration = {operationName:name,query:''}
        handler.call = (params,config) => {
          requests.push({name:name,path:path,params:params,config:config})
          const result = () => ({isOk:() => failRead !== name,value:respond()})
          return defer ? new Promise(resolve => pending.push(() => resolve(result()))) : Promise.resolve(result())
        }
        return handler
      }
      const modeHandler = method('GetLoyaltyProgramV2','loyalty/bonusState/loyaltyProgramV2',() => ({loyaltyProgram:mode}))
      globalThis.moduleFixture = {
        Kc:'/apps/loyalty',rc:{_internal_setSsrStuff:context => {
          if (!context || context.privateContext !== 'fixture context') throw new Error('Wrong context wrapper')
          contextInitialized = true
        }},
        get Fn() {
          if (!contextInitialized) throw new Error('Native accessor used before loyalty context initialization')
          return modeHandler
        },
        $s:{create:declaration => {
          if (declaration.name !== 'GetCashbackProgram' || declaration.gqlNode !== false ||
              declaration.mapper(payload) !== payload) throw new Error('Unexpected declaration')
          return {getClientHandler:path => {
            if (path !== 'loyalty/bonusState/cashbackProgram') throw new Error('Unexpected read')
            return method('GetCashbackProgram',path,() => payload)
          }}
        }}
      }
      globalThis.__import = url => {imports.push(url);return Promise.resolve(moduleFixture)}
      // Test mutations inspect the handler before startup; the actual run must
      // initialize context again before accessing it.
      contextInitialized = true
    )JS"));
    REQUIRE_FALSE(initialized.isError());
}

void drain(QJSEngine& engine) {
    for (int index = 0; index < 24; ++index) {
        QCoreApplication::processEvents();
        engine.evaluate(QStringLiteral("0"));
    }
}

QString run(QJSEngine& engine, const QString& runId = QStringLiteral("fixture"),
    const QDate& today = QDate(2026,11,1)) {
    // Only replace module loading; the production script and native-client
    // contract otherwise run unchanged in the fixture engine.
    auto script = ozon_cashback_start_script(today, runId);
    script.replace(QStringLiteral("import("), QStringLiteral("__import("));
    engine.evaluate(QStringLiteral("contextInitialized=false"));
    const auto result = engine.evaluate(script);
    INFO(result.toString().toStdString());
    REQUIRE_FALSE(result.isError());
    if (result.toString() == QStringLiteral("wrong-origin")) return result.toString();
    drain(engine);
    return engine.evaluate(QStringLiteral(
        "__wealthtrackerCashback.status === 'ready' ? JSON.stringify(__wealthtrackerCashback.result) : __wealthtrackerCashback.status"))
        .toString();
}
}

TEST_CASE("Ozon cashback reads accepted monthly categories through its native read clients") {
    QJSEngine engine;
    initialize(engine);
    const auto body = run(engine);
    INFO(body.toStdString());
    const auto snapshot = parse_cashback_projection("ozon", body.toUtf8(), QDate(2026,11,1));
    REQUIRE(snapshot);
    REQUIRE(snapshot->months.size() == 2);
    const auto rows = snapshot->rows("ozon", QDate(2026,11,1));
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().toMap()["name"] == "Кафе");
    CHECK(rows.front().toMap()["percent"] == "5,25 %");
    CHECK(snapshot->rows("ozon", QDate(2026,12,1)).front().toMap()["percent"] == "10 %");
    CHECK(engine.evaluate("requests.length").toInt() == 2);
    CHECK(engine.evaluate("requests[0].path").toString() == "loyalty/bonusState/loyaltyProgramV2");
    CHECK(engine.evaluate("requests[1].path").toString() == "loyalty/bonusState/cashbackProgram");
    CHECK(engine.evaluate("requests.every(r => Object.keys(r.params).length === 0 && r.config.signal === __wealthtrackerCashback.controller.signal)").toBool());
    CHECK(engine.evaluate("contextRequests.length").toInt() == 1);
    CHECK(engine.evaluate("contextRequests[0].url").toString() == "/apps/loyalty/_mf/context");
    CHECK(engine.evaluate("contextRequests[0].config.method === 'GET' && contextRequests[0].config.credentials === 'same-origin' && contextRequests[0].config.redirect === 'error' && contextRequests[0].config.signal === __wealthtrackerCashback.controller.signal").toBool());
    CHECK_FALSE(engine.evaluate("JSON.stringify(__wealthtrackerCashback).includes('fixture context')").toBool());
    CHECK(engine.evaluate("imports[0]").toString() == "https://cdn2.ozone.ru/s3/bozon-jinx/assets/prod/660d86c1/apps/loyalty/_app/immutable/chunks/MFBonuses.DVjct8rH.js");
    CHECK(engine.evaluate("__wealthtrackerCashback.phase").toString() == "READY");
}

TEST_CASE("Ozon cashback uses the observed official module and rejects unrelated resource URLs") {
    QJSEngine engine;
    initialize(engine);
    QString expected;
    SECTION("new observed official module") {
        expected = "https://cdn2.ozone.ru/s3/bozon-jinx/assets/prod/abcdef12/apps/loyalty/_app/immutable/chunks/MFBonuses.NewVersion.js";
        engine.evaluate(QStringLiteral("resourceAssets=['%1']").arg(expected));
    }
    SECTION("unrelated host and credential-bearing URL") {
        expected = "https://cdn2.ozone.ru/s3/bozon-jinx/assets/prod/660d86c1/apps/loyalty/_app/immutable/chunks/MFBonuses.DVjct8rH.js";
        engine.evaluate("resourceAssets=['https://attacker.example/MFBonuses.Fake.js','https://user@cdn2.ozone.ru/s3/bozon-jinx/assets/prod/abcdef12/apps/loyalty/_app/immutable/chunks/MFBonuses.Fake.js']");
    }
    REQUIRE(parse_cashback_projection("ozon", run(engine).toUtf8(), QDate(2026,11,1)));
    CHECK(engine.evaluate("imports[0]").toString() == expected);
}

TEST_CASE("Ozon cashback distinguishes unselected and unavailable future months") {
    QJSEngine engine;
    initialize(engine);
    SECTION("current categories have not been chosen") {
        engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.atLeastOncePromotionsSelected=false;payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions.shift()");
        const auto snapshot = parse_cashback_projection("ozon", run(engine).toUtf8(), QDate(2026,11,1));
        REQUIRE(snapshot);
        CHECK(snapshot->selectionRequired.contains(QDate(2026,11,1)));
        CHECK(snapshot->rows("ozon", QDate(2026,11,1)).isEmpty());
    }
    SECTION("future offers are not available") {
        engine.evaluate("payload.me.client.loyaltyV2.futureMonthlyPromotionsV2={period:'2026-12',isPromotionsSelected:false,monthlyPromotions:[]}");
        const auto snapshot = parse_cashback_projection("ozon", run(engine).toUtf8(), QDate(2026,11,1));
        REQUIRE(snapshot);
        CHECK_FALSE(snapshot->months.contains(QDate(2026,12,1)));
    }
    SECTION("future offers are available but not chosen") {
        engine.evaluate("payload.me.client.loyaltyV2.futureMonthlyPromotionsV2.isPromotionsSelected=false;payload.me.client.loyaltyV2.futureMonthlyPromotionsV2.monthlyPromotions[0].status='NOT_ACCEPTED'");
        const auto snapshot = parse_cashback_projection("ozon", run(engine).toUtf8(), QDate(2026,11,1));
        REQUIRE(snapshot);
        CHECK(snapshot->selectionRequired.contains(QDate(2026,12,1)));
    }
}

TEST_CASE("Ozon cashback reports stars mode without loading cashback or changing bank preferences") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("mode='LOYALTY_PROGRAM_KIND_V2_STARS'");
    CHECK(run(engine) == "failed");
    CHECK(engine.evaluate("__wealthtrackerCashback.reasonCode").toString() == "OZON-STARS");
    CHECK(engine.evaluate("requests.length").toInt() == 1);
    CHECK(engine.evaluate("__wealthtrackerCashback.result === undefined").toBool());
}

TEST_CASE("Ozon cashback fails closed on changed methods missing data invalid rates and stale periods") {
    QJSEngine engine;
    initialize(engine);
    SECTION("wrong origin") { engine.evaluate("location.href='https://finance.ozon.ru.attacker.example/m/lk/main'"); }
    SECTION("changed native method") { engine.evaluate("moduleFixture.Fn.url='loyalty/stars/toggleProgramV2'"); }
    SECTION("changed native declaration") { engine.evaluate("moduleFixture.Fn.declaration.operationName='ActivateLoyaltyStarsV2'"); }
    SECTION("failed native read") { engine.evaluate("failRead='GetCashbackProgram'"); }
    SECTION("inactive program") { engine.evaluate("mode='LOYALTY_PROGRAM_KIND_V2_DISABLED'"); }
    SECTION("missing data") { engine.evaluate("payload={}"); }
    SECTION("unknown period") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.period='ноябрь'"); }
    SECTION("stale year") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.period='2025-11'"); }
    SECTION("excess precision") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions[0].cashbackPromotionParams.decimalPercent='5.123'"); }
    SECTION("out of range percent") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions[0].cashbackPromotionParams.decimalPercent='100.01'"); }
    SECTION("unknown selection state") { engine.evaluate("delete payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.isPromotionsSelected"); }
    SECTION("contradictory selection state") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.atLeastOncePromotionsSelected=false"); }
    SECTION("unknown category status") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions[0].status='MAYBE_SELECTED'"); }
    SECTION("duplicate selected category") { engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions.push(payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.monthlyPromotions[0])"); }
    const auto result = run(engine);
    CHECK_FALSE(parse_cashback_projection("ozon", result.toUtf8(), QDate(2026,11,1)));
    CHECK(result != "loading");
    CHECK(engine.evaluate("imports.length").toInt() <= 1);
}

TEST_CASE("Ozon cashback keeps explicit January selection separate across year rollover") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("payload.me.client.loyaltyV2.currentMonthlyPromotionsV2.period='2026-12';payload.me.client.loyaltyV2.futureMonthlyPromotionsV2.period='2027-01'");
    const auto snapshot = parse_cashback_projection("ozon", run(engine,"fixture",QDate(2026,12,31)).toUtf8(), QDate(2026,12,31));
    REQUIRE(snapshot);
    CHECK(snapshot->months.contains(QDate(2027,1,1)));
    CHECK(snapshot->rows("ozon", QDate(2027,1,1)).size() == 1);
}

TEST_CASE("Ozon cashback diagnostics retain only fixed phases when native operations throw") {
    QJSEngine engine;
    initialize(engine);
    QString expected;
    SECTION("module import") {
        expected = "IMPORT";
        engine.evaluate("__import=() => Promise.reject(new Error('private fixture error'))");
    }
    SECTION("mode contract") {
        expected = "MODE-CONTRACT";
        engine.evaluate("Object.defineProperty(moduleFixture,'Fn',{get:() => {throw new Error('private fixture error')}})");
    }
    SECTION("mode call") {
        expected = "MODE-CALL";
        engine.evaluate("moduleFixture.Fn.call=() => {throw new Error('private fixture error')}");
    }
    SECTION("mode response") {
        expected = "MODE-VALUE";
        engine.evaluate("moduleFixture.Fn.call=() => Promise.resolve({isOk:() => {throw new Error('private fixture error')}})");
    }
    SECTION("cashback creation") {
        expected = "CASHBACK-CREATE";
        engine.evaluate("moduleFixture.$s.create=() => {throw new Error('private fixture error')}");
    }
    SECTION("cashback call") {
        expected = "CASHBACK-CALL";
        engine.evaluate("const originalCreate=moduleFixture.$s.create;moduleFixture.$s.create=declaration => {const operation=originalCreate(declaration);const originalGet=operation.getClientHandler;operation.getClientHandler=path => {const handler=originalGet(path);handler.call=() => {throw new Error('private fixture error')};return handler};return operation}");
    }
    SECTION("cashback response") {
        expected = "CASHBACK-VALUE";
        engine.evaluate("Object.defineProperty(payload,'me',{get:() => {throw new Error('private fixture error')}})");
    }
    SECTION("month projection") {
        expected = "MONTHS";
        engine.evaluate("const originalString=String;String=value => {if(value===5.25)throw new Error('private fixture error');return originalString(value)}");
    }
    CHECK(run(engine) == "failed");
    CHECK(engine.evaluate("__wealthtrackerCashback.phase").toString() == expected);
    CHECK(engine.evaluate("__wealthtrackerCashback.reasonCode").toString() == "OZON-READ");
    CHECK(engine.evaluate("JSON.stringify(__wealthtrackerCashback).includes('private fixture error')").toBool() == false);
    CHECK(engine.evaluate("__wealthtrackerCashback.result === undefined").toBool());
}

TEST_CASE("Ozon cashback rejects invalid loyalty context before invoking native clients") {
    QJSEngine engine;
    initialize(engine);
    SECTION("wrong native service base") { engine.evaluate("moduleFixture.Kc='/apps/main'"); }
    SECTION("missing native context setter") { engine.evaluate("delete moduleFixture.rc._internal_setSsrStuff"); }
    SECTION("unsuccessful context read") { engine.evaluate("contextOk=false"); }
    SECTION("failed context transport") { engine.evaluate("fetch=() => Promise.reject(new Error('private context error'))"); }
    SECTION("malformed context JSON") { engine.evaluate("contextText='not JSON'"); }
    SECTION("non-object context") { engine.evaluate("contextText='[]'"); }
    SECTION("null context") { engine.evaluate("contextText='null'"); }
    SECTION("oversized context") { engine.evaluate("contextText=' '.repeat(2*1024*1024+1)"); }
    SECTION("native setter rejects a context wrapper") { engine.evaluate("contextText='{}'"); }
    CHECK(run(engine) == "failed");
    CHECK(engine.evaluate("__wealthtrackerCashback.phase").toString() == "CONTEXT");
    CHECK(engine.evaluate("__wealthtrackerCashback.reasonCode").toString() == "OZON-CONTEXT");
    CHECK(engine.evaluate("requests.length").toInt() == 0);
    CHECK_FALSE(engine.evaluate("contextInitialized").toBool());
    CHECK_FALSE(engine.evaluate("JSON.stringify(__wealthtrackerCashback).includes('private context error')").toBool());
    CHECK(engine.evaluate("__wealthtrackerCashback.result === undefined").toBool());
}

TEST_CASE("Ozon cashback cancellation prevents late context initialization and native reads") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("deferContext=true");
    CHECK(run(engine,"first") == "loading");
    CHECK(engine.evaluate("__wealthtrackerCashback.phase").toString() == "CONTEXT");
    engine.evaluate("globalThis.firstState=__wealthtrackerCashback");
    CHECK(run(engine,"second") == "loading");
    CHECK(engine.evaluate("firstState.controller.aborted").toBool());
    engine.evaluate("contextPending[0]()");
    drain(engine);
    CHECK_FALSE(engine.evaluate("contextInitialized").toBool());
    CHECK(engine.evaluate("requests.length").toInt() == 0);
    CHECK(engine.evaluate("__wealthtrackerCashback.id").toString() == "second");
    engine.evaluate("__wealthtrackerCashback.active=false;contextPending[1]()");
    drain(engine);
    CHECK_FALSE(engine.evaluate("contextInitialized").toBool());
    CHECK(engine.evaluate("requests.length").toInt() == 0);
    CHECK(engine.evaluate("__wealthtrackerCashback.result === undefined").toBool());
}

TEST_CASE("Ozon cashback cancellation prevents old callbacks from publishing into another run") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("defer=true");
    CHECK(run(engine,"first") == "loading");
    engine.evaluate("globalThis.firstState=__wealthtrackerCashback");
    CHECK(run(engine,"second") == "loading");
    CHECK(engine.evaluate("firstState.controller.aborted").toBool());
    engine.evaluate("pending[0]()");
    drain(engine);
    CHECK(engine.evaluate("__wealthtrackerCashback.status").toString() == "loading");
    CHECK(engine.evaluate("__wealthtrackerCashback.id").toString() == "second");
    CHECK(engine.evaluate("requests.length").toInt() == 2);
    engine.evaluate("__wealthtrackerCashback.active=false;pending[1]()");
    drain(engine);
    CHECK(engine.evaluate("__wealthtrackerCashback.result === undefined").toBool());
}
