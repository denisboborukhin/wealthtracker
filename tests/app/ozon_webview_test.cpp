#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
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
    explicit Credentials(std::shared_ptr<Secrets> values) : values_(std::move(values)) {}
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString& key, const QByteArray& value) override {
        values_->insert(key, value);
        return {};
    }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        if (values_->contains(key)) return std::optional<QByteArray>{values_->value(key)};
        return std::optional<QByteArray>{};
    }
    std::expected<void, QString> remove(const QString& key) override {
        values_->remove(key);
        return {};
    }
private:
    std::shared_ptr<Secrets> values_;
};

struct SettingsScope {
    QTemporaryDir directory;
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QSettings::Format format = QSettings::defaultFormat();
    SettingsScope() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("OzonWebViewTests"));
        QCoreApplication::setApplicationName(QStringLiteral("OzonWebViewTests"));
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
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    timer.start(10);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}
}

TEST_CASE("Ozon validated balances reach capital and survive restart and failed refresh") {
    SettingsScope settings;
    const auto secrets = std::make_shared<Secrets>();
    const auto path = settings.directory.filePath("history.db");
    QString capital;
    QString updated;
    {
        AppController controller(std::make_unique<Credentials>(secrets),
            std::make_unique<QNetworkAccessManager>(), nullptr, path);
        controller.beginOzonLogin();
        REQUIRE(controller.ozonLoginActive());
        CHECK_FALSE(controller.ozonConnected());
        auto* view = static_cast<QWebView*>(controller.ozonWebView());
        REQUIRE(view);
        view->loadHtml(QStringLiteral(R"HTML(<html><script>
          globalThis.__wealthtrackerOzonSync = {status:'ready',cancelled:false,installmentDebt:5000,page:{widgets:[
            {type:'MAIN_ACCOUNT',version:1,status:'READY',data:{__typename:'MainAccountData',balanceCents:12345}},
            {type:'SAVINGS_B2C_SUMMARY',version:1,status:'READY',data:{__typename:'SavingsB2cSummaryData',title:'Накопления',activeProductsTotalAmount:{cents:20000}}},
            {type:'MKK_INSTALLMENT',version:1,status:'READY',data:{__typename:'MkkInstallmentData',mkkInstallmentStatus:'INSTALLMENT_LIMIT_PROGRESS'}}
          ]}};
        </script>Synthetic Ozon dashboard</html>)HTML"), controller.ozonLoginUrl());
        REQUIRE(waitFor([&] { return controller.ozonConnected(); }));
        CHECK_FALSE(controller.ozonLoginActive());
        CHECK(controller.accounts()->rowCount() == 3);
        CHECK(controller.netWorth() == QStringLiteral("273,45 ₽"));
        capital = controller.netWorth();
        updated = controller.netWorthUpdatedAt();
        REQUIRE_FALSE(updated.isEmpty());
        controller.setOzonAutoUpdate(false);
    }
    {
        AppController restored(std::make_unique<Credentials>(secrets),
            std::make_unique<QNetworkAccessManager>(), nullptr, path);
        CHECK(restored.ozonConnected());
        CHECK(restored.netWorth() == capital);
        CHECK(restored.netWorthUpdatedAt() == updated);
        CHECK_FALSE(restored.ozonAutoUpdate());
        REQUIRE(waitFor([&] { return restored.ozonSyncRunning(); }));
        auto* view = static_cast<QWebView*>(restored.ozonWebView());
        REQUIRE(view);
        view->loadHtml(QStringLiteral("<html><script>globalThis.__wealthtrackerOzonSync={status:'invalid',cancelled:false,reasonCode:'OZON-SCHEMA'}</script>Synthetic incompatible dashboard</html>"), restored.ozonLoginUrl());
        REQUIRE(waitFor([&] { return !restored.ozonSyncRunning(); }));
        CHECK(restored.ozonConnected());
        CHECK(restored.netWorth() == capital);
        CHECK(restored.netWorthUpdatedAt() == updated);
        restored.disconnectOzon();
        CHECK_FALSE(restored.ozonConnected());
        CHECK(restored.accounts()->rowCount() == 0);
    }
    AppController disconnected(std::make_unique<Credentials>(secrets),
        std::make_unique<QNetworkAccessManager>(), nullptr, path);
    CHECK_FALSE(disconnected.ozonConnected());
    CHECK_FALSE(disconnected.hasFinancialData());
}

TEST_CASE("Ozon keeps the login page open while its dashboard module is unavailable") {
    SettingsScope settings;
    AppController controller(nullptr, nullptr);
    controller.beginOzonLogin();
    auto* view = static_cast<QWebView*>(controller.ozonWebView());
    REQUIRE(view);
    auto url = controller.ozonLoginUrl();
    SECTION("dashboard module is not loaded yet") {}
    SECTION("verified SSO page is still authenticating") { url = QUrl("https://sso.ozon.ru/"); }
    view->loadHtml(QStringLiteral("<html><script>globalThis.__wealthtrackerOzonSync={status:'waiting',cancelled:false,attempt:999}</script>Synthetic login page</html>"), url);
    QEventLoop loop;
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);
    loop.exec();
    CHECK(controller.ozonLoginActive());
    CHECK_FALSE(controller.ozonConnected());
    CHECK(controller.accounts()->rowCount() == 0);
    controller.cancelOzonLogin();
    CHECK_FALSE(controller.ozonLoginActive());
}
