#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

import wt.cashback_sync;
import wt.cashback_state;
import wt.cashback;

namespace {
using namespace wealthtracker::app;

void initialize(QJSEngine& engine) {
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
      var globalThis = this
      globalThis.location = {href:'https://online.vtb.ru/products/cashback/catalogue'}
      globalThis.URL = function() { return {origin:'https://online.vtb.ru',username:'',password:''} }
      globalThis.TextEncoder = function() { this.encode = value => ({byteLength:String(value).length}) }
      globalThis.AbortController = function() { this.signal = {}; this.abort = function() {this.aborted=true} }
      globalThis.requests = []
      globalThis.serverDate = 'Thu, 29 Oct 2026 12:00:00 GMT'
      globalThis.payload = {categories:[
        {month:'сентябрь',choice:false,categories:[{displayName:'Expired',displayPercent:'99%'}]},
        {month:'Октябрь',choice:false,categories:[{displayName:'Кафе',displayPercent:'5,25%'}],
          archive:[{displayName:'Archive',displayPercent:'99%'}]},
        {month:'ноябрь',choice:false,categories:[{displayName:'Транспорт',displayPercent:'10%'}]}
      ], availableCategories:[{displayName:'Unselected offer',displayPercent:'99%'}]}
      globalThis.mf_1488_core = {get:function(name) {
        if (name !== './HttpClient') throw new Error('Unexpected module')
        return Promise.resolve(function() { return {http:{get:function(path,config) {
          requests.push({path:path,config:config})
          return Promise.resolve({data:payload,headers:{date:serverDate}})
        }}} })
      }}
    )JS"));
    REQUIRE_FALSE(setup.isError());
}

QString run(QJSEngine& engine, const QDate& day = QDate(2026,10,29), const QString& id = "fixture") {
    const auto started = engine.evaluate(vtb_cashback_start_script(day, id));
    REQUIRE_FALSE(started.isError());
    for (int index = 0; index < 16; ++index) {
        QCoreApplication::processEvents();
        engine.evaluate(QStringLiteral("0"));
    }
    const auto result = engine.evaluate(vtb_cashback_projection_script(id));
    REQUIRE_FALSE(result.isError());
    return result.toString();
}
}

TEST_CASE("VTB cashback reads confirmed current and next month without offers or archive") {
    QJSEngine engine;
    initialize(engine);
    const auto result = run(engine);
    INFO(result.toStdString());
    const auto snapshot = parse_cashback_projection("vtb", result.toUtf8(), QDate(2026,10,29));
    REQUIRE(snapshot);
    REQUIRE(snapshot->months.size() == 2);
    const auto current = snapshot->categories.rows("vtb", QDate(2026,10,1));
    REQUIRE(current.size() == 1);
    CHECK(current.front().toMap()["name"] == "Кафе");
    CHECK(current.front().toMap()["percent"] == "5,25 %");
    CHECK(snapshot->categories.rows("vtb", QDate(2026,11,1)).front().toMap()["percent"] == "10 %");
    CHECK(engine.evaluate("requests.length").toInt() == 1);
    CHECK(engine.evaluate("requests[0].path").toString() == "/msa/api-gw/private/loya/loya-web/v2/bonus/month/categories");
    CHECK(engine.evaluate("requests[0].config.headers['X-Source']").toString() == "VTB_ONLINE");
}

TEST_CASE("VTB cashback choice flag never promotes offers into selected categories") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("payload.categories[1].choice=true; payload.categories[2].choice=true");
    const auto result = run(engine);
    const auto snapshot = parse_cashback_projection("vtb", result.toUtf8(), QDate(2026,10,29));
    REQUIRE(snapshot);
    CHECK(snapshot->selectionRequired.size() == 2);
    CHECK(snapshot->categories.rows("vtb", QDate(2026,10,1)).isEmpty());
    CHECK(snapshot->categories.rows("vtb", QDate(2026,11,1)).isEmpty());
}

