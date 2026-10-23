#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import wt.platform.credential_store;

namespace {

using namespace wealthtracker::platform;
using Bytes = std::vector<std::byte>;
constexpr auto bundle_account = "credential-vault-v1";

struct FakeState {
    std::map<std::string, Bytes> values;
    std::map<std::string, unsigned> reads;
    unsigned writes = 0;
    unsigned removals = 0;
    bool deny_bundle = false;
    std::string deny_legacy_account;
    bool fail_write = false;
    bool fail_remove = false;
    bool supported = true;
};

CredentialError failure() {
    return {CredentialErrorCode::access_denied, 0, "fake access denied"};
}

class FakeStore final : public CredentialStore {
public:
    explicit FakeStore(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

    bool supported() const noexcept override { return state_->supported; }

    std::expected<void, CredentialError> put(
        std::string_view account, std::span<const std::byte> secret) override {
        ++state_->writes;
        if (state_->fail_write) return std::unexpected(failure());
        state_->values[std::string(account)] = Bytes(secret.begin(), secret.end());
        return {};
    }

    std::expected<SecureBuffer, CredentialError> get(std::string_view account) override {
        ++state_->reads[std::string(account)];
        if (account == bundle_account && state_->deny_bundle) return std::unexpected(failure());
        if (account == state_->deny_legacy_account) return std::unexpected(failure());
        const auto found = state_->values.find(std::string(account));
        if (found == state_->values.end()) {
            return std::unexpected(CredentialError{CredentialErrorCode::not_found, 0, "fake missing"});
        }
        auto result = SecureBuffer::create(found->second.size());
        REQUIRE(result);
        std::copy(found->second.begin(), found->second.end(), result->bytes().begin());
        return std::move(*result);
    }

    std::expected<void, CredentialError> remove(std::string_view account) override {
        ++state_->removals;
        if (state_->fail_remove) return std::unexpected(failure());
        state_->values.erase(std::string(account));
        return {};
    }

private:
    std::shared_ptr<FakeState> state_;
};

std::unique_ptr<CredentialStore> vault(const std::shared_ptr<FakeState>& state) {
    return make_bundled_credential_store(std::make_unique<FakeStore>(state));
}

Bytes value(CredentialStore& store, std::string_view account) {
    auto result = store.get(account);
    REQUIRE(result);
    return Bytes(result->bytes().begin(), result->bytes().end());
}

void check_missing(CredentialStore& store, std::string_view account) {
    auto result = store.get(account);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == CredentialErrorCode::not_found);
}

}  // namespace

TEST_CASE("credential vault migrates exact bytes and reads its bundle once per launch",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    Bytes key(32);
    for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::byte>(i);
    const Bytes token{std::byte{0}, std::byte{255}, std::byte{42}};
    state->values["balance-history-key"] = key;
    state->values["t-invest-readonly"] = token;
    auto store = vault(state);

    CHECK(value(*store, "balance-history-key") == key);
    CHECK(value(*store, "t-invest-readonly") == token);
    CHECK(value(*store, "t-invest-readonly") == token);
    CHECK(state->reads[bundle_account] == 1);
    CHECK(state->reads["balance-history-key"] == 1);
    CHECK(state->reads["t-invest-readonly"] == 1);
    CHECK_FALSE(state->values.contains("balance-history-key"));
    CHECK_FALSE(state->values.contains("t-invest-readonly"));

    store.reset();
    state->reads.clear();
    store = vault(state);
    CHECK(value(*store, "balance-history-key") == key);
    CHECK(value(*store, "t-invest-readonly") == token);
    CHECK(value(*store, "balance-history-key") == key);
    REQUIRE(state->reads.size() == 1);
    CHECK(state->reads[bundle_account] == 1);
}

TEST_CASE("credential vault caches missing legacy lookups only for the process lifetime",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    auto store = vault(state);
    check_missing(*store, "missing");
    check_missing(*store, "missing");
    CHECK(state->reads["missing"] == 1);
    CHECK(state->writes == 0);
    const Bytes secret{std::byte{23}};
    state->values["missing"] = secret;
    check_missing(*store, "missing");
    store = vault(state);
    CHECK(value(*store, "missing") == secret);
}

