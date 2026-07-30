module;

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module wt.platform.credential_store;

namespace wealthtracker::platform {
namespace {

CredentialError invalid_account() {
    return CredentialError{
        CredentialErrorCode::invalid_argument, 0, "credential account must not be empty"};
}

bool valid_identifier(std::string_view value) {
    return !value.empty() && value.size() <= 1'024 &&
           value.find('\0') == std::string_view::npos;
}

class UnsupportedCredentialStore final : public CredentialStore {
public:
    explicit UnsupportedCredentialStore(std::string service)
        : service_(std::move(service)) {}

    std::expected<void, CredentialError> put(
        std::string_view,
        std::span<const std::byte>) override {
        return std::unexpected(error());
    }

    std::expected<SecureBuffer, CredentialError> get(std::string_view) override {
        return std::unexpected(error());
    }

    std::expected<void, CredentialError> remove(std::string_view) override {
        return std::unexpected(error());
    }

    bool supported() const noexcept override {
        return false;
    }

private:
    CredentialError error() const {
        return CredentialError{
            CredentialErrorCode::unsupported,
            0,
            "no secure system credential store is available for service " + service_};
    }

    std::string service_;
};

#if defined(__APPLE__)

template <typename T>
class CfRef final {
public:
    explicit CfRef(T value = nullptr) noexcept : value_(value) {}
    ~CfRef() {
        if (value_ != nullptr) {
            CFRelease(value_);
        }
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    T get() const noexcept { return value_; }

private:
    T value_;
};

CfRef<CFStringRef> make_cf_string(std::string_view value) {
    return CfRef<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false));
}

CredentialError keychain_error(OSStatus status, std::string operation) {
    CredentialErrorCode code = CredentialErrorCode::platform_error;
    if (status == errSecItemNotFound) {
        code = CredentialErrorCode::not_found;
    } else if (status == errSecAuthFailed || status == errSecInteractionNotAllowed) {
        code = CredentialErrorCode::access_denied;
    }
    return CredentialError{code, static_cast<long>(status), std::move(operation)};
}

class MacCredentialStore final : public CredentialStore {
public:
    explicit MacCredentialStore(std::string service) : service_(std::move(service)) {}

