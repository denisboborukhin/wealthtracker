#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QHash>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtWebView/QWebView>

#include <functional>

namespace {
using wealthtracker::app::AppController;

using Secrets = QHash<QString, QByteArray>;
class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    explicit Credentials(std::shared_ptr<Secrets> secrets) : secrets_(std::move(secrets)) {}
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString& key, const QByteArray& value) override {
        secrets_->insert(key, value);
        return {};
    }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        if (secrets_->contains(key)) return std::optional<QByteArray>{secrets_->value(key)};
        return std::optional<QByteArray>{};
    }
    std::expected<void, QString> remove(const QString& key) override {
        secrets_->remove(key);
        return {};
    }
private:
    std::shared_ptr<Secrets> secrets_;
};

struct SettingsScope {
    QTemporaryDir directory;
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QSettings::Format format = QSettings::defaultFormat();
    SettingsScope() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("CashbackWebViewTests"));
        QCoreApplication::setApplicationName(QStringLiteral("CashbackWebViewTests"));
    }
    ~SettingsScope() {
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setApplicationName(application);
        QSettings::setDefaultFormat(format);
    }
};

bool waitFor(const std::function<bool()>& ready) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    poll.start(10);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

void evaluate(QWebView* view, const QString& script) {
    bool completed = false;
    view->runJavaScript(script, [&](const QVariant&) { completed = true; });
    REQUIRE(waitFor([&] { return completed; }));
}

QVariantMap vtb(const AppController& controller) {
    return controller.cashbackBanks().at(2).toMap();
}

QString fixture() {
    const auto today = QDate::currentDate();
    const auto current = QDate(today.year(),today.month(),1);
    const QLocale russian(QLocale::Russian);
    const auto month = [&](const QDate& day, bool choice) {
        return QJsonObject{{"month",russian.standaloneMonthName(day.month())}, {"choice",choice},
            {"categories",QJsonArray{QJsonObject{{"displayName","Кафе"},{"displayPercent","5%"}}}}};
    };
    const auto data = QJsonDocument(QJsonObject{{"categories",QJsonArray{
        month(current.addMonths(-1),false),month(current,false),month(current.addMonths(1),true)}}})
        .toJson(QJsonDocument::Compact);
    return QStringLiteral(R"HTML(<html><script>
      globalThis.cashbackData = %1
      globalThis.cashbackFail = false
      globalThis.cashbackHold = false
      globalThis.mf_1488_core = {get: () => Promise.resolve(() => ({http:{get:(path) => {
        if (path.endsWith('/portfolios/active')) return Promise.resolve({data:{portfolio:{accounts:[
          {publicId:'fixture-account',name:'Fixture',balance:{sum:'125.50',currency:'RUB'}}
        ]}}})
        if (path.endsWith('/financial/summary')) return Promise.resolve({data:{balanceResponse:[]}})
        if (path !== '/msa/api-gw/private/loya/loya-web/v2/bonus/month/categories') throw new Error('Unexpected route')
        if (cashbackFail) return Promise.reject({status:503})
        const response = {data:cashbackData,headers:{date:'%2'}}
        return cashbackHold ? new Promise(resolve => { globalThis.finishCashback = () => resolve(response) })
          : Promise.resolve(response)
      }}}))}
    </script>Cashback fixture</html>)HTML").arg(QString::fromUtf8(data),
        QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date));
}
}

