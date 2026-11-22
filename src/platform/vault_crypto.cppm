module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module wt.platform.vault_crypto;

export namespace wealthtracker::platform {

enum class CryptoErrorCode {
    initialization_failed,
    invalid_argument,
    allocation_failed,
    key_derivation_failed,
    authentication_failed,
    invalid_serialization,
};

struct CryptoError {
    CryptoErrorCode code{CryptoErrorCode::invalid_argument};
    std::string message;
};

class SecureBuffer final {
public:
    SecureBuffer() = default;
    ~SecureBuffer();

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    [[nodiscard]] static std::expected<SecureBuffer, CryptoError> create(std::size_t size);

    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::byte* data() noexcept;
    [[nodiscard]] const std::byte* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    SecureBuffer(void* data, std::size_t size) noexcept;

    void* data_{};
    std::size_t size_{};
};

struct KdfLimits {
    // A zero value selects libsodium's MODERATE preset.
    std::uint64_t operations{};
    std::uint64_t memory_bytes{};
};

struct WrappedKey {
    std::uint32_t format_version{1};
    std::uint64_t kdf_operations{};
    std::uint64_t kdf_memory_bytes{};
    std::array<std::byte, 16> salt{};
    std::array<std::byte, 24> nonce{};
    std::vector<std::byte> ciphertext;
};

class VaultCrypto final {
public:
    static constexpr std::size_t database_key_size = 32;

    [[nodiscard]] static std::expected<void, CryptoError> initialize();
    [[nodiscard]] static std::expected<SecureBuffer, CryptoError> generate_database_key();
    [[nodiscard]] static std::expected<WrappedKey, CryptoError> wrap_database_key(
        std::span<const std::byte> database_key,
        std::string_view pin,
        KdfLimits limits = {});
    [[nodiscard]] static std::expected<SecureBuffer, CryptoError> unwrap_database_key(
        const WrappedKey& wrapped_key,
        std::string_view pin);

    [[nodiscard]] static std::vector<std::byte> serialize(const WrappedKey& wrapped_key);
    [[nodiscard]] static std::expected<WrappedKey, CryptoError> deserialize(
        std::span<const std::byte> serialized);
};

}  // namespace wealthtracker::platform
