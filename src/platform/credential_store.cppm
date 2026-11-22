module;

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

export module wt.platform.credential_store;

export import wt.platform.vault_crypto;

export namespace wealthtracker::platform {

enum class CredentialErrorCode {
    invalid_argument,
    not_found,
    access_denied,
    unsupported,
    platform_error,
};

struct CredentialError {
    CredentialErrorCode code{CredentialErrorCode::platform_error};
    long native_code{};
    std::string message;
};

class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    [[nodiscard]] virtual std::expected<void, CredentialError> put(
        std::string_view account,
        std::span<const std::byte> secret) = 0;
    [[nodiscard]] virtual std::expected<SecureBuffer, CredentialError> get(
        std::string_view account) = 0;
    [[nodiscard]] virtual std::expected<void, CredentialError> remove(
        std::string_view account) = 0;
    [[nodiscard]] virtual bool supported() const noexcept = 0;
};

// On unsupported platforms this deliberately returns a fail-closed implementation;
// secrets are never silently downgraded to plaintext files.
[[nodiscard]] std::unique_ptr<CredentialStore> make_system_credential_store(
    std::string service_name);

// Opt-in bundle/cache for stores supporting 64 KiB items. Legacy accounts migrate
// lazily; the wrapper never changes the bytes of an existing database key.
[[nodiscard]] std::unique_ptr<CredentialStore> make_bundled_credential_store(
    std::unique_ptr<CredentialStore> store);

}  // namespace wealthtracker::platform