TEST_CASE("VTB cashback rejects schema drift invalid rates wrong months and off-origin pages") {
    QJSEngine engine;
    initialize(engine);
    SECTION("wrong month") { engine.evaluate("payload.categories[2].month='декабрь'"); }
    SECTION("stale server year") { engine.evaluate("serverDate='Wed, 29 Oct 2025 12:00:00 GMT'"); }
    SECTION("missing server date") { engine.evaluate("serverDate=''"); }
    SECTION("unknown selection state") { engine.evaluate("delete payload.categories[1].choice"); }
    SECTION("unknown rate wording") { engine.evaluate("payload.categories[1].categories[0].displayPercent='5% + бонус'"); }
    SECTION("too many categories") { engine.evaluate("payload.categories[1].categories=Array(33).fill({displayName:'Too many',displayPercent:'5%'})"); }
    SECTION("off-origin") { engine.evaluate("URL=function(){return {origin:'https://example.com',username:'',password:''}}"); }
    const auto result = run(engine);
    CHECK_FALSE(parse_cashback_projection("vtb", result.toUtf8(), QDate(2026,10,29)));
}

TEST_CASE("VTB cashback keeps next January separate across year rollover") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("payload.categories[1].month='декабрь';payload.categories[2].month='январь';serverDate='Thu, 31 Dec 2026 12:00:00 GMT'");
    const auto result = run(engine, QDate(2026,12,31));
    const auto snapshot = parse_cashback_projection("vtb", result.toUtf8(), QDate(2026,12,31));
    REQUIRE(snapshot);
    CHECK(snapshot->months.contains(QDate(2027,1,1)));
    CHECK(snapshot->categories.rows("vtb", QDate(2027,1,1)).size() == 1);
}

TEST_CASE("VTB cashback preserves upper bound qualifiers in displayed rates") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("payload.categories[1].categories[0].displayPercent='до 5,25%'");
    const auto snapshot = parse_cashback_projection("vtb", run(engine).toUtf8(), QDate(2026,10,29));
    REQUIRE(snapshot);
    CHECK(snapshot->rows("vtb",QDate(2026,10,1)).front().toMap()["percent"] == "до 5,25 %");
}

TEST_CASE("Cashback projection distinguishes covered empty months from unavailable ones") {
    const QByteArray body = R"({"version":1,"stage":"ready","months":[
      {"month":"2026-10-01","selectionRequired":false,"categories":[]}]})";
    const auto result = parse_cashback_projection("vtb", body, QDate(2026,10,29));
    REQUIRE(result);
    CHECK(result->months.contains(QDate(2026,10,1)));
    CHECK_FALSE(result->months.contains(QDate(2026,11,1)));
    CHECK_FALSE(parse_cashback_projection("vtb", R"({"version":1,"stage":"ready","months":[]})", QDate(2026,10,29)));
}

TEST_CASE("Old VTB cashback callbacks cannot publish into a replacement request") {
    QJSEngine engine;
    initialize(engine);
    engine.evaluate("var pending=[];mf_1488_core.get=function(){return Promise.resolve(function(){return {http:{get:function(){return new Promise(function(resolve){pending.push(resolve)})}}}})}");
    CHECK(run(engine, QDate(2026,10,29), "first") == "loading");
    CHECK(run(engine, QDate(2026,10,29), "second") == "loading");
    engine.evaluate("pending[0]({data:payload,headers:{date:serverDate}})");
    for (int i=0;i<8;++i) { QCoreApplication::processEvents(); engine.evaluate("0"); }
    CHECK(engine.evaluate(vtb_cashback_projection_script("first")).toString().isEmpty());
    CHECK(engine.evaluate(vtb_cashback_projection_script("second")).toString() == "loading");
}

TEST_CASE("Cashback partial refresh preserves uncovered month choices and their original timestamp") {
    CashbackSnapshot previous;
    const QDate today(2026,10,29);
    const QDate current(2026,10,1), next(2026,11,1);
    REQUIRE(previous.categories.set("tbank","2026-10-01","Кафе","5",today));
    REQUIRE(previous.categories.set("tbank","2026-11-01","Транспорт","10",today));
    previous.months = {current,next};
    previous.upperBounds.insert("2026-11-01/Транспорт");
    CashbackSnapshot stored;
    const QDateTime first(today,QTime(12,0));
    stored.merge("tbank",previous,first);
    CashbackSnapshot incoming;
    incoming.months.insert(current);
    REQUIRE(incoming.categories.set("tbank","2026-10-01","Кафе","7",today));
    stored.merge("tbank",incoming,first.addSecs(60));
    CHECK(stored.rows("tbank",current).front().toMap()["percent"] == "7 %");
    CHECK(stored.rows("tbank",next).front().toMap()["percent"] == "до 10 %");
    CHECK(stored.observedAt.value(next) == first);
    CHECK(stored.observedAt.value(current) == first.addSecs(60));
    CashbackSnapshot empty;
    empty.months.insert(next);
    empty.selectionRequired.insert(next);
    stored.merge("tbank",empty,first.addSecs(120));
    CHECK(stored.rows("tbank",next).isEmpty());
    CHECK(stored.selectionRequired.contains(next));
    CHECK(stored.upperBounds.isEmpty());
}

