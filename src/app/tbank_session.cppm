module;

#include <algorithm>
#include <string_view>

export module wt.tbank_session;

export namespace wealthtracker::app {

[[nodiscard]] bool is_valid_tbank_session_id(
    const std::string_view value) {
    if (value.size() < 16 || value.size() > 4096) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '.' || character == '-'
            || character == '_';
    });
}

}  // namespace wealthtracker::app