TEST_CASE("credential vault failed migration preserves the original credential",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    const Bytes secret{std::byte{19}, std::byte{0}};
    state->values["legacy"] = secret;
    state->fail_write = true;
    auto store = vault(state);
    REQUIRE_FALSE(store->get("legacy"));
    CHECK(state->values["legacy"] == secret);
    CHECK_FALSE(state->values.contains(bundle_account));
    CHECK(state->removals == 0);
    state->fail_write = false;
    CHECK(value(*store, "legacy") == secret);
    CHECK_FALSE(state->values.contains("legacy"));
}

TEST_CASE("credential vault failed writes preserve committed values and database key",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    auto store = vault(state);
    const Bytes key(32, std::byte{91});
    const Bytes old_secret{std::byte{1}};
    const Bytes replacement{std::byte{2}};
    REQUIRE(store->put("balance-history-key", key));
    REQUIRE(store->put("token", old_secret));
    const auto committed = state->values.at(bundle_account);
    state->fail_write = true;
    CHECK_FALSE(store->put("token", replacement));
    CHECK_FALSE(store->remove("token"));
    CHECK(value(*store, "token") == old_secret);
    CHECK(value(*store, "balance-history-key") == key);
    CHECK(state->values.at(bundle_account) == committed);
    store = vault(state);
    CHECK(value(*store, "token") == old_secret);
}

TEST_CASE("credential vault tombstones prevent resurrection after legacy cleanup fails",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    const Bytes secret{std::byte{71}};
    state->values["legacy"] = secret;
    state->fail_remove = true;
    auto store = vault(state);
    CHECK(value(*store, "legacy") == secret);
    CHECK(state->values.contains("legacy"));
    REQUIRE_FALSE(store->remove("legacy"));
    check_missing(*store, "legacy");
    state->reads.clear();
    store = vault(state);
    check_missing(*store, "legacy");
    CHECK(state->reads["legacy"] == 0);
    CHECK(state->values["legacy"] == secret);
    state->fail_remove = false;
    REQUIRE(store->remove("legacy"));
    CHECK_FALSE(state->values.contains("legacy"));
    REQUIRE(store->put("legacy", secret));
    CHECK(value(*store, "legacy") == secret);
}

TEST_CASE("credential vault denied bundle is attempted once without legacy fallback",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    state->deny_bundle = true;
    state->values["legacy"] = Bytes{std::byte{1}};
    auto store = vault(state);
    CHECK_FALSE(store->get("legacy"));
    CHECK_FALSE(store->put("new", state->values["legacy"]));
    CHECK_FALSE(store->remove("legacy"));
    state->deny_bundle = false;
    CHECK_FALSE(store->get("legacy"));
    CHECK(state->reads[bundle_account] == 1);
    CHECK(state->reads["legacy"] == 0);
    CHECK(state->writes == 0);
    CHECK(state->removals == 0);
}

TEST_CASE("credential vault stops all storage attempts after denied legacy migration",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    const Bytes secret{std::byte{5}};
    state->values["legacy"] = secret;
    state->values["another"] = secret;
    auto store = vault(state);
    REQUIRE(store->put("saved", secret));
    state->deny_legacy_account = "legacy";
    auto denied = store->get("legacy");
    REQUIRE_FALSE(denied);
    CHECK(denied.error().code == CredentialErrorCode::access_denied);
    const auto reads = state->reads;
    const auto writes = state->writes;
    const auto removals = state->removals;
    const auto values = state->values;

    state->deny_legacy_account.clear();
    for (const auto* account : {"legacy", "another", "saved", "missing"}) {
        auto result = store->get(account);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CredentialErrorCode::access_denied);
    }
    CHECK_FALSE(store->put("saved", secret));
    CHECK_FALSE(store->remove("legacy"));
    CHECK(state->reads == reads);
    CHECK(state->writes == writes);
    CHECK(state->removals == removals);
    CHECK(state->values == values);

    store = vault(state);
    CHECK(value(*store, "legacy") == secret);
    CHECK(value(*store, "saved") == secret);
}

