module;

#include <algorithm>
#include <sodium.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module wt.platform.credential_store;

namespace wealthtracker::platform {
namespace {

constexpr std::string_view bundle_account = "credential-vault-v1";
constexpr std::size_t maximum_bundle_size = 65'536;
constexpr std::size_t header_size = 16;
constexpr std::array<std::byte, 8> magic{
    std::byte{'W'}, std::byte{'T'}, std::byte{'C'}, std::byte{'R'},
    std::byte{'E'}, std::byte{'D'}, std::byte{0}, std::byte{0}};
using Entries = std::map<std::string, SecureBuffer, std::less<>>;

CredentialError vault_error(std::string message,
                            CredentialErrorCode code = CredentialErrorCode::platform_error) {
    return {code, 0, std::move(message)};
}

bool valid_account(std::string_view value) {
    if (value.empty() || value.size() > 1'024 || value == bundle_account) return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first == 0) return false;
        if (first < 0x80U) continue;
        unsigned remaining{};
        std::uint32_t codepoint{};
        std::uint32_t minimum{};
        if (first >= 0xc2U && first <= 0xdfU) {
            remaining = 1; codepoint = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            remaining = 2; codepoint = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            remaining = 3; codepoint = first & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (remaining > value.size() - i) return false;
        while (remaining-- > 0) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return false;
    }
    return true;
}

std::expected<SecureBuffer, CredentialError> copy_secret(std::span<const std::byte> bytes) {
    if (bytes.empty()) return SecureBuffer{};
    auto copy = SecureBuffer::create(bytes.size());
    if (!copy) return std::unexpected(vault_error("allocate credential buffer"));
    std::memcpy(copy->data(), bytes.data(), bytes.size());
    return std::move(*copy);
}

void write_u32(std::span<std::byte> bytes, std::size_t& offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t& offset) {
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= std::to_integer<std::uint32_t>(bytes[offset++]) << shift;
    }
    return value;
}

std::expected<Entries, CredentialError> decode(std::span<const std::byte> bytes) {
    const auto malformed = [] { return std::unexpected(vault_error("invalid credential vault")); };
    if (bytes.size() < header_size || bytes.size() > maximum_bundle_size ||
        !std::equal(magic.begin(), magic.end(), bytes.begin())) return malformed();
    std::size_t offset = magic.size();
    if (read_u32(bytes, offset) != 1) {
        return std::unexpected(vault_error("unsupported credential vault version"));
    }
    const auto count = read_u32(bytes, offset);
    if (count > (bytes.size() - header_size) / 9) return malformed();
    Entries entries;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (bytes.size() - offset < 8) return malformed();
        const auto name_size = read_u32(bytes, offset);
        const auto secret_size = read_u32(bytes, offset);
        if (name_size > bytes.size() - offset) return malformed();
        const std::string_view name(reinterpret_cast<const char*>(bytes.data() + offset), name_size);
        if (!valid_account(name) || entries.contains(name)) return malformed();
        offset += name_size;
        if (secret_size > bytes.size() - offset) return malformed();
        auto secret = copy_secret(bytes.subspan(offset, secret_size));
        if (!secret) return std::unexpected(secret.error());
        entries.emplace(std::string(name), std::move(*secret));
        offset += secret_size;
    }
    if (offset != bytes.size()) return malformed();
    return entries;
}

// Empty values are persistent tombstones, so undeleted legacy items cannot reappear.
std::expected<SecureBuffer, CredentialError> encode(
    const Entries& entries, std::string_view account, std::span<const std::byte> replacement) {
    std::size_t size = header_size;
    const auto add_size = [&size](std::string_view name, std::span<const std::byte> secret) {
        const auto overhead = 8 + name.size();
        if (overhead > maximum_bundle_size - size) return false;
        size += overhead;
        if (secret.size() > maximum_bundle_size - size) return false;
        size += secret.size();
        return true;
    };
    for (const auto& [name, secret] : entries) {
        if (!add_size(name, name == account ? replacement : secret.bytes())) {
            return std::unexpected(vault_error("credential vault exceeds size limit"));
        }
    }
    if (!entries.contains(account) && !add_size(account, replacement)) {
        return std::unexpected(vault_error("credential vault exceeds size limit"));
    }
    auto buffer = SecureBuffer::create(size);
    if (!buffer) return std::unexpected(vault_error("allocate credential vault"));
    auto bytes = buffer->bytes();
    std::copy(magic.begin(), magic.end(), bytes.begin());
    std::size_t offset = magic.size();
    write_u32(bytes, offset, 1);
    write_u32(bytes, offset, static_cast<std::uint32_t>(entries.size() +
                                                      (entries.contains(account) ? 0 : 1)));
    const auto write_entry = [&bytes, &offset](std::string_view name,
                                              std::span<const std::byte> secret) {
        write_u32(bytes, offset, static_cast<std::uint32_t>(name.size()));
        write_u32(bytes, offset, static_cast<std::uint32_t>(secret.size()));
        std::memcpy(bytes.data() + offset, name.data(), name.size());
        offset += name.size();
        if (!secret.empty()) std::memcpy(bytes.data() + offset, secret.data(), secret.size());
        offset += secret.size();
    };
    for (const auto& [name, secret] : entries) {
        write_entry(name, name == account ? replacement : secret.bytes());
    }
    if (!entries.contains(account)) write_entry(account, replacement);
    return std::move(*buffer);
}

class BundledCredentialStore final : public CredentialStore {
public:
    explicit BundledCredentialStore(std::unique_ptr<CredentialStore> store)
        : store_(std::move(store)) {}

