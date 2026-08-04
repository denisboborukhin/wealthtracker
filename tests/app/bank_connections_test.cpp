#include "app/AppController.hpp"
#include "app/FinanceModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

import wt.read_only_resource;
import wt.tbank_auth;
import wt.tbank_session;

namespace {

class MemoryCredentialBackend final
    : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }

    std::expected<void, QString> put(const QString& key,
                                     const QByteArray& secret) override {
        values.insert(key, secret);
        return {};
    }

    std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) override {
        const auto value = values.constFind(key);
        if (value == values.cend()) {
            return std::optional<QByteArray>{};
        }
        return std::optional<QByteArray>{*value};
    }

    std::expected<void, QString> remove(const QString& key) override {
        values.remove(key);
        return {};
    }

    QHash<QString, QByteArray> values;
};

bool connectionState(QAbstractItemModel* model, const QString& connectorId) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, ConnectionModel::ConnectedRole).toBool();
        }
    }
    return false;
}

QVariant connectionValue(QAbstractItemModel* model, const QString& connectorId,
                         const int role) {
    using wealthtracker::app::ConnectionModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (model->data(index, ConnectionModel::ConnectorIdRole).toString() ==
            connectorId) {
            return model->data(index, role);
        }
    }
    return {};
}

bool waitUntil(const std::function<bool()>& predicate,
               const int timeoutMilliseconds = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    }
    return predicate();
}

class ZenSettingsCleanup final {
public:
    ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
    ~ZenSettingsCleanup() {
        QSettings{}.remove(QStringLiteral("zenPlugin"));
    }
};

}  // namespace

TEST_CASE("new installations start with empty data and optional demo mode") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(controller.categories()->rowCount() == 0);
    CHECK(controller.holdings()->rowCount() == 0);
    CHECK(controller.netWorth() == QStringLiteral("0 ₽"));

    controller.loadDemoData();
    CHECK(controller.demoMode());
    CHECK(controller.accounts()->rowCount() > 0);

    controller.clearDemoData();
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.accounts()->rowCount() == 0);
}

TEST_CASE("T-Bank runtime bundle upgrades an existing read-only resource") {
    using wealthtracker::app::install_read_only_resource;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto destination = directory.filePath(
        QStringLiteral("tbank-retail.cjs"));
    const QByteArray firstContents{"first embedded bundle"};
    const QByteArray updatedContents{"updated embedded bundle"};

    const auto firstInstall =
        install_read_only_resource(destination, firstContents);
    REQUIRE(firstInstall);
    CHECK(*firstInstall);
    CHECK_FALSE(QFileInfo(destination).permissions()
                & QFileDevice::WriteOwner);

    const auto unchangedInstall =
        install_read_only_resource(destination, firstContents);
    REQUIRE(unchangedInstall);
    CHECK_FALSE(*unchangedInstall);

    const auto updatedInstall =
        install_read_only_resource(destination, updatedContents);
    REQUIRE(updatedInstall);
    CHECK(*updatedInstall);
    QFile installed(destination);
    REQUIRE(installed.open(QIODevice::ReadOnly));
    CHECK(installed.readAll() == updatedContents);
    installed.close();
    CHECK_FALSE(QFileInfo(destination).permissions()
                & QFileDevice::WriteOwner);
    (void)QFile::setPermissions(
        destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

TEST_CASE("T-Bank is bundled while other retail banks remain adapter slots") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    const auto tbankAvailable = connectionValue(
        controller.bankConnections(), QStringLiteral("tbank-retail"),
        wealthtracker::app::ConnectionModel::AvailableRole).toBool();
    CHECK(tbankAvailable
          == !QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty());
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("tbank-retail")));
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::MethodRole)
              .toString()
              .contains(QStringLiteral("Системный браузер")));
    CHECK(connectionValue(controller.bankConnections(),
                          QStringLiteral("tbank-retail"),
                          wealthtracker::app::ConnectionModel::StatusRole)
              .toString()
              .contains(QStringLiteral("вход")));
    controller.describeBankConnection(QStringLiteral("tbank-retail"));
    CHECK(controller.connectionMessage().contains(
        QStringLiteral("идентификатор API-сессии")));
    CHECK(controller.connectionMessage().contains(
        QStringLiteral("не читает cookies")));

    for (const auto& connectorId :
         {QStringLiteral("alfa-retail"), QStringLiteral("sber-retail"),
          QStringLiteral("vtb-retail")}) {
        CHECK_FALSE(connectionValue(controller.bankConnections(), connectorId,
                                    wealthtracker::app::ConnectionModel::
                                        AvailableRole)
                        .toBool());
        CHECK(connectionValue(controller.bankConnections(), connectorId,
                              wealthtracker::app::ConnectionModel::MethodRole)
                  .toString()
                  .contains(QStringLiteral("Телефон + код")));
        CHECK(connectionValue(controller.bankConnections(), connectorId,
                              wealthtracker::app::ConnectionModel::StatusRole)
                  .toString()
                  .contains(QStringLiteral("адаптер")));
    }
}

