module;
extern "C++" {
#include "app/CredentialBackend.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSet>
#include <QTimeZone>
#include <QVariantMap>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
}
export module wt.plugin_service;
import wt.connectors;
import wt.zen_plugin_runner;

export namespace wealthtracker::app {
using wealthtracker::connectors::ZenPluginDescriptor;

QString pluginStateCredentialKey(const ZenPluginDescriptor& descriptor) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(descriptor.pluginId.toUtf8());
    hash.addData("\n");
    hash.addData(descriptor.bundlePath.toUtf8());
    if (descriptor.pluginId != QStringLiteral("tbank-retail")) {
        hash.addData("\n");
        hash.addData(descriptor.bundleDigest);
    }
    return QStringLiteral("zen-plugin-state-%1")
        .arg(QString::fromLatin1(hash.result().toHex()));
}

struct PreparedPluginRun {
    QByteArray startMessage;
    QJsonObject safePreferences;
    QString stateKey;
};

struct PluginStateSaved {
    bool stored;
    bool oldStateRemoved;
};

// Owns the selected plugin and its process/session correlation. Presentation,
// batch publication and refresh scheduling stay with the application facade.
class PluginService final {
public:
    [[nodiscard]] const std::optional<ZenPluginDescriptor>& descriptor() const { return descriptor_; }
    void select(ZenPluginDescriptor descriptor) { descriptor_ = std::move(descriptor); }
    [[nodiscard]] const QString& configuredStateKey() const { return configured_state_key_; }
    void setConfiguredStateKey(QString key) { configured_state_key_ = std::move(key); }
    void resetSelection() { descriptor_.reset(); configured_state_key_.clear(); }

    [[nodiscard]] std::expected<PreparedPluginRun, QString> prepare(
        const QVariantMap& preferences, CredentialBackend* credentials) const {
        if (!descriptor_) return std::unexpected(QStringLiteral("Сначала выберите собранный bundle"));
        const auto current = wealthtracker::connectors::inspectZenPluginBundle(descriptor_->bundlePath);
        if (!current || current->bundleDigest != descriptor_->bundleDigest)
            return std::unexpected(QStringLiteral("Bundle изменился. Проверьте его и выберите снова."));
        if (!credentials || !credentials->supported()) return std::unexpected(QStringLiteral(
            "Системное хранилище секретов нужно для состояния плагина"));
        QJsonObject normalizedPreferences;
        QDateTime from =
            QDateTime(QDate::currentDate().addYears(-1), QTime(0, 0),
                      QTimeZone::UTC);
        for (const auto& field :
             descriptor_->preferences) {
            auto value =
                preferences.value(field.key, field.defaultValue);
            if (field.kind
                == wealthtracker::connectors::
                    ZenPluginPreferenceKind::Checkbox) {
                normalizedPreferences.insert(field.key, value.toBool());
            } else {
                const auto text = value.toString();
                if (field.required && text.trimmed().isEmpty()) {
                    return std::unexpected(QStringLiteral("Заполните поле «%1»")
                            .arg(field.label));
                }
                normalizedPreferences.insert(field.key, text);
                if (field.key == QStringLiteral("startDate")) {
                    auto parsed =
                        QDateTime::fromString(text, Qt::ISODateWithMs);
                    if (!parsed.isValid()) {
                        parsed =
                            QDateTime::fromString(text, Qt::ISODate);
                    }
                    if (parsed.isValid()) {
                        from = parsed.toUTC();
                    }
                }
            }
        }

        QJsonObject state;
        const auto stateKey =
            pluginStateCredentialKey(*descriptor_);
        auto savedState = credentials->get(stateKey);
        if (!savedState) {
            return std::unexpected(savedState.error());
        }
        if (*savedState) {
            QByteArray encoded = std::move(**savedState);
            QJsonParseError parseError;
            const auto document =
                QJsonDocument::fromJson(encoded, &parseError);
            encoded.fill('\0');
            if (parseError.error == QJsonParseError::NoError
                && document.isObject()) {
                state = document.object();
            } else {
                const auto removed =
                    credentials->remove(stateKey);
                if (!removed) {
                    return std::unexpected(removed.error());
                }
            }
        }

        const wealthtracker::connectors::ZenPluginRunRequest request{
            .descriptor = *descriptor_,
            .preferences = std::move(normalizedPreferences),
            .state = std::move(state),
            .from = from,
            .to = QDateTime::currentDateTimeUtc(),
            .allowedOrigins = {},
        };
        const auto safePreferences =
            persistablePreferences(request.descriptor, request.preferences);
        auto startMessage =
            wealthtracker::connectors::makeZenPluginStartMessage(
                request);
        if (!startMessage) {
            return std::unexpected(startMessage.error().message);
        }

        return PreparedPluginRun{std::move(*startMessage), safePreferences, stateKey};
    }

