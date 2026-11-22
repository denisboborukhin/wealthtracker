#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <sodium.h>

import wt.platform.vault_crypto;
import wt.platform.credential_store;

TEST_CASE("vault key wraps and unwraps with Argon2id", "[platform][crypto]") {
    auto key = wealthtracker::platform::VaultCrypto::generate_database_key();
    REQUIRE(key.has_value());
    REQUIRE(key->size() == wealthtracker::platform::VaultCrypto::database_key_size);

    const wealthtracker::platform::KdfLimits test_limits{
        crypto_pwhash_OPSLIMIT_MIN,
        crypto_pwhash_MEMLIMIT_MIN,
    };
    auto wrapped =
        wealthtracker::platform::VaultCrypto::wrap_database_key(key->bytes(), "correct horse", test_limits);
    REQUIRE(wrapped.has_value());
    REQUIRE_FALSE(wrapped->ciphertext.empty());

    auto unwrapped = wealthtracker::platform::VaultCrypto::unwrap_database_key(*wrapped, "correct horse");
    REQUIRE(unwrapped.has_value());
    REQUIRE(std::equal(
        key->bytes().begin(), key->bytes().end(), unwrapped->bytes().begin()));
}

TEST_CASE("wrong PIN cannot authenticate a wrapped vault key", "[platform][crypto]") {
    auto key = wealthtracker::platform::VaultCrypto::generate_database_key();
    REQUIRE(key.has_value());
    const wealthtracker::platform::KdfLimits test_limits{
        crypto_pwhash_OPSLIMIT_MIN,
        crypto_pwhash_MEMLIMIT_MIN,
    };
    auto wrapped = wealthtracker::platform::VaultCrypto::wrap_database_key(key->bytes(), "right-pin", test_limits);
    REQUIRE(wrapped.has_value());

    auto result = wealthtracker::platform::VaultCrypto::unwrap_database_key(*wrapped, "wrong-pin");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == wealthtracker::platform::CryptoErrorCode::authentication_failed);
}

TEST_CASE("wrapped key serialization is versioned and rejects corruption", "[platform][crypto]") {
    auto key = wealthtracker::platform::VaultCrypto::generate_database_key();
    REQUIRE(key.has_value());
    const wealthtracker::platform::KdfLimits test_limits{
        crypto_pwhash_OPSLIMIT_MIN,
        crypto_pwhash_MEMLIMIT_MIN,
    };
    auto wrapped = wealthtracker::platform::VaultCrypto::wrap_database_key(key->bytes(), "1234", test_limits);
    REQUIRE(wrapped.has_value());

    auto bytes = wealthtracker::platform::VaultCrypto::serialize(*wrapped);
    auto decoded = wealthtracker::platform::VaultCrypto::deserialize(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->ciphertext == wrapped->ciphertext);
    REQUIRE(decoded->kdf_operations == wrapped->kdf_operations);

    bytes.front() = std::byte{0};
    auto corrupted = wealthtracker::platform::VaultCrypto::deserialize(bytes);
    REQUIRE_FALSE(corrupted.has_value());
    REQUIRE(corrupted.error().code == wealthtracker::platform::CryptoErrorCode::invalid_serialization);
}

TEST_CASE("credential identifiers reject malformed UTF-8", "[platform][credentials]") {
    auto store = wealthtracker::platform::make_system_credential_store(
        "org.wealthtracker.tests");
    const std::string malformed(1, static_cast<char>(0xff));

    const auto result = store->get(malformed);
    REQUIRE_FALSE(result);
    CHECK(result.error().code ==
          wealthtracker::platform::CredentialErrorCode::invalid_argument);
}
