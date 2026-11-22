#pragma once

#include <QByteArray>
#include <QString>
#include <expected>
#include <optional>

namespace wealthtracker::app {
class CredentialBackend {
public:
    virtual ~CredentialBackend() = default;
    [[nodiscard]] virtual bool supported() const noexcept = 0;
    [[nodiscard]] virtual std::expected<void, QString>
    put(const QString& key, const QByteArray& secret) = 0;
    [[nodiscard]] virtual std::expected<std::optional<QByteArray>, QString>
    get(const QString& key) = 0;
    [[nodiscard]] virtual std::expected<void, QString>
    remove(const QString& key) = 0;
};

}