    bool supported() const noexcept override { return store_->supported(); }

    std::expected<SecureBuffer, CredentialError> get(std::string_view account) override {
        if (!valid_account(account)) return std::unexpected(invalid_account());
        if (auto ready = load(); !ready) return std::unexpected(ready.error());
        const auto found = entries_.find(account);
        if (found != entries_.end()) {
            if (found->second.empty()) return std::unexpected(missing());
            return copy_secret(found->second.bytes());
        }
        if (missing_.contains(account)) return std::unexpected(missing());
        auto legacy = store_->get(account);
        if (!legacy) {
            if (legacy.error().code == CredentialErrorCode::not_found) {
                missing_.emplace(account);
            } else if (legacy.error().code == CredentialErrorCode::access_denied) {
                // Respect cancellation for the whole startup migration, rather
                // than prompting again for each remaining legacy account.
                load_error_ = legacy.error();
            }
            return std::unexpected(legacy.error());
        }
        auto migrated = put(account, legacy->bytes());
        if (!migrated) return std::unexpected(migrated.error());
        return std::move(*legacy);
    }

    std::expected<void, CredentialError> put(
        std::string_view account, std::span<const std::byte> secret) override {
        if (!valid_account(account) || secret.empty() || secret.size() > maximum_bundle_size) {
            return std::unexpected(invalid_account());
        }
        if (auto ready = load(); !ready) return ready;
        const auto current = entries_.find(account);
        if (current != entries_.end() && current->second.size() == secret.size() &&
            sodium_memcmp(current->second.data(), secret.data(), secret.size()) == 0) return {};
        auto staged = copy_secret(secret);
        if (!staged) return std::unexpected(staged.error());
        if (auto saved = persist(account, staged->bytes()); !saved) return saved;
        entries_.insert_or_assign(std::string(account), std::move(*staged));
        missing_.erase(std::string(account));
        // A failed cleanup leaves the durable bundled value authoritative.
        (void)store_->remove(account);
        return {};
    }

    std::expected<void, CredentialError> remove(std::string_view account) override {
        if (!valid_account(account)) return std::unexpected(invalid_account());
        if (auto ready = load(); !ready) return ready;
        if (auto saved = persist(account, {}); !saved) return saved;
        entries_.insert_or_assign(std::string(account), SecureBuffer{});
        missing_.erase(std::string(account));
        const auto removed = store_->remove(account);
        if (!removed && removed.error().code != CredentialErrorCode::not_found) return removed;
        return {};
    }

private:
    static CredentialError invalid_account() {
        return vault_error("invalid credential fields", CredentialErrorCode::invalid_argument);
    }
    static CredentialError missing() {
        return vault_error("credential not found", CredentialErrorCode::not_found);
    }

    std::expected<void, CredentialError> load() {
        if (load_error_) return std::unexpected(*load_error_);
        if (loaded_) return {};
        loaded_ = true;
        auto bundle = store_->get(bundle_account);
        if (!bundle) {
            if (bundle.error().code == CredentialErrorCode::not_found) return {};
            load_error_ = bundle.error();
        } else {
            auto decoded = decode(bundle->bytes());
            if (decoded) {
                entries_ = std::move(*decoded);
                return {};
            }
            load_error_ = decoded.error();
        }
        return std::unexpected(*load_error_);
    }

    std::expected<void, CredentialError> persist(
        std::string_view account, std::span<const std::byte> secret) {
        auto encoded = encode(entries_, account, secret);
        if (!encoded) return std::unexpected(encoded.error());
        return store_->put(bundle_account, encoded->bytes());
    }

    std::unique_ptr<CredentialStore> store_;
    Entries entries_;
    std::set<std::string, std::less<>> missing_;
    bool loaded_ = false;
    std::optional<CredentialError> load_error_;
};

}  // namespace

std::unique_ptr<CredentialStore> make_bundled_credential_store(
    std::unique_ptr<CredentialStore> store) {
    if (!store || !store->supported()) return store;
    return std::make_unique<BundledCredentialStore>(std::move(store));
}

}  // namespace wealthtracker::platform
