module;

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

module wt.platform.vault_crypto;

namespace wealthtracker::platform {
namespace {

constexpr std::array<std::byte, 4> magic{
    std::byte{0x57}, std::byte{0x54}, std::byte{0x56}, std::byte{0x4b}};
constexpr std::string_view associated_data = "wealthtracker-vault-key-v1";
constexpr std::uint64_t maximum_kdf_memory = 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t serialized_header_size = 4 + 4 + 8 + 8 + 16 + 24 + 4;

void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

std::uint32_t read_u32(std::span<const std::byte> input, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::byte> input, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= std::to_integer<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

std::expected<SecureBuffer, CryptoError> derive_key(
    std::string_view pin,
    std::span<const std::byte, crypto_pwhash_SALTBYTES> salt,
    std::uint64_t operations,
    std::uint64_t memory_bytes) {
    if (pin.empty() || operations < crypto_pwhash_OPSLIMIT_MIN ||
        operations > crypto_pwhash_OPSLIMIT_MAX ||
        memory_bytes < crypto_pwhash_MEMLIMIT_MIN ||
        memory_bytes > crypto_pwhash_MEMLIMIT_MAX ||
        memory_bytes > maximum_kdf_memory) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::invalid_argument, "PIN or Argon2id limits are invalid"});
    }
    auto derived = SecureBuffer::create(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    if (!derived) {
        return std::unexpected(derived.error());
    }
    const int result = crypto_pwhash(
        reinterpret_cast<unsigned char*>(derived->data()),
        derived->size(),
        pin.data(),
        static_cast<unsigned long long>(pin.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        operations,
        static_cast<std::size_t>(memory_bytes),
        crypto_pwhash_ALG_ARGON2ID13);
    if (result != 0) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::key_derivation_failed,
            "Argon2id could not derive a key with the requested memory limits"});
    }
    return derived;
}

}  // namespace

SecureBuffer::SecureBuffer(void* data, std::size_t size) noexcept : data_(data), size_(size) {}

SecureBuffer::~SecureBuffer() {
    if (data_ != nullptr) {
        sodium_memzero(data_, size_);
        sodium_free(data_);
    }
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (data_ != nullptr) {
        sodium_memzero(data_, size_);
        sodium_free(data_);
    }
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
    return *this;
}

std::expected<SecureBuffer, CryptoError> SecureBuffer::create(std::size_t size) {
    if (size == 0) {
        return std::unexpected(
            CryptoError{CryptoErrorCode::invalid_argument, "secure buffer size cannot be zero"});
    }
    if (auto initialized = VaultCrypto::initialize(); !initialized) {
        return std::unexpected(initialized.error());
    }
    void* memory = sodium_malloc(size);
    if (memory == nullptr) {
        return std::unexpected(
            CryptoError{CryptoErrorCode::allocation_failed, "secure memory allocation failed"});
    }
    sodium_memzero(memory, size);
    return SecureBuffer(memory, size);
}

std::span<std::byte> SecureBuffer::bytes() noexcept {
    return {static_cast<std::byte*>(data_), size_};
}

std::span<const std::byte> SecureBuffer::bytes() const noexcept {
    return {static_cast<const std::byte*>(data_), size_};
}

std::byte* SecureBuffer::data() noexcept {
    return static_cast<std::byte*>(data_);
}

const std::byte* SecureBuffer::data() const noexcept {
    return static_cast<const std::byte*>(data_);
}

std::size_t SecureBuffer::size() const noexcept {
    return size_;
}

bool SecureBuffer::empty() const noexcept {
    return size_ == 0;
}

std::expected<void, CryptoError> VaultCrypto::initialize() {
    if (sodium_init() < 0) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::initialization_failed, "libsodium initialization failed"});
    }
    return {};
}

std::expected<SecureBuffer, CryptoError> VaultCrypto::generate_database_key() {
    auto key = SecureBuffer::create(database_key_size);
    if (!key) {
        return std::unexpected(key.error());
    }
    randombytes_buf(key->data(), key->size());
    return key;
}

std::expected<WrappedKey, CryptoError> VaultCrypto::wrap_database_key(
    std::span<const std::byte> database_key,
    std::string_view pin,
    KdfLimits limits) {
    if (database_key.size() != database_key_size || pin.empty()) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::invalid_argument,
            "database key must be 32 bytes and PIN must not be empty"});
    }
    if (auto initialized = initialize(); !initialized) {
        return std::unexpected(initialized.error());
    }

    WrappedKey wrapped;
    wrapped.kdf_operations =
        limits.operations == 0 ? crypto_pwhash_OPSLIMIT_MODERATE : limits.operations;
    wrapped.kdf_memory_bytes =
        limits.memory_bytes == 0 ? crypto_pwhash_MEMLIMIT_MODERATE : limits.memory_bytes;
    randombytes_buf(wrapped.salt.data(), wrapped.salt.size());
    randombytes_buf(wrapped.nonce.data(), wrapped.nonce.size());

    auto wrapping_key = derive_key(
        pin,
        std::span<const std::byte, crypto_pwhash_SALTBYTES>(wrapped.salt),
        wrapped.kdf_operations,
        wrapped.kdf_memory_bytes);
    if (!wrapping_key) {
        return std::unexpected(wrapping_key.error());
    }

    wrapped.ciphertext.resize(
        database_key.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long ciphertext_size = 0;
    const int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char*>(wrapped.ciphertext.data()),
        &ciphertext_size,
        reinterpret_cast<const unsigned char*>(database_key.data()),
        database_key.size(),
        reinterpret_cast<const unsigned char*>(associated_data.data()),
        associated_data.size(),
        nullptr,
        reinterpret_cast<const unsigned char*>(wrapped.nonce.data()),
        reinterpret_cast<const unsigned char*>(wrapping_key->data()));
    if (result != 0) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::authentication_failed, "database key wrapping failed"});
    }
    wrapped.ciphertext.resize(static_cast<std::size_t>(ciphertext_size));
    return wrapped;
}