TEST_CASE("Saved automatic cashback restores historical months rates and observation timestamps") {
    const QDate current(2026,12,1), next(2027,1,1);
    const auto projection = parse_cashback_projection("vtb", R"({"version":1,"stage":"ready","months":[
      {"month":"2026-12-01","selectionRequired":false,"categories":[
        {"name":"Кафе","percent":"5.25","upperBound":true}]},
      {"month":"2027-01-01","selectionRequired":true,"categories":[]}]})", current);
    REQUIRE(projection);
    auto snapshot = *projection;
    const auto firstObserved = QDateTime::fromString("2026-12-25T12:00:00.123Z", Qt::ISODateWithMs);
    const auto nextObserved = firstObserved.addSecs(3600);
    snapshot.observedAt.insert(current, firstObserved);
    snapshot.observedAt.insert(next, nextObserved);
    const auto saved = serialize_cashback_snapshot("vtb", snapshot);
    REQUIRE(saved);
    // Restoration deliberately has no dependency on today's month.
    const auto restored = parse_cashback_snapshot("vtb", *saved);
    REQUIRE(restored);
    CHECK(restored->months == snapshot.months);
    CHECK(restored->selectionRequired == snapshot.selectionRequired);
    CHECK(restored->upperBounds == snapshot.upperBounds);
    CHECK(restored->observedAt == snapshot.observedAt);
    CHECK(restored->rows("vtb", current).front().toMap()["percent"] == "до 5,25 %");
    CHECK(restored->rows("vtb", next).isEmpty());
    CHECK(serialize_cashback_snapshot("vtb", *restored) == saved);
    CHECK_FALSE(parse_cashback_snapshot("alfa", *saved));
}

TEST_CASE("Saved automatic cashback preserves covered empty months without implying coverage elsewhere") {
    const QDate month(2026,10,1);
    CashbackSnapshot snapshot;
    snapshot.months.insert(month);
    snapshot.observedAt.insert(month, QDateTime::fromString("2026-10-29T12:00:00.000Z", Qt::ISODateWithMs));
    const auto body = serialize_cashback_snapshot("alfa", snapshot);
    REQUIRE(body);
    const auto restored = parse_cashback_snapshot("alfa", *body);
    REQUIRE(restored);
    CHECK(restored->months == QSet<QDate>{month});
    CHECK(restored->selectionRequired.isEmpty());
    CHECK(restored->categories.rows("alfa", month).isEmpty());
    CHECK_FALSE(restored->months.contains(month.addMonths(1)));
}

TEST_CASE("Saved automatic cashback rejects incomplete or inconsistent snapshots before serialization") {
    CashbackSnapshot snapshot;
    const QDate month(2026,10,1);
    snapshot.months.insert(month);
    snapshot.observedAt.insert(month, QDateTime::fromString("2026-10-29T12:00:00.000Z", Qt::ISODateWithMs));
    REQUIRE(snapshot.categories.set("tbank", "2026-10-01", "Кафе", "5", month));
    SECTION("missing timestamp") { snapshot.observedAt.clear(); }
    SECTION("invalid timestamp") { snapshot.observedAt[month] = QDateTime{}; }
    SECTION("uncovered timestamp") { snapshot.observedAt.insert(month.addMonths(1), snapshot.observedAt[month]); }
    SECTION("uncovered selection flag") { snapshot.selectionRequired.insert(month.addMonths(1)); }
    SECTION("selection required with chosen categories") { snapshot.selectionRequired.insert(month); }
    SECTION("unknown rate qualifier") { snapshot.upperBounds.insert("2026-10-01/Missing"); }
    SECTION("categories from another bank") { REQUIRE(snapshot.categories.set("alfa", "2026-10-01", "Кафе", "3", month)); }
    SECTION("uncovered categories") { REQUIRE(snapshot.categories.set("tbank", "2026-11-01", "Транспорт", "3", month)); }
    CHECK_FALSE(serialize_cashback_snapshot("tbank", snapshot));
}

