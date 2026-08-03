module;

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

export module wt.tbank_tls;

export namespace wealthtracker::app {

struct TBankCertificateEvidence {
    std::string host;
    int error_type{};
    bool overridable{};
    std::span<const std::string> chain_sha256;
};

[[nodiscard]] bool is_trusted_tbank_certificate(
    const TBankCertificateEvidence& evidence) {
    constexpr int certificateAuthorityInvalid = -202;
    constexpr std::string_view requiredHost = "api-common.tinkoff.ru";
    constexpr std::string_view rootSha256 =
        "d26d2d0231b7c39f92cc738512ba54103519e4405d68b5bd703e9788ca8ecf31";
    if (!evidence.overridable
        || evidence.error_type != certificateAuthorityInvalid
        || evidence.host != requiredHost
        || evidence.chain_sha256.empty()) {
        return false;
    }

    // Chromium has already checked the host name, validity period, and chain
    // signatures when the only reported error is an unknown authority. Pin
    // the terminal trust anchor while allowing legitimate intermediate
    // rotations below that exact root.
    return evidence.chain_sha256.back() == rootSha256;
}

[[nodiscard]] bool is_blocked_tbank_login_subresource(
    const std::string_view host) {
    constexpr std::array blockedHosts{
        std::string_view{"mddc.tbank.ru"},
        std::string_view{"mddc.tinkoff.ru"},
        std::string_view{"api-statist.tinkoff.ru"},
        std::string_view{"api-statist.dev-tcsgroup.io"},
    };
    return std::ranges::find(blockedHosts, host) != blockedHosts.end();
}

}  // namespace wealthtracker::app
