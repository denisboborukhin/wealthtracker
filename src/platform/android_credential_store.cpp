module;

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QString>
#include <QtCore/qnativeinterface.h>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

module wt.platform.credential_store;

namespace wealthtracker::platform {
namespace {
constexpr auto vaultClass = "org/wealthtracker/android/CredentialVault";
constexpr std::size_t maximumSecretSize = 65'536;

CredentialError androidError() {
    return {CredentialErrorCode::platform_error, 0, "Android credential storage failed"};
}

bool failed(QJniEnvironment& environment) {
    if (!environment->ExceptionCheck()) return false;
    environment->ExceptionClear();
    return true;
}

class JavaArrayWiper {
public:
    JavaArrayWiper(QJniEnvironment& environment, jbyteArray array)
        : environment_(environment), array_(array) {}
    ~JavaArrayWiper() { if (!cleared_) (void)clear(); }
    bool clear() {
        const auto type = environment_.findClass("java/util/Arrays");
        const auto method = type ? environment_->GetStaticMethodID(type, "fill", "([BB)V") : nullptr;
        if (failed(environment_) || !method) return false;
        environment_->CallStaticVoidMethod(type, method, array_, jbyte{0});
        cleared_ = !failed(environment_);
        return cleared_;
    }
private:
    QJniEnvironment& environment_;
    jbyteArray array_;
    bool cleared_ = false;
};

class AndroidCredentialStore final : public CredentialStore {
public:
    explicit AndroidCredentialStore(std::string service) : service_(std::move(service)) {}

    std::expected<void, CredentialError> put(
        std::string_view account, std::span<const std::byte> secret) override {
        if (!valid(account) || secret.empty() || secret.size() > maximumSecretSize)
            return std::unexpected(CredentialError{CredentialErrorCode::invalid_argument, 0, "Invalid credential"});
        QJniEnvironment environment;
        auto local = environment->NewByteArray(static_cast<jsize>(secret.size()));
        const bool allocationFailed = failed(environment);
        if (!local || allocationFailed) return std::unexpected(androidError());
        const auto value = QJniObject::fromLocalRef(local);
        const auto array = value.object<jbyteArray>();
        JavaArrayWiper wiper(environment, array);
        environment->SetByteArrayRegion(array, 0, static_cast<jsize>(secret.size()),
                                       reinterpret_cast<const jbyte*>(secret.data()));
        if (failed(environment)) return std::unexpected(androidError());
        const auto name = key(account);
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto type = environment.findClass(vaultClass);
        const auto method = type ? environment->GetStaticMethodID(type, "put",
            "(Landroid/content/Context;Ljava/lang/String;[B)V") : nullptr;
        if (method) environment->CallStaticVoidMethod(type, method, context.object(), name.object<jstring>(), array);
        const bool error = failed(environment) || !method;
        const bool wiped = wiper.clear();
        if (error || !wiped) return std::unexpected(androidError());
        return {};
    }

    std::expected<SecureBuffer, CredentialError> get(std::string_view account) override {
        if (!valid(account)) return std::unexpected(CredentialError{
            CredentialErrorCode::invalid_argument, 0, "Invalid credential account"});
        QJniEnvironment environment;
        const auto name = key(account);
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto type = environment.findClass(vaultClass);
        const auto method = type ? environment->GetStaticMethodID(type, "get",
            "(Landroid/content/Context;Ljava/lang/String;)[B") : nullptr;
        if (failed(environment) || !method) return std::unexpected(androidError());
        auto local = static_cast<jbyteArray>(environment->CallStaticObjectMethod(type, method, context.object(), name.object<jstring>()));
        if (failed(environment)) return std::unexpected(androidError());
        if (!local) return std::unexpected(CredentialError{
            CredentialErrorCode::not_found, 0, "Credential not found"});
        const auto value = QJniObject::fromLocalRef(local);
        const auto array = value.object<jbyteArray>();
        JavaArrayWiper wiper(environment, array);
        const auto size = environment->GetArrayLength(array);
        if (size <= 0 || static_cast<std::size_t>(size) > maximumSecretSize)
            return std::unexpected(androidError());
        auto result = SecureBuffer::create(static_cast<std::size_t>(size));
        if (!result) return std::unexpected(androidError());
        environment->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(result->data()));
        const bool error = failed(environment);
        const bool wiped = wiper.clear();
        if (error || !wiped) return std::unexpected(androidError());
        return std::move(*result);
    }

    std::expected<void, CredentialError> remove(std::string_view account) override {
        if (!valid(account)) return std::unexpected(CredentialError{
            CredentialErrorCode::invalid_argument, 0, "Invalid credential account"});
        QJniEnvironment environment;
        const auto name = key(account);
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto type = environment.findClass(vaultClass);
        const auto method = type ? environment->GetStaticMethodID(type, "remove",
            "(Landroid/content/Context;Ljava/lang/String;)V") : nullptr;
        if (failed(environment) || !method) return std::unexpected(androidError());
        environment->CallStaticVoidMethod(type, method, context.object(), name.object<jstring>());
        if (failed(environment)) return std::unexpected(androidError());
        return {};
    }

    bool supported() const noexcept override { return true; }

private:
    static bool valid(std::string_view account) {
        return !account.empty() && account.size() <= 1024 && account.find('\0') == std::string_view::npos;
    }
    QJniObject key(std::string_view account) const {
        return QJniObject::fromString(QString::fromStdString(service_ + "/" + std::string(account)));
    }
    std::string service_;
};
}

std::unique_ptr<CredentialStore> make_android_credential_store(std::string service) {
    return std::make_unique<AndroidCredentialStore>(std::move(service));
}
}
