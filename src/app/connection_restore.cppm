module;
extern "C++" {
#include "app/CredentialBackend.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QVector>
#include <optional>
#include <string_view>
#include <utility>
}
export module wt.connection_restore;
import wt.connectors;
import wt.plugin_service;

export namespace wealthtracker::app {
struct ConnectionCredential {
    QString connectorId;
    QString credentialKey;
};

struct RestoredConnections {
    QSet<QString> apiConnectionIds;
    bool tbankConnected = false;
    bool tbankSessionSaved = false;
    bool alfaConnected = false;
    bool vtbConnected = false;
    bool ozonConnected = false;
    std::optional<wealthtracker::connectors::ZenPluginDescriptor> plugin;
    QString pluginInstitution;
};

RestoredConnections restoreConnections(
    CredentialBackend* credentials, QSettings& settings,
    const QVector<ConnectionCredential>& apiCredentials) {
    RestoredConnections restored;
    const auto validSession = [](const QByteArray& session) {
        return wealthtracker::connectors::isValidTBankRetailSessionId(
            std::string_view{session.constData(), static_cast<std::size_t>(session.size())});
    };
    const auto sessionKey = QStringLiteral("tbank-retail-session");
    if (credentials && credentials->supported()) {
        for (const auto& connection : apiCredentials) {
            auto secret = credentials->get(connection.credentialKey);
            if (secret && *secret) {
                restored.apiConnectionIds.insert(connection.connectorId);
                (**secret).fill('\0');
            }
        }
        auto saved = credentials->get(sessionKey);
        if (saved && *saved) {
            auto session = std::move(**saved);
            restored.tbankConnected = validSession(session);
            restored.tbankSessionSaved = restored.tbankConnected;
            session.fill('\0');
            if (!restored.tbankConnected) (void)credentials->remove(sessionKey);
        }
    }
    restored.alfaConnected = settings.value(QStringLiteral("alfa/connected"), false).toBool();
    restored.vtbConnected = settings.value(QStringLiteral("vtb/connected"), false).toBool();
    restored.ozonConnected = settings.value(QStringLiteral("ozon/connected"), false).toBool();
    const auto configuredBundle = settings.value(QStringLiteral("zenPlugin/bundlePath")).toString();
    if (configuredBundle.isEmpty()) return restored;
    auto descriptor = wealthtracker::connectors::inspectZenPluginBundle(configuredBundle);
    if (!descriptor) return restored;
    if (descriptor->pluginId != QStringLiteral("tbank-retail")) {
        restored.pluginInstitution = settings.value(
            QStringLiteral("zenPlugin/institution"), descriptor->pluginId).toString();
        restored.plugin = std::move(*descriptor);
        return restored;
    }
    bool migrationComplete = false;
    if (credentials && credentials->supported()) {
        const auto legacyStateKey = pluginStateCredentialKey(*descriptor);
        auto legacyState = credentials->get(legacyStateKey);
        if (legacyState && *legacyState) {
            auto encoded = std::move(**legacyState);
            if (encoded.size() <= 1024 * 1024) {
                const auto document = QJsonDocument::fromJson(encoded);
                auto session = document.object().value(QStringLiteral("pluginData"))
                                   .toObject().value(QStringLiteral("sessionId"))
                                   .toString().toLatin1();
                if (validSession(session)) {
                    const auto migrated = credentials->put(sessionKey, session);
                    restored.tbankConnected = migrated.has_value();
                    migrationComplete = migrated.has_value();
                }
                session.fill('\0');
            }
            encoded.fill('\0');
        }
        if (migrationComplete) (void)credentials->remove(legacyStateKey);
    }
    if (migrationComplete) {
        const auto legacyLastSync = settings.value(QStringLiteral("zenPlugin/lastSyncAt"));
        if (legacyLastSync.isValid()) {
            settings.setValue(QStringLiteral("tbank/lastSyncAt"), legacyLastSync);
        }
        settings.setValue(QStringLiteral("tbank/autoUpdate"),
                          settings.value(QStringLiteral("zenPlugin/autoUpdate"), true));
        settings.remove(QStringLiteral("zenPlugin"));
    }
    return restored;
}
}