TEST_CASE("T-Bank runtime bundle is embedded in the application") {
    QFile bundle(QStringLiteral(
        ":/org/wealthtracker/runtime/bundles/tbank-retail/tbank-retail.cjs"));
    QFile preferences(QStringLiteral(
        ":/org/wealthtracker/runtime/bundles/tbank-retail/preferences.xml"));
    REQUIRE(bundle.open(QIODevice::ReadOnly));
    REQUIRE(preferences.open(QIODevice::ReadOnly));
    const auto source = bundle.readAll();
    CHECK(source.contains(QByteArrayLiteral("accounts_light_ib")));
    CHECK(source.contains(QByteArrayLiteral("apiGet('operations'")));
    CHECK(preferences.readAll().contains(QByteArrayLiteral("startDate")));
}

TEST_CASE("T-Bank login UI delegates authentication to the system browser") {
    QFile page(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/ui/qml/pages/ConnectionsPage.qml"));
    REQUIRE(page.open(QIODevice::ReadOnly));
    const auto source = page.readAll();
    CHECK(source.contains(QByteArrayLiteral("App.beginTBankLogin()")));
    CHECK(source.contains(QByteArrayLiteral("App.openTBankLoginPage()")));
    CHECK(source.contains(QByteArrayLiteral("App.checkTBankLogin()")));
    CHECK(source.contains(QByteArrayLiteral("системном браузере")));
    CHECK(source.contains(QByteArrayLiteral("cookies остаются в браузере")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("QtWebEngine")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("WebEngineView")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("WebEngineProfile")));

    QFile controllerSource(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/src/app/AppController.cpp"));
    REQUIRE(controllerSource.open(QIODevice::ReadOnly));
    const auto controller = controllerSource.readAll();
    CHECK(controller.contains(QByteArrayLiteral(
        "QNetworkRequest::CookieLoadControlAttribute")));
    CHECK(controller.contains(QByteArrayLiteral(
        "QNetworkRequest::CookieSaveControlAttribute")));
    CHECK(controller.contains(QByteArrayLiteral(
        "QNetworkRequest::Manual")));
    CHECK(controller.contains(QByteArrayLiteral(
        "QDesktopServices::openUrl")));
    CHECK_FALSE(controller.contains(QByteArrayLiteral("cookieStore()")));
}

TEST_CASE("T-Bank API session identifiers reject unsafe values") {
    using wealthtracker::app::is_valid_tbank_session_id;
    CHECK(is_valid_tbank_session_id("observed-session-123456"));
    CHECK_FALSE(is_valid_tbank_session_id("session;injected-value"));
    CHECK_FALSE(is_valid_tbank_session_id("session?query-injected"));
    CHECK_FALSE(is_valid_tbank_session_id("session&query-injected"));
    CHECK_FALSE(is_valid_tbank_session_id("session\nheader-value"));
    CHECK_FALSE(is_valid_tbank_session_id("too-short"));
}

TEST_CASE("T-Bank external authorization uses an explicit cookie-free API session") {
    using wealthtracker::app::TBankAccessLevel;
    using wealthtracker::app::parse_tbank_access_level;
    using wealthtracker::app::parse_tbank_session_start;
    using wealthtracker::app::tbank_authorization_start_url;
    using wealthtracker::app::tbank_ping_url;
    using wealthtracker::app::tbank_session_start_url;
    using wealthtracker::app::validate_tbank_authorization_redirect;

    const QByteArray sessionId{
        "0123456789abcdef0123456789abcdef.auth-session-host"};
    const auto parsedSession = parse_tbank_session_start(
        QByteArrayLiteral("{\"resultCode\":\"OK\",\"payload\":\"")
        + sessionId + QByteArrayLiteral("\"}"));
    REQUIRE(parsedSession);
    CHECK(*parsedSession == sessionId);

    const auto startUrl = tbank_session_start_url();
    CHECK(startUrl.scheme() == QStringLiteral("https"));
    CHECK(startUrl.host() == QStringLiteral("www.tbank.ru"));
    CHECK(startUrl.path() == QStringLiteral("/api/common/v1/session"));
    CHECK(QUrlQuery(startUrl).queryItemValue(QStringLiteral("origin"))
          == QStringLiteral("web,ib5,platform"));

    const auto authorizationUrl = tbank_authorization_start_url(sessionId);
    REQUIRE(authorizationUrl);
    CHECK(authorizationUrl->host() == QStringLiteral("www.tbank.ru"));
    CHECK(authorizationUrl->path()
          == QStringLiteral("/api/common/v1/session/authorize/"));
    const QUrlQuery authorizationQuery(*authorizationUrl);
    CHECK(authorizationQuery.queryItemValue(QStringLiteral("sessionid"))
          == QString::fromLatin1(sessionId));
    CHECK(authorizationQuery.queryItemValue(QStringLiteral("complete_uri"))
          == QStringLiteral("https://www.tbank.ru/auth/"));
    CHECK_FALSE(authorizationQuery.hasQueryItem(QStringLiteral("cookie")));

    QUrl identityUrl(QStringLiteral("https://id.tbank.ru/auth/authorize"));
    QUrlQuery identityQuery;
    identityQuery.addQueryItem(QStringLiteral("state"),
                               QString(96, QLatin1Char('s')));
    identityQuery.addQueryItem(QStringLiteral("client_id"),
                               QStringLiteral("portal-api"));
    identityQuery.addQueryItem(QStringLiteral("response_type"),
                               QStringLiteral("code"));
    identityQuery.addQueryItem(QStringLiteral("redirect_uri"),
                               QStringLiteral("https://www.tbank.ru/auth/"));
    identityQuery.addQueryItem(QStringLiteral("code_challenge"),
                               QString(43, QLatin1Char('c')));
    identityQuery.addQueryItem(QStringLiteral("code_challenge_method"),
                               QStringLiteral("S256"));
    identityUrl.setQuery(identityQuery);
    REQUIRE(validate_tbank_authorization_redirect(identityUrl));
    const auto encodedIdentityUrl = QUrl::fromEncoded(
        QByteArrayLiteral("https://id.tbank.ru/auth/authorize?state=")
        + QByteArray(96, 's')
        + QByteArrayLiteral(
            "&redirect_uri=https%3A%2F%2Fwww.tbank.ru%2Fauth%2F"
            "&client_id=portal-api&code_challenge=")
        + QByteArray(43, 'c')
        + QByteArrayLiteral(
            "&response_type=code&code_challenge_method=S256"));
    REQUIRE(validate_tbank_authorization_redirect(encodedIdentityUrl));
    auto lookalikeIdentityUrl = identityUrl;
    lookalikeIdentityUrl.setHost(QStringLiteral("id.tbank.ru.evil.example"));
    CHECK_FALSE(validate_tbank_authorization_redirect(lookalikeIdentityUrl));
    auto wrongCallbackUrl = identityUrl;
    QUrlQuery wrongCallbackQuery(wrongCallbackUrl);
    wrongCallbackQuery.removeAllQueryItems(QStringLiteral("redirect_uri"));
    wrongCallbackQuery.addQueryItem(QStringLiteral("redirect_uri"),
                                    QStringLiteral("https://evil.example/"));
    wrongCallbackUrl.setQuery(wrongCallbackQuery);
    CHECK_FALSE(validate_tbank_authorization_redirect(wrongCallbackUrl));
    auto duplicateCallbackUrl = identityUrl;
    QUrlQuery duplicateCallbackQuery(duplicateCallbackUrl);
    duplicateCallbackQuery.addQueryItem(
        QStringLiteral("redirect_uri"),
        QStringLiteral("https://www.tbank.ru/auth/"));
    duplicateCallbackUrl.setQuery(duplicateCallbackQuery);
    CHECK_FALSE(validate_tbank_authorization_redirect(duplicateCallbackUrl));

    const auto pingUrl = tbank_ping_url(sessionId);
    REQUIRE(pingUrl);
    CHECK(pingUrl->path() == QStringLiteral("/api/common/v1/ping"));
    CHECK(QUrlQuery(*pingUrl).queryItemValue(QStringLiteral("sessionid"))
          == QString::fromLatin1(sessionId));

    const auto anonymous = parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"ANONYMOUS\"}}"));
    REQUIRE(anonymous);
    CHECK(*anonymous == TBankAccessLevel::anonymous);
    const auto client = parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"CLIENT\"}}"));
    REQUIRE(client);
    CHECK(*client == TBankAccessLevel::client);

    CHECK_FALSE(parse_tbank_session_start(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":\"too-short\"}")));
    CHECK_FALSE(tbank_authorization_start_url(
        QByteArrayLiteral("bad;session")));
    CHECK_FALSE(parse_tbank_session_start(QByteArray::fromHex(
        "7b22726573756c74436f6465223a224f4b222c227061796c6f6164223a22"
        "d181d0b5d181d181d0b8d18f2d30313233343536373839616263646566227d")));
    CHECK_FALSE(parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"OK\",\"payload\":{\"accessLevel\":\"ADMIN\"}}")));
    CHECK_FALSE(parse_tbank_access_level(QByteArrayLiteral(
        "{\"resultCode\":\"ERROR\"}")));
}

