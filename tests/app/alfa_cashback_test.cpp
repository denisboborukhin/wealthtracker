#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtWebView/QWebView>
#include <QtWebView/QWebViewLoadingInfo>
#include <QtWebView/QWebViewSettings>

#include <memory>
#include <functional>

import wt.alfa_cashback;
import wt.cashback_sync;

namespace {
using namespace wealthtracker::app;

// QJSEngine does not implement async/await. Exercise the production script in
// the native browser engine that actually runs the bank integration.
class BrowserEngine {
public:
    struct Value {
        QJsonObject result;
        bool isError() const { return !result.value("ok").toBool(); }
        QString toString() const { return isError() ? result.value("error").toString() : result.value("value").toString(); }
        int toInt() const { return result.value("value").toInt(); }
        bool toBool() const { return result.value("value").toBool(); }
    };
    explicit BrowserEngine(QWebView* external=nullptr)
        : owned_(external ? nullptr : std::make_unique<QWebView>()),view_(external ? external : owned_.get()) {
        if (owned_) {
            view_->settings()->setAttribute(QWebViewSettings::WebAttribute::LocalStorageEnabled,false);
            view_->settings()->setAttribute(QWebViewSettings::WebAttribute::AllowFileAccess,false);
        }
        bool loaded=false;
        QEventLoop loop;
        QObject::connect(view_,&QWebView::loadingChanged,&loop,[&](const QWebViewLoadingInfo& info) {
            if (info.status()==QWebViewLoadingInfo::LoadStatus::Succeeded) {loaded=true;loop.quit();}
        });
        view_->loadHtml(QStringLiteral("<html>Alfa cashback fixture</html>"),QUrl("https://web.alfabank.ru/dashboard"));
        QTimer::singleShot(3000,&loop,&QEventLoop::quit);
        loop.exec();
        REQUIRE(loaded);
    }
    Value evaluate(const QString& code) {
        const auto encoded=QJsonDocument(QJsonArray{code}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
        const auto wrapped=QStringLiteral("(() => {try {return JSON.stringify({ok:true,value:(0,eval)(%1)})} catch(error) {return JSON.stringify({ok:false,error:String(error)})}})()")
            .arg(QString::fromUtf8(encoded));
        struct Result {bool completed=false;QString value;};
        auto state=std::make_shared<Result>();
        view_->runJavaScript(wrapped,[state](const QVariant& value) {state->value=value.toString();state->completed=true;});
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll,&QTimer::timeout,&loop,[&] {if (state->completed) loop.quit();});
        poll.start(5);
        QTimer::singleShot(3000,&loop,&QEventLoop::quit);
        if (!state->completed) loop.exec();
        REQUIRE(state->completed);
        return {QJsonDocument::fromJson(state->value.toUtf8()).object()};
    }
private:
    std::unique_ptr<QWebView> owned_;
    QWebView* view_;
};

void initialize(BrowserEngine& engine) {
    const auto result=engine.evaluate(QStringLiteral(R"JS(
      var globalThis=this
      globalThis.initialState={layoutData:{accounts:[{id:'fixture',amount:{value:'125.50',currency:'RUB'}}]}}
      globalThis.requests=[]
      globalThis.prefix=''
      globalThis.serverDate='Thu, 29 Oct 2026 12:00:00 GMT'
      globalThis.programs=[
        {id:1,date:'2026-10-01',confirmed:true,deeplink:'alfaonline://panel?basketOfferId=current'},
        {id:1,date:'ноябрь',confirmed:true,deeplink:'alfaonline://panel?options=%7B%22basketOfferId%22%3A%22next%22%7D'},
        {id:2,date:'not a month',confirmed:false,deeplink:'not a category basket'}]
      globalThis.detail={categoriesSection:{categories:[
        {title:'5% Супермаркеты',isConfirmed:true},
        {title:'99% Not confirmed',isConfirmed:false,isSelected:true,isPotential:true}]},
        paidCategoriesSection:{categories:[{title:'до 7,5% Кафе',isConfirmed:true}]}}
      const method=(service,path,respond)=>{
        const fn=(params,config)=>{
          requests.push({path:'/api/'+service+path,params:params,config:config})
          return Promise.resolve({data:respond(params),headers:{date:serverDate}})
        }
        fn.getPath=()=>prefix+'/'+service
        fn.apiDeclaration=params=>({url:path,method:'GET'})
        return fn
      }
      const accounts={getAccounts:method('v1/loyalty','/accounts',()=>({accounts:[
        {account:{typeId:'EE'}},{account:{typeId:'CC'}},{account:{typeId:'EE'}}]}))}
      const view={
        getPromotedPrograms:method('v1/loyalty-view','/promoted/programs',()=>({programs:programs})),
        getCategoriesV2:method('v1/loyalty-view','/v2/categories',()=>detail)}
      globalThis.apiView=view
      globalThis.newclick_loyalty_ui={get:()=>Promise.resolve(()=>{})}
      const requireModule=id=>{
        if (id===81812) return {e:()=>prefix,K:value=>{prefix=value}}
        if (id===29876) return {q:()=> 'api'}
        if (id===58481) return {h:accounts}
        if (id===8670) return {S:view}
        throw new Error('Unexpected module')
      }
      globalThis.webpackChunknewclick_loyalty_ui=[]
      webpackChunknewclick_loyalty_ui.push=function(entry){entry[2](requireModule);return Array.prototype.push.call(this,entry)}
    )JS"));
    REQUIRE_FALSE(result.isError());
}

QString run(BrowserEngine& engine) {
    const auto started=engine.evaluate(alfa_cashback_start_script(QDate(2026,10,29),"fixture"));
    INFO(started.toString().toStdString());
    REQUIRE_FALSE(started.isError());
    for(int i=0;i<64;++i) {
        QCoreApplication::processEvents();
        const auto result=engine.evaluate(cashback_projection_script("alfa","fixture"));
        REQUIRE_FALSE(result.isError());
        if (result.toString()!="loading") return result.toString();
    }
    return QStringLiteral("loading");
}
}

TEST_CASE("Alfa cashback discovers real basket IDs and reads only confirmed current and next categories") {
    BrowserEngine engine;
    initialize(engine);
    const auto raw=run(engine);
    INFO(raw.toStdString());
    const auto result=parse_cashback_projection("alfa",raw.toUtf8(),QDate(2026,10,29));
    REQUIRE(result);
    CHECK(result->months.size()==2);
    const auto rows=result->rows("alfa",QDate(2026,10,1));
    REQUIRE(rows.size()==2);
    CHECK(rows.front().toMap()["name"]=="Супермаркеты");
    CHECK(rows.front().toMap()["percent"]=="5 %");
    CHECK(rows.back().toMap()["percent"]=="до 7,5 %");
    CHECK(engine.evaluate("requests.length").toInt()==5);
    CHECK(engine.evaluate("requests.every(r=>r.config.adapter==='fetch'&&r.config.fetchOptions.redirect==='error')").toBool());
    CHECK(engine.evaluate("requests.filter(r=>r.path.endsWith('/v2/categories')).length").toInt()==2);
    CHECK(engine.evaluate("requests.every(r=>r.params.basketOfferId!=='undefined')").toBool());
}

TEST_CASE("Alfa cashback never promotes available monthly programs or draft selections") {
    BrowserEngine engine;
    initialize(engine);
    engine.evaluate("programs[1].confirmed=false");
    const auto result=parse_cashback_projection("alfa",run(engine).toUtf8(),QDate(2026,10,29));
    REQUIRE(result);
    CHECK(result->selectionRequired.contains(QDate(2026,11,1)));
    CHECK(result->rows("alfa",QDate(2026,11,1)).isEmpty());
    CHECK(engine.evaluate("requests.filter(r=>r.path.endsWith('/v2/categories')).length").toInt()==1);
}

TEST_CASE("Alfa cashback decodes rich display titles while keeping upper bound rates") {
    BrowserEngine engine;
    initialize(engine);
    engine.evaluate("detail.categoriesSection.categories[0].title='<b>5%</b>&nbsp;<span>Супермаркеты</span>'");
    const auto result=parse_cashback_projection("alfa",run(engine).toUtf8(),QDate(2026,10,29));
    REQUIRE(result);
    const auto rows=result->rows("alfa",QDate(2026,10,1));
    CHECK(rows.front().toMap()["name"]=="Супермаркеты");
    CHECK(rows.back().toMap()["percent"]=="до 7,5 %");
}

TEST_CASE("Alfa cashback rejects ambiguous rates dates identifiers and changed read methods") {
    BrowserEngine engine;
    initialize(engine);
    SECTION("ambiguous rate") {engine.evaluate("detail.categoriesSection.categories[0].title='5% или 10% Кафе'");}
    SECTION("active markup") {engine.evaluate("detail.categoriesSection.categories[0].title='<script>throw new Error()</script>5% Кафе'");}
    SECTION("unknown month") {engine.evaluate("programs[0].date='next'");}
    SECTION("unconfirmed schema") {engine.evaluate("delete detail.categoriesSection.categories[0].isConfirmed");}
    SECTION("changed method") {engine.evaluate("apiView.getCategoriesV2.apiDeclaration=()=>({url:'/accept',method:'PUT'})");}
    SECTION("bad basket ID") {engine.evaluate("programs[0].deeplink='alfaonline://panel?basketOfferId=../../other'");}
    SECTION("stale response") {engine.evaluate("serverDate='Wed, 29 Oct 2025 12:00:00 GMT'");}
    SECTION("wrong API origin") {engine.evaluate("apiView.getCategoriesV2.getPath=()=> 'https://example.com/api/v1/loyalty-view'");}
    const auto result=run(engine);
    CHECK_FALSE(parse_cashback_projection("alfa",result.toUtf8(),QDate(2026,10,29)));
    CHECK(engine.evaluate("requests.every(r=>r.path!=='/accept')").toBool());
}

TEST_CASE("Alfa balance refresh automatically publishes confirmed cashback categories") {
    struct SettingsScope {
        QTemporaryDir directory;
        QString organization=QCoreApplication::organizationName(),application=QCoreApplication::applicationName();
        QSettings::Format format=QSettings::defaultFormat();
        SettingsScope() {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
            QCoreApplication::setOrganizationName("AlfaCashbackTests");QCoreApplication::setApplicationName("AlfaCashbackTests");
        }
        ~SettingsScope() {
            QCoreApplication::setOrganizationName(organization);QCoreApplication::setApplicationName(application);
            QSettings::setDefaultFormat(format);
        }
    } settings;
    const auto wait=[&](const std::function<bool()>& ready) {
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll,&QTimer::timeout,&loop,[&] {if (ready()) loop.quit();});poll.start(10);
        QTimer::singleShot(5000,&loop,&QEventLoop::quit);if (!ready()) loop.exec();return ready();
    };
    QSettings{}.setValue("alfa/connected",true);
    AppController controller(nullptr,nullptr);
    REQUIRE(wait([&] {return controller.alfaSyncRunning();}));
    auto* view=static_cast<QWebView*>(controller.alfaWebView());
    REQUIRE(view);
    BrowserEngine engine(view);
    initialize(engine);
    const auto today=QDate::currentDate();
    const auto current=QDate(today.year(),today.month(),1);
    REQUIRE_FALSE(engine.evaluate(QStringLiteral("programs[0].date='%1';programs[1].date='%2';serverDate='%3'")
        .arg(current.toString(Qt::ISODate),current.addMonths(1).toString(Qt::ISODate),QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date))).isError());
    REQUIRE(wait([&] {return controller.cashbackBanks().at(1).toMap()["currentFromBank"].toBool();}));
    CHECK_FALSE(controller.alfaSyncRunning());
    const auto bank=controller.cashbackBanks().at(1).toMap();
    CHECK(bank["current"].toList().size()==2);
    CHECK_FALSE(bank["currentLastSync"].toString().isEmpty());
    controller.lock();
    CHECK(controller.cashbackBanks().isEmpty());
}