TEST_CASE("VTB balance refresh automatically reads cashback and preserves choices on failure and cancellation") {
    SettingsScope settings;
    QSettings{}.setValue(QStringLiteral("vtb/connected"),true);
    AppController controller(nullptr,nullptr);
    const auto month = QDate::currentDate().toString(QStringLiteral("yyyy-MM-01"));
    REQUIRE(controller.saveCashbackCategory("vtb",month,"Manual fallback","3"));
    REQUIRE(waitFor([&] { return controller.vtbSyncRunning(); }));
    auto* view = static_cast<QWebView*>(controller.vtbWebView());
    REQUIRE(view);
    view->loadHtml(fixture(),controller.vtbLoginUrl());
    REQUIRE(waitFor([&] { return vtb(controller)["currentFromBank"].toBool(); }));
    CHECK_FALSE(controller.vtbSyncRunning());
    const auto initial = vtb(controller);
    REQUIRE(initial["current"].toList().size() == 1);
    CHECK(initial["current"].toList().front().toMap()["name"] == "Кафе");
    CHECK(initial["nextSelectionRequired"].toBool());
    CHECK(initial["next"].toList().isEmpty());
    CHECK_FALSE(initial["cashbackLastSync"].toString().isEmpty());

    evaluate(view,QStringLiteral("cashbackFail = true"));
    REQUIRE(controller.synchronizeVtb());
    REQUIRE(waitFor([&] { return !controller.vtbSyncRunning() &&
        !vtb(controller)["cashbackRunning"].toBool() &&
        vtb(controller)["cashbackStatus"] != initial["cashbackStatus"]; }));
    CHECK(vtb(controller)["current"] == initial["current"]);
    CHECK(vtb(controller)["cashbackLastSync"] == initial["cashbackLastSync"]);

    evaluate(view,QStringLiteral("cashbackFail = false; cashbackHold = true"));
    REQUIRE(controller.synchronizeVtb());
    REQUIRE(waitFor([&] { return !controller.vtbSyncRunning() && vtb(controller)["cashbackRunning"].toBool(); }));
    controller.lock();
    CHECK(controller.cashbackBanks().isEmpty());
    evaluate(view,QStringLiteral("if (globalThis.finishCashback) finishCashback()"));
    REQUIRE(controller.unlock());
    CHECK_FALSE(vtb(controller)["cashbackRunning"].toBool());
    CHECK(vtb(controller)["current"] == initial["current"]);
    CHECK(vtb(controller)["cashbackLastSync"] == initial["cashbackLastSync"]);
    controller.loadDemoData();
    CHECK(controller.cashbackBanks().isEmpty());
}

TEST_CASE("Saved cashback and capital survive restart and failed refresh until explicit disconnect") {
    SettingsScope settings;
    QSettings{}.setValue(QStringLiteral("vtb/connected"), true);
    const auto secrets = std::make_shared<Secrets>();
    const auto path = settings.directory.filePath("history.db");
    QVariantMap saved;
    QString capital;
    QString updated;
    {
        AppController controller(std::make_unique<Credentials>(secrets),
            std::make_unique<QNetworkAccessManager>(), nullptr, path);
        REQUIRE(waitFor([&] { return controller.vtbSyncRunning(); }));
        auto* view = static_cast<QWebView*>(controller.vtbWebView());
        REQUIRE(view);
        view->loadHtml(fixture(), controller.vtbLoginUrl());
        REQUIRE(waitFor([&] { return vtb(controller)["currentFromBank"].toBool(); }));
        saved = vtb(controller);
        capital = controller.netWorth();
        updated = controller.netWorthUpdatedAt();
        REQUIRE(capital != QStringLiteral("0,00 ₽"));
        REQUIRE_FALSE(updated.isEmpty());
    }
    {
        AppController restored(std::make_unique<Credentials>(secrets),
            std::make_unique<QNetworkAccessManager>(), nullptr, path);
        CHECK(restored.netWorth() == capital);
        CHECK(restored.netWorthUpdatedAt() == updated);
        CHECK(restored.hasFinancialData());
        CHECK(vtb(restored)["current"] == saved["current"]);
        CHECK(vtb(restored)["nextSelectionRequired"] == saved["nextSelectionRequired"]);
        CHECK(vtb(restored)["currentLastSync"] == saved["currentLastSync"]);
        REQUIRE(waitFor([&] { return restored.vtbSyncRunning(); }));
        auto* view = static_cast<QWebView*>(restored.vtbWebView());
        REQUIRE(view);
        view->loadHtml(QStringLiteral("<html><script>globalThis.mf_1488_core={get:()=>Promise.resolve(()=>({http:{get:()=>Promise.reject({status:403})}}))}</script></html>"), restored.vtbLoginUrl());
        REQUIRE(waitFor([&] { return !restored.vtbSyncRunning(); }));
        CHECK(restored.netWorth() == capital);
        CHECK(restored.netWorthUpdatedAt() == updated);
        CHECK(vtb(restored)["current"] == saved["current"]);
        CHECK(vtb(restored)["currentLastSync"] == saved["currentLastSync"]);
        restored.disconnectVtb();
    }
    AppController disconnected(std::make_unique<Credentials>(secrets),
        std::make_unique<QNetworkAccessManager>(), nullptr, path);
    CHECK_FALSE(disconnected.hasFinancialData());
    CHECK(disconnected.netWorthUpdatedAt().isEmpty());
    CHECK_FALSE(vtb(disconnected)["currentFromBank"].toBool());
}