    bool start(const QString& executable, const QByteArray& message,
               ZenPluginRunner::Prompted prompted, ZenPluginRunner::Completed completed,
               ZenPluginRunner::Failed failed, QObject* parent) {
        if (!descriptor_) return false;
        prompt_id_.clear();
        runner_ = std::make_unique<ZenPluginRunner>(descriptor_->pluginId,
            [this, prompted = std::move(prompted)](wealthtracker::connectors::ZenPluginPrompt prompt) {
                prompt_id_ = prompt.requestId;
                prompted(std::move(prompt));
            }, std::move(completed), std::move(failed), parent);
        if (runner_->start(executable, message)) return true;
        runner_.reset();
        return false;
    }

    [[nodiscard]] std::expected<bool, QString> answer(const QString& value) {
        if (!runner_ || prompt_id_.isEmpty()) return false;
        auto encoded = value.toUtf8();
        auto message = wealthtracker::connectors::makeZenPluginInputMessage(prompt_id_, encoded);
        encoded.fill('\0');
        if (!message) return std::unexpected(message.error().message);
        const auto accepted = runner_->answer(*message);
        message->fill('\0');
        if (accepted) prompt_id_.clear();
        return accepted;
    }

    void clearPrompt() { prompt_id_.clear(); }
    void cancel() { if (runner_) runner_->cancel(); prompt_id_.clear(); }

    [[nodiscard]] PluginStateSaved saveState(CredentialBackend& credentials,
                                             const QString& key, const QJsonObject& state) {
        auto encoded = QJsonDocument(state).toJson(QJsonDocument::Compact);
        const auto stored = credentials.put(key, encoded);
        encoded.fill('\0');
        bool removed = true;
        if (stored && !configured_state_key_.isEmpty() && configured_state_key_ != key)
            removed = credentials.remove(configured_state_key_).has_value();
        if (stored) configured_state_key_ = key;
        return {stored.has_value(), removed};
    }

    [[nodiscard]] std::expected<void, QString> removeSavedState(CredentialBackend& credentials) const {
        QSet<QString> keys;
        if (descriptor_) keys.insert(pluginStateCredentialKey(*descriptor_));
        if (!configured_state_key_.isEmpty()) keys.insert(configured_state_key_);
        for (const auto& key : keys) {
            const auto removed = credentials.remove(key);
            if (!removed) return std::unexpected(removed.error());
        }
        return {};
    }

private:
    static QJsonObject persistablePreferences(const ZenPluginDescriptor& descriptor,
                                              const QJsonObject& preferences) {
        QJsonObject safe;
        for (const auto& field : descriptor.preferences) {
            if (!field.sensitive && field.kind != wealthtracker::connectors::ZenPluginPreferenceKind::Password
                && preferences.contains(field.key)) safe.insert(field.key, preferences.value(field.key));
        }
        return safe;
    }
    std::optional<ZenPluginDescriptor> descriptor_;
    std::unique_ptr<ZenPluginRunner> runner_;
    QString prompt_id_;
    QString configured_state_key_;
};
}