TEST_CASE("official bank tokens have independent credential state") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    REQUIRE(controller.saveBankToken(QStringLiteral("alfa-api"),
                                     QStringLiteral("alfa-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("tbank-business"),
                                     QStringLiteral("tbank-token-valid")));
    REQUIRE(controller.saveBankToken(QStringLiteral("modulbank"),
                                     QStringLiteral("sandboxtoken")));

    CHECK(stored->values.value(QStringLiteral("alfa-b2c-readonly")) ==
          QByteArrayLiteral("alfa-token-valid"));
    CHECK(stored->values.value(QStringLiteral("tbank-business-readonly")) ==
          QByteArrayLiteral("tbank-token-valid"));
    CHECK(stored->values.value(QStringLiteral("modulbank-readonly")) ==
          QByteArrayLiteral("sandboxtoken"));
    CHECK_FALSE(controller.syncStatus().contains(
        QStringLiteral("sandboxtoken")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("tbank-business")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));

    controller.removeBankToken(QStringLiteral("alfa-api"));
    CHECK_FALSE(
        stored->values.contains(QStringLiteral("alfa-b2c-readonly")));
    CHECK_FALSE(connectionState(controller.bankConnections(),
                                QStringLiteral("alfa-api")));
    CHECK(connectionState(controller.bankConnections(),
                          QStringLiteral("modulbank")));
}