    std::expected<void, CredentialError> put(
        std::string_view account,
        std::span<const std::byte> secret) override {
        if (!valid_identifier(account) || secret.empty() || secret.size() > 65'536) {
            return std::unexpected(!valid_identifier(account)
                                       ? invalid_account()
                                       : CredentialError{CredentialErrorCode::invalid_argument,
                                                         0,
                                                         "credential secret size is invalid"});
        }
        auto service = make_cf_string(service_);
        auto account_name = make_cf_string(account);
        if (service.get() == nullptr || account_name.get() == nullptr) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, "encode Keychain attributes"});
        }

        const void* query_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* query_values[] = {
            kSecClassGenericPassword, service.get(), account_name.get()};
        CfRef<CFDictionaryRef> query(CFDictionaryCreate(
            kCFAllocatorDefault,
            query_keys,
            query_values,
            3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        // Reference the caller's secure buffer only for the duration of this
        // synchronous call instead of creating an additional unwipeable CFData copy.
        CfRef<CFDataRef> secret_data(CFDataCreateWithBytesNoCopy(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(secret.data()),
            static_cast<CFIndex>(secret.size()),
            kCFAllocatorNull));
        if (secret_data.get() == nullptr) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, "create Keychain secret view"});
        }
        const void* update_keys[] = {kSecValueData, kSecAttrAccessible};
        const void* update_values[] = {
            secret_data.get(), kSecAttrAccessibleWhenUnlockedThisDeviceOnly};
        CfRef<CFDictionaryRef> update(CFDictionaryCreate(
            kCFAllocatorDefault,
            update_keys,
            update_values,
            2,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        const OSStatus update_status = SecItemUpdate(query.get(), update.get());
        if (update_status == errSecSuccess) {
            return {};
        }
        if (update_status != errSecItemNotFound) {
            return std::unexpected(keychain_error(update_status, "update Keychain item"));
        }

        const void* add_keys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData, kSecAttrAccessible};
        const void* add_values[] = {
            kSecClassGenericPassword,
            service.get(),
            account_name.get(),
            secret_data.get(),
            kSecAttrAccessibleWhenUnlockedThisDeviceOnly};
        CfRef<CFDictionaryRef> add(CFDictionaryCreate(
            kCFAllocatorDefault,
            add_keys,
            add_values,
            5,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        const OSStatus status = SecItemAdd(add.get(), nullptr);
        if (status != errSecSuccess) {
            return std::unexpected(keychain_error(status, "store Keychain item"));
        }
        return {};
    }

    std::expected<SecureBuffer, CredentialError> get(std::string_view account) override {
        if (!valid_identifier(account)) {
            return std::unexpected(invalid_account());
        }
        auto service = make_cf_string(service_);
        auto account_name = make_cf_string(account);
        const void* keys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
        const void* values[] = {
            kSecClassGenericPassword,
            service.get(),
            account_name.get(),
            kCFBooleanTrue,
            kSecMatchLimitOne};
        CfRef<CFDictionaryRef> query(CFDictionaryCreate(
            kCFAllocatorDefault,
            keys,
            values,
            5,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        CFTypeRef raw_result = nullptr;
        const OSStatus status = SecItemCopyMatching(query.get(), &raw_result);
        CfRef<CFDataRef> data(static_cast<CFDataRef>(raw_result));
        if (status != errSecSuccess) {
            return std::unexpected(keychain_error(status, "read Keychain item"));
        }
        const auto size = static_cast<std::size_t>(CFDataGetLength(data.get()));
        if (size == 0) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, "Keychain item is empty"});
        }
        auto secret = SecureBuffer::create(size);
        if (!secret) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, secret.error().message});
        }
        std::memcpy(secret->data(), CFDataGetBytePtr(data.get()), size);
        return std::move(*secret);
    }

    std::expected<void, CredentialError> remove(std::string_view account) override {
        if (!valid_identifier(account)) {
            return std::unexpected(invalid_account());
        }
        auto service = make_cf_string(service_);
        auto account_name = make_cf_string(account);
        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* values[] = {
            kSecClassGenericPassword, service.get(), account_name.get()};
        CfRef<CFDictionaryRef> query(CFDictionaryCreate(
            kCFAllocatorDefault,
            keys,
            values,
            3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        const OSStatus status = SecItemDelete(query.get());
        if (status == errSecItemNotFound) {
            return {};
        }
        if (status != errSecSuccess) {
            return std::unexpected(keychain_error(status, "remove Keychain item"));
        }
        return {};
    }

    bool supported() const noexcept override {
        return true;
    }

private:
    std::string service_;
};

#elif defined(_WIN32)

std::expected<std::wstring, CredentialError> to_wide(std::string_view value) {
    if (value.empty()) {
        return std::wstring{};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return std::unexpected(CredentialError{
            CredentialErrorCode::invalid_argument,
            static_cast<long>(GetLastError()),
            "credential identifier is not valid UTF-8"});
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        size);
    return output;
}

CredentialError windows_error(DWORD code, std::string operation) {
    CredentialErrorCode error_code = CredentialErrorCode::platform_error;
    if (code == ERROR_NOT_FOUND) {
        error_code = CredentialErrorCode::not_found;
    } else if (code == ERROR_ACCESS_DENIED || code == ERROR_NO_SUCH_LOGON_SESSION) {
        error_code = CredentialErrorCode::access_denied;
    }
    return CredentialError{error_code, static_cast<long>(code), std::move(operation)};
}

class WindowsCredentialStore final : public CredentialStore {
public:
    explicit WindowsCredentialStore(std::string service) : service_(std::move(service)) {}

    std::expected<void, CredentialError> put(
        std::string_view account,
        std::span<const std::byte> secret) override {
        if (!valid_identifier(account) || secret.empty() ||
            secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::invalid_argument, 0, "credential fields are invalid"});
        }
        auto target = target_name(account);
        if (!target) {
            return std::unexpected(target.error());
        }
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target->data();
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(
            const_cast<std::byte*>(secret.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!CredWriteW(&credential, 0)) {
            return std::unexpected(windows_error(GetLastError(), "store Windows credential"));
        }
        return {};
    }

    std::expected<SecureBuffer, CredentialError> get(std::string_view account) override {
        if (!valid_identifier(account)) {
            return std::unexpected(invalid_account());
        }
        auto target = target_name(account);
        if (!target) {
            return std::unexpected(target.error());
        }
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target->c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            return std::unexpected(windows_error(GetLastError(), "read Windows credential"));
        }
        struct CredentialGuard {
            PCREDENTIALW value;
            ~CredentialGuard() {
                if (value != nullptr && value->CredentialBlob != nullptr) {
                    SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
                }
                CredFree(value);
            }
        } guard{credential};
        if (credential->CredentialBlobSize == 0) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, "Windows credential is empty"});
        }
        auto secret = SecureBuffer::create(credential->CredentialBlobSize);
        if (!secret) {
            return std::unexpected(CredentialError{
                CredentialErrorCode::platform_error, 0, secret.error().message});
        }
        std::memcpy(secret->data(), credential->CredentialBlob, credential->CredentialBlobSize);
        return std::move(*secret);
    }

    std::expected<void, CredentialError> remove(std::string_view account) override {
        if (!valid_identifier(account)) {
            return std::unexpected(invalid_account());
        }
        auto target = target_name(account);
        if (!target) {
            return std::unexpected(target.error());
        }
        if (!CredDeleteW(target->c_str(), CRED_TYPE_GENERIC, 0)) {
            const auto code = GetLastError();
            if (code != ERROR_NOT_FOUND) {
                return std::unexpected(windows_error(code, "remove Windows credential"));
            }
        }
        return {};
    }

    bool supported() const noexcept override {
        return true;
    }

private:
    std::expected<std::wstring, CredentialError> target_name(std::string_view account) const {
        return to_wide(service_ + "/" + std::string(account));
    }

    std::string service_;
};

#endif

}  // namespace

std::unique_ptr<CredentialStore> make_system_credential_store(std::string service_name) {
    if (!valid_identifier(service_name)) {
        return std::make_unique<UnsupportedCredentialStore>(std::move(service_name));
    }
#if defined(__APPLE__)
    return std::make_unique<MacCredentialStore>(std::move(service_name));
#elif defined(_WIN32)
    return std::make_unique<WindowsCredentialStore>(std::move(service_name));
#else
    return std::make_unique<UnsupportedCredentialStore>(std::move(service_name));
#endif
}

}  // namespace wealthtracker::platform