TEST_CASE("Saved automatic cashback rejects corrupt metadata schemas and category data") {
    QJsonObject month{{"month", "2026-10-01"}, {"selectionRequired", false},
        {"observedAt", "2026-10-29T12:00:00.000Z"},
        {"categories", QJsonArray{QJsonObject{{"name", "Кафе"}, {"percent", "5.25"}, {"upperBound", false}}}}};
    QJsonObject root{{"version", 1}, {"bank", "vtb"}, {"months", QJsonArray{month}}};
    SECTION("unknown schema") { root["version"] = 2; }
    SECTION("unknown root field") { root["extra"] = true; }
    SECTION("wrong bank") { root["bank"] = "alfa"; }
    SECTION("empty coverage") { root["months"] = QJsonArray{}; }
    SECTION("duplicate month") { root["months"] = QJsonArray{month, month}; }
    SECTION("non-adjacent months") {
        auto other = month;
        other["month"] = "2026-12-01";
        root["months"] = QJsonArray{month, other};
    }
    SECTION("invalid month day") { month["month"] = "2026-10-02"; root["months"] = QJsonArray{month}; }
    SECTION("missing observation") { month.remove("observedAt"); root["months"] = QJsonArray{month}; }
    SECTION("malformed observation") { month["observedAt"] = "not a timestamp"; root["months"] = QJsonArray{month}; }
    SECTION("timestamp without timezone") { month["observedAt"] = "2026-10-29T12:00:00.000"; root["months"] = QJsonArray{month}; }
    SECTION("invalid rate") {
        month["categories"] = QJsonArray{QJsonObject{{"name", "Кафе"}, {"percent", "100.01"}, {"upperBound", false}}};
        root["months"] = QJsonArray{month};
    }
    SECTION("unknown category field") {
        month["categories"] = QJsonArray{QJsonObject{{"name", "Кафе"}, {"percent", "5"}, {"upperBound", false}, {"extra", true}}};
        root["months"] = QJsonArray{month};
    }
    CHECK_FALSE(parse_cashback_snapshot("vtb", QJsonDocument(root).toJson()));
    CHECK_FALSE(parse_cashback_snapshot("vtb", {}));
    CHECK_FALSE(parse_cashback_snapshot("vtb", "not json"));
    CHECK_FALSE(parse_cashback_snapshot("vtb", QByteArray(128 * 1024 + 1, ' ')));
}

TEST_CASE("Automatic cashback state retains observations on failed reads and isolates banks") {
    using wealthtracker::app::CashbackState;
    const auto observed = QDateTime::fromString("2026-11-01T12:00:00Z", Qt::ISODate);
    CashbackSnapshot snapshot;
    snapshot.months.insert(QDate(2026,11,1));
    REQUIRE(snapshot.categories.set("ozon", "2026-11-01", "Кафе", "5", QDate(2026,11,1)));
    CashbackState state;
    state.accept("ozon", snapshot, "ready", observed);
    REQUIRE(state.find("ozon"));
    const auto rows = state.find("ozon")->rows("ozon", QDate(2026,11,1));
    state.setStatus("ozon", "failed");
    CHECK(state.find("ozon")->rows("ozon", QDate(2026,11,1)) == rows);
    CHECK(state.updatedAt("ozon") == observed);
    CHECK(state.status("ozon") == "failed");
    CHECK_FALSE(state.find("vtb"));
    CashbackState restored;
    restored.restore("ozon", state.snapshot("ozon"));
    CHECK(restored.updatedAt("ozon") == observed);
    CHECK(restored.find("ozon")->rows("ozon",QDate(2026,11,1)) == rows);
    state.clear("ozon");
    CHECK_FALSE(state.find("ozon"));
    CHECK_FALSE(state.updatedAt("ozon").isValid());
    CHECK(restored.find("ozon"));
}