TEST_CASE("application runs a selected local bundle through the embedded host") {
    if (QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty()) {
        SKIP("Node.js is not installed");
    }
    const ZenSettingsCleanup cleanup;
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);
    const auto baselineCredentialCount = stored->values.size();
    REQUIRE(stored->values.contains(QStringLiteral("database-key")));
    const auto fixture = QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR
        "/tests/fixtures/zen_plugin_fixture.cjs");

    REQUIRE(controller.inspectZenPluginBundle(
        QUrl::fromLocalFile(fixture)));
    CHECK(controller.zenPluginId()
          == QStringLiteral("zen_plugin_fixture"));
    controller.setZenPluginAutoUpdate(true);
    CHECK(controller.zenPluginAutoUpdate());
    CHECK(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));
    REQUIRE(controller.startZenPlugin(
        QStringLiteral("Fixture Bank"),
        {{QStringLiteral("login"), QStringLiteral("fixture")},
         {QStringLiteral("password"),
          QStringLiteral("session-only-secret")},
         {QStringLiteral("startDate"),
          QStringLiteral("2026-07-01T00:00:00.000Z")}}));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginPrompt().isEmpty();
    }));
    REQUIRE(controller.answerZenPluginPrompt(
        QStringLiteral("123456")));
    REQUIRE(waitUntil([&controller] {
        return !controller.zenPluginRunning();
    }));
    CHECK(controller.accounts()->rowCount() == 1);
    CHECK(controller.transactions()->rowCount() == 1);
    CHECK_FALSE(controller.demoMode());
    CHECK(controller.syncStatus().contains(
        QStringLiteral("Fixture Bank")));
    CHECK(stored->values.size() == baselineCredentialCount + 1);
    CHECK(controller.zenPluginLastSync() !=
          QStringLiteral("Нет успешного обновления"));
    CHECK_FALSE(controller.zenPluginNextSync().contains(
        QStringLiteral("первого успешного запуска")));

    const auto savedPreferences =
        QSettings{}.value(QStringLiteral("zenPlugin/preferences"))
            .toByteArray();
    CHECK(savedPreferences.contains("fixture"));
    CHECK(savedPreferences.contains("startDate"));
    CHECK_FALSE(savedPreferences.contains("session-only-secret"));
    CHECK_FALSE(savedPreferences.contains("password"));
    CHECK_FALSE(controller.synchronizeZenPlugin());
    CHECK(controller.syncStatus().contains(QStringLiteral("Пароль")));

    controller.removeZenPlugin();
    CHECK(controller.accounts()->rowCount() == 0);
    CHECK(controller.transactions()->rowCount() == 0);
    CHECK(stored->values.size() == baselineCredentialCount);
    CHECK(stored->values.contains(QStringLiteral("database-key")));
    CHECK(controller.zenPluginId().isEmpty());
    CHECK_FALSE(controller.zenPluginAutoUpdate());
}

TEST_CASE("bank connection state rejects malformed and unknown tokens") {
    auto credentials = std::make_unique<MemoryCredentialBackend>();
    auto* stored = credentials.get();
    wealthtracker::app::AppController controller(std::move(credentials),
                                                  nullptr);

    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("modulbank"), QStringLiteral("short")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("alfa-api"), QStringLiteral("token with spaces")));
    CHECK_FALSE(controller.saveBankToken(
        QStringLiteral("unknown-bank"), QStringLiteral("valid-token")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("modulbank-readonly")));
    CHECK_FALSE(stored->values.contains(
        QStringLiteral("alfa-b2c-readonly")));
}
