module;

#include <string>

export module wt.storage.error;

export namespace wealthtracker::storage {

enum class ErrorCode {
    invalid_argument,
    open_failed,
    cipher_unavailable,
    sqlite_error,
    constraint_violation,
    migration_error,
    not_found,
    invalid_state,
};

struct Error {
    ErrorCode code{ErrorCode::sqlite_error};
    int native_code{};
    std::string message;
};

}  // namespace wealthtracker::storage