TEST_CASE("credential vault rejects malformed versioned bundles without replacing them",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    auto store = vault(state);
    const Bytes secret{std::byte{3}};
    REQUIRE(store->put("a", secret));
    auto& encoded = state->values.at(bundle_account);
    SECTION("wrong magic") { encoded[0] = std::byte{0}; }
    SECTION("unsupported version") { encoded[8] = std::byte{2}; }
    SECTION("truncated header") { encoded.resize(8); }
    SECTION("truncated secret") { encoded.pop_back(); }
    SECTION("trailing data") { encoded.push_back(std::byte{0}); }
    SECTION("impossible count") { encoded[12] = std::byte{255}; }
    SECTION("invalid account") { encoded[24] = std::byte{255}; }
    SECTION("oversized account length") { encoded[17] = std::byte{255}; }
    SECTION("oversized secret length") { encoded[23] = std::byte{255}; }
    SECTION("duplicate account") {
        const Bytes entry(encoded.begin() + 16, encoded.end());
        encoded.insert(encoded.end(), entry.begin(), entry.end());
        encoded[12] = std::byte{2};
    }
    SECTION("oversized bundle") { encoded.resize(65'537); }
    const auto corrupt = encoded;
    state->values["legacy"] = secret;
    state->reads.clear();
    const auto writes_before = state->writes;
    store = vault(state);
    CHECK_FALSE(store->get("legacy"));
    CHECK_FALSE(store->put("new", secret));
    CHECK_FALSE(store->remove("legacy"));
    CHECK(state->reads[bundle_account] == 1);
    CHECK(state->reads["legacy"] == 0);
    CHECK(state->writes == writes_before);
    CHECK(state->values.at(bundle_account) == corrupt);
    CHECK(state->values["legacy"] == secret);
}

TEST_CASE("credential vault enforces aggregate size and validates identifiers before storage",
          "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    auto store = vault(state);
    const Bytes secret{std::byte{1}};
    for (const auto& invalid : {std::string{}, std::string(bundle_account),
                                std::string(1, static_cast<char>(255)),
                                std::string("a\0b", 3), std::string(1'025, 'a')}) {
        CHECK_FALSE(store->get(invalid));
        CHECK_FALSE(store->put(invalid, secret));
        CHECK_FALSE(store->remove(invalid));
    }
    CHECK_FALSE(store->put("a", {}));
    CHECK(state->reads.empty());
    CHECK(state->writes == 0);
    const Bytes largest(65'536 - 16 - 8 - 1, std::byte{7});
    REQUIRE(store->put("a", largest));
    CHECK(state->values.at(bundle_account).size() == 65'536);
    CHECK_FALSE(store->put("b", secret));
    CHECK(value(*store, "a") == largest);
    store = vault(state);
    CHECK(value(*store, "a") == largest);
}

TEST_CASE("credential vault leaves unsupported stores unwrapped", "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    state->supported = false;
    auto source = std::make_unique<FakeStore>(state);
    auto* identity = source.get();
    auto wrapped = make_bundled_credential_store(std::move(source));
    CHECK(wrapped.get() == identity);
    CHECK_FALSE(wrapped->supported());
    CHECK(state->reads.empty());
    CHECK_FALSE(make_bundled_credential_store(nullptr));
}

TEST_CASE("credential vault avoids authorizing writes for unchanged secrets", "[platform][credentials]") {
    auto state = std::make_shared<FakeState>();
    auto store = vault(state);
    const Bytes token{std::byte{1}, std::byte{2}, std::byte{3}};
    REQUIRE(store->put("session", token));
    const auto writes = state->writes;
    const auto removals = state->removals;
    state->fail_write = true;
    REQUIRE(store->put("session", token));
    CHECK(state->writes == writes);
    CHECK(state->removals == removals);
    CHECK(value(*store, "session") == token);
}
