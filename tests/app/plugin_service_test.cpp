#include "app/CredentialBackend.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

import wt.plugin_service;
import wt.connectors;

namespace {
class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }
    std::expected<void,QString> put(const QString& key,const QByteArray& bytes) override {
        if (failWrites) return std::unexpected(QStringLiteral("Fixture write failed"));
        values[key] = bytes; return {};
    }
    std::expected<std::optional<QByteArray>,QString> get(const QString& key) override {
        if (!values.contains(key)) return std::optional<QByteArray>{};
        return std::optional<QByteArray>{values[key]};
    }
    std::expected<void,QString> remove(const QString& key) override { values.remove(key); return {}; }
    QHash<QString,QByteArray> values;
    bool failWrites = false;
};
}

TEST_CASE("Plugin service prepares private credentials but persists only safe preferences") {
    using namespace wealthtracker;
    app::PluginService service;
    Credentials credentials;
    const auto descriptor = connectors::inspectZenPluginBundle(QStringLiteral(
        WEALTHTRACKER_SOURCE_DIR "/tests/fixtures/zen_plugin_fixture.cjs"));
    REQUIRE(descriptor);
    service.select(*descriptor);
    const auto prepared = service.prepare({{"login","fixture"}, {"password","synthetic-secret"},
        {"startDate","2026-07-01T00:00:00Z"}}, &credentials);
    REQUIRE(prepared);
    CHECK(prepared->startMessage.contains("synthetic-secret"));
    CHECK_FALSE(prepared->safePreferences.contains("password"));
    CHECK(prepared->safePreferences["login"] == QJsonValue("fixture"));
    CHECK(prepared->stateKey == app::pluginStateCredentialKey(*descriptor));
    CHECK_FALSE(service.answer("no pending prompt").value_or(true));
    service.cancel();
    service.resetSelection();
    CHECK_FALSE(service.descriptor());
}

TEST_CASE("Plugin state replacement preserves prior credential until successful save") {
    using namespace wealthtracker::app;
    PluginService service;
    Credentials credentials;
    credentials.values["old"] = "previous-state";
    service.setConfiguredStateKey("old");
    credentials.failWrites = true;
    const auto failed = service.saveState(credentials,"new", QJsonObject{{"session","next"}});
    CHECK_FALSE(failed.stored);
    CHECK(credentials.values["old"] == "previous-state");
    CHECK_FALSE(credentials.values.contains("new"));
    CHECK(service.configuredStateKey() == "old");
    credentials.failWrites = false;
    const auto saved = service.saveState(credentials,"new", QJsonObject{{"session","next"}});
    CHECK(saved.stored);
    CHECK(saved.oldStateRemoved);
    CHECK_FALSE(credentials.values.contains("old"));
    CHECK(service.configuredStateKey() == "new");
    REQUIRE(service.removeSavedState(credentials));
    CHECK_FALSE(credentials.values.contains("new"));
}
