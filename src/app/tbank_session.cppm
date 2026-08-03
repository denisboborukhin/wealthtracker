module;

#include <algorithm>
#include <cstddef>
#include <string_view>

export module wt.tbank_session;

export namespace wealthtracker::app {

struct TBankSessionRequestEvidence {
    std::string_view scheme;
    std::string_view host;
    std::string_view path;
    int port{-1};
    bool has_user_info{};
    std::size_t session_id_count{};
    std::string_view session_id;
};

[[nodiscard]] bool is_valid_tbank_session_id(
    const std::string_view value) {
    if (value.size() < 16 || value.size() > 4096) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char character) {
        return character >= 0x21 && character <= 0x7e
            && character != ';';
    });
}

[[nodiscard]] bool is_tbank_session_request(
    const TBankSessionRequestEvidence& evidence) {
    constexpr std::string_view apiPrefix = "/api/common/v1/";
    const bool exactHost = evidence.host == "api-common.tinkoff.ru"
        || evidence.host == "www.tbank.ru";
    return evidence.scheme == "https" && exactHost
        && (evidence.port == -1 || evidence.port == 443)
        && !evidence.has_user_info
        && evidence.path.starts_with(apiPrefix)
        && evidence.path.size() > apiPrefix.size()
        && evidence.session_id_count == 1
        && is_valid_tbank_session_id(evidence.session_id);
}

}  // namespace wealthtracker::app