std::expected<SecureBuffer, CryptoError> VaultCrypto::unwrap_database_key(
    const WrappedKey& wrapped_key,
    std::string_view pin) {
    if (wrapped_key.format_version != 1 || pin.empty() ||
        wrapped_key.ciphertext.size() !=
            database_key_size + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return std::unexpected(
            CryptoError{CryptoErrorCode::invalid_argument, "wrapped key is invalid"});
    }
    auto wrapping_key = derive_key(
        pin,
        std::span<const std::byte, crypto_pwhash_SALTBYTES>(wrapped_key.salt),
        wrapped_key.kdf_operations,
        wrapped_key.kdf_memory_bytes);
    if (!wrapping_key) {
        return std::unexpected(wrapping_key.error());
    }
    auto database_key = SecureBuffer::create(database_key_size);
    if (!database_key) {
        return std::unexpected(database_key.error());
    }
    unsigned long long plaintext_size = 0;
    const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char*>(database_key->data()),
        &plaintext_size,
        nullptr,
        reinterpret_cast<const unsigned char*>(wrapped_key.ciphertext.data()),
        wrapped_key.ciphertext.size(),
        reinterpret_cast<const unsigned char*>(associated_data.data()),
        associated_data.size(),
        reinterpret_cast<const unsigned char*>(wrapped_key.nonce.data()),
        reinterpret_cast<const unsigned char*>(wrapping_key->data()));
    if (result != 0 || plaintext_size != database_key_size) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::authentication_failed, "incorrect PIN or corrupted wrapped key"});
    }
    return database_key;
}

std::vector<std::byte> VaultCrypto::serialize(const WrappedKey& wrapped_key) {
    if (wrapped_key.ciphertext.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    std::vector<std::byte> result;
    result.reserve(serialized_header_size + wrapped_key.ciphertext.size());
    result.insert(result.end(), magic.begin(), magic.end());
    append_u32(result, wrapped_key.format_version);
    append_u64(result, wrapped_key.kdf_operations);
    append_u64(result, wrapped_key.kdf_memory_bytes);
    result.insert(result.end(), wrapped_key.salt.begin(), wrapped_key.salt.end());
    result.insert(result.end(), wrapped_key.nonce.begin(), wrapped_key.nonce.end());
    append_u32(result, static_cast<std::uint32_t>(wrapped_key.ciphertext.size()));
    result.insert(result.end(), wrapped_key.ciphertext.begin(), wrapped_key.ciphertext.end());
    return result;
}

std::expected<WrappedKey, CryptoError> VaultCrypto::deserialize(
    std::span<const std::byte> serialized) {
    if (serialized.size() < serialized_header_size ||
        !std::equal(magic.begin(), magic.end(), serialized.begin())) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::invalid_serialization, "wrapped key header is invalid"});
    }
    WrappedKey result;
    std::size_t offset = magic.size();
    result.format_version = read_u32(serialized, offset);
    offset += 4;
    result.kdf_operations = read_u64(serialized, offset);
    offset += 8;
    result.kdf_memory_bytes = read_u64(serialized, offset);
    offset += 8;
    std::copy_n(serialized.begin() + static_cast<std::ptrdiff_t>(offset), result.salt.size(),
                result.salt.begin());
    offset += result.salt.size();
    std::copy_n(serialized.begin() + static_cast<std::ptrdiff_t>(offset), result.nonce.size(),
                result.nonce.begin());
    offset += result.nonce.size();
    const auto ciphertext_size = read_u32(serialized, offset);
    offset += 4;
    if (result.format_version != 1 ||
        ciphertext_size != database_key_size + crypto_aead_xchacha20poly1305_ietf_ABYTES ||
        serialized.size() != offset + ciphertext_size ||
        result.kdf_operations < crypto_pwhash_OPSLIMIT_MIN ||
        result.kdf_operations > crypto_pwhash_OPSLIMIT_MAX ||
        result.kdf_memory_bytes < crypto_pwhash_MEMLIMIT_MIN ||
        result.kdf_memory_bytes > crypto_pwhash_MEMLIMIT_MAX ||
        result.kdf_memory_bytes > maximum_kdf_memory) {
        return std::unexpected(CryptoError{
            CryptoErrorCode::invalid_serialization, "wrapped key parameters are invalid"});
    }
    result.ciphertext.assign(
        serialized.begin() + static_cast<std::ptrdiff_t>(offset), serialized.end());
    return result;
}

}  // namespace wealthtracker::platform
