#include "app/CredentialBackend.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QVector>
#include <QTemporaryDir>

import wt.connection_restore;
import wt.connectors;
import wt.plugin_service;

namespace {
class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return available; }
    std::expected<void, QString> put(const QString& key, const QByteArray& secret) override {
        ++writes;
        if (failWrites) return std::unexpected(QStringLiteral("Fixture write failed"));
        values[key] = secret;
        return {};
    }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        ++reads;
        if (values.contains(key)) return std::optional<QByteArray>{values[key]};
        return std::optional<QByteArray>{};
    }
    std::expected<void, QString> remove(const QString& key) override {
        values.remove(key);
        return {};
    }
    QHash<QString, QByteArray> values;
    bool available = true;
    bool failWrites = false;
    int reads = 0;
    int writes = 0;
};
}

TEST_CASE("Connection restoration reads saved facts without requiring live bank access") {
    using namespace wealthtracker::app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    settings.setValue("alfa/connected", true);
    settings.setValue("ozon/connected", true);
    Credentials credentials;
    credentials.values["investment-key"] = QByteArray{};
    credentials.values["tbank-retail-session"] = "synthetic-session-123";
    const auto restored = restoreConnections(&credentials, settings,
        {{"t-invest", "investment-key"}, {"missing", "missing-key"}});
    CHECK(restored.apiConnectionIds == QSet<QString>{"t-invest"});
    CHECK(restored.tbankConnected);
    CHECK(restored.tbankSessionSaved);
    CHECK(restored.alfaConnected);
    CHECK_FALSE(restored.vtbConnected);
    CHECK(restored.ozonConnected);
    CHECK_FALSE(restored.plugin);
    credentials.values["tbank-retail-session"] = "invalid";
    const auto invalid = restoreConnections(&credentials, settings, {});
    CHECK_FALSE(invalid.tbankConnected);
    CHECK_FALSE(invalid.tbankSessionSaved);
    CHECK_FALSE(credentials.values.contains("tbank-retail-session"));
    credentials.available = false;
    credentials.reads = 0;
    const auto unavailable = restoreConnections(&credentials, settings, {{"t-invest", "investment-key"}});
    CHECK(unavailable.apiConnectionIds.isEmpty());
    CHECK(unavailable.alfaConnected);
    CHECK(credentials.reads == 0);
}

TEST_CASE("Legacy connection migration retains retryable state until durable replacement") {
    using namespace wealthtracker::app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("tbank-retail.js");
    QFile bundle(path);
    REQUIRE(bundle.open(QIODevice::WriteOnly));
    REQUIRE(bundle.write("global.main = async function () {};") > 0);
    bundle.close();
    REQUIRE(QFile::copy(QStringLiteral(WEALTHTRACKER_SOURCE_DIR "/tests/fixtures/preferences.xml"),
                        directory.filePath("preferences.xml")));
    const auto descriptor = wealthtracker::connectors::inspectZenPluginBundle(path);
    REQUIRE(descriptor);
    const auto key = pluginStateCredentialKey(*descriptor);
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    settings.setValue("zenPlugin/bundlePath", path);
    settings.setValue("zenPlugin/lastSyncAt", "2026-10-01T12:00:00Z");
    settings.setValue("zenPlugin/autoUpdate", false);
    Credentials credentials;
    const auto legacy = QJsonDocument(QJsonObject{{"pluginData",
        QJsonObject{{"sessionId", "synthetic-session-123"}}}}).toJson();
    credentials.values[key] = legacy;
    SECTION("Failed writes retain the previous saved-session distinction") {
        credentials.values["tbank-retail-session"] = "previous-session-123";
        credentials.failWrites = true;
        const auto failed = restoreConnections(&credentials, settings, {});
        CHECK_FALSE(failed.tbankConnected);
        CHECK(failed.tbankSessionSaved);
        CHECK(credentials.values[key] == legacy);
        CHECK(settings.contains("zenPlugin/bundlePath"));
        credentials.failWrites = false;
    }
    SECTION("Malformed state remains available for retry") {
        credentials.values[key] = "not JSON";
        const auto failed = restoreConnections(&credentials, settings, {});
        CHECK_FALSE(failed.tbankConnected);
        CHECK(credentials.values[key] == QByteArrayLiteral("not JSON"));
        CHECK(credentials.writes == 0);
        CHECK(settings.contains("zenPlugin/bundlePath"));
        credentials.values[key] = legacy;
    }
    SECTION("Oversized state remains available for retry") {
        credentials.values[key] = QByteArray(1024 * 1024 + 1, 'x');
        CHECK_FALSE(restoreConnections(&credentials, settings, {}).tbankConnected);
        CHECK(credentials.values[key].size() == 1024 * 1024 + 1);
        CHECK(credentials.writes == 0);
        CHECK(settings.contains("zenPlugin/bundlePath"));
        credentials.values[key] = legacy;
    }
    const auto migrated = restoreConnections(&credentials, settings, {});
    CHECK(migrated.tbankConnected);
    CHECK_FALSE(migrated.plugin);
    CHECK(credentials.values["tbank-retail-session"] == QByteArrayLiteral("synthetic-session-123"));
    CHECK_FALSE(credentials.values.contains(key));
    CHECK_FALSE(settings.contains("zenPlugin/bundlePath"));
    CHECK(settings.value("tbank/lastSyncAt").toString() == "2026-10-01T12:00:00Z");
    CHECK_FALSE(settings.value("tbank/autoUpdate").toBool());
}

TEST_CASE("External plugin restoration returns its inspected configuration") {
    using namespace wealthtracker::app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    const auto bundlePath = directory.filePath("zen_plugin_fixture.cjs");
    REQUIRE(QFile::copy(QStringLiteral(WEALTHTRACKER_SOURCE_DIR "/tests/fixtures/zen_plugin_fixture.cjs"), bundlePath));
    REQUIRE(QFile::copy(QStringLiteral(WEALTHTRACKER_SOURCE_DIR "/tests/fixtures/preferences.xml"),
                        directory.filePath("preferences.xml")));
    const auto descriptor = wealthtracker::connectors::inspectZenPluginBundle(bundlePath);
    REQUIRE(descriptor);
    settings.setValue("zenPlugin/bundlePath", bundlePath);
    settings.setValue("zenPlugin/institution", "Fixture institution");
    CHECK_FALSE(restoreConnections(nullptr, settings, {}).plugin);
    settings.setValue("zenPlugin/bundleDigest", descriptor->bundleDigest);
    const auto restored = restoreConnections(nullptr, settings, {});
    REQUIRE(restored.plugin);
    CHECK(restored.plugin->pluginId == "zen_plugin_fixture");
    CHECK(restored.pluginInstitution == "Fixture institution");
    CHECK(settings.contains("zenPlugin/bundlePath"));
    QFile changed(bundlePath);
    REQUIRE(changed.open(QIODevice::Append));
    REQUIRE(changed.write("\n// Changed bundle\n") > 0);
    changed.close();
    CHECK_FALSE(restoreConnections(nullptr, settings, {}).plugin);
}
