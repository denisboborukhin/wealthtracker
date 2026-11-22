module;
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module wt.storage.database;

namespace wealthtracker::storage {
namespace {

[[nodiscard]] Error make_error(
    sqlite3* database,
    int native_code,
    std::string_view operation,
    ErrorCode code = ErrorCode::sqlite_error) {
    std::string message(operation);
    if (database != nullptr) {
        message.append(": ");
        message.append(sqlite3_errmsg(database));
    }
    return Error{code, native_code, std::move(message)};
}

[[nodiscard]] Error make_statement_error(
    sqlite3_stmt* statement,
    int native_code,
    std::string_view operation,
    ErrorCode code = ErrorCode::sqlite_error) {
    return make_error(sqlite3_db_handle(statement), native_code, operation, code);
}

[[nodiscard]] std::string hex_key(std::span<const std::byte> key) {
    static constexpr std::array<char, 16> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result;
    result.resize(key.size() * 2);
    for (std::size_t index = 0; index < key.size(); ++index) {
        const auto value = std::to_integer<unsigned char>(key[index]);
        result[index * 2] = digits[value >> 4U];
        result[index * 2 + 1] = digits[value & 0x0fU];
    }
    return result;
}

void erase_string(std::string& value) noexcept {
    volatile char* bytes = value.data();
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes[index] = '\0';
    }
    value.clear();
}

}  // namespace

Statement::Statement(sqlite3_stmt* statement) noexcept : statement_(statement) {}

Statement::~Statement() {
    if (statement_ != nullptr) {
        sqlite3_finalize(statement_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : statement_(std::exchange(other.statement_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (statement_ != nullptr) {
        sqlite3_finalize(statement_);
    }
    statement_ = std::exchange(other.statement_, nullptr);
    return *this;
}

std::expected<void, Error> Statement::bind_null(int index) {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    const int result = sqlite3_bind_null(statement_, index);
    if (result != SQLITE_OK) {
        return std::unexpected(make_statement_error(statement_, result, "bind null"));
    }
    return {};
}

std::expected<void, Error> Statement::bind(int index, std::int64_t value) {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    const int result = sqlite3_bind_int64(statement_, index, value);
    if (result != SQLITE_OK) {
        return std::unexpected(make_statement_error(statement_, result, "bind integer"));
    }
    return {};
}

std::expected<void, Error> Statement::bind(int index, std::string_view value) {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    if (value.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected(Error{
            ErrorCode::invalid_argument, SQLITE_TOOBIG, "text binding exceeds SQLite limits"});
    }
    const int result = sqlite3_bind_text(
        statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (result != SQLITE_OK) {
        return std::unexpected(make_statement_error(statement_, result, "bind text"));
    }
    return {};
}

std::expected<void, Error> Statement::bind(int index, std::span<const std::byte> value) {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    if (value.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected(Error{
            ErrorCode::invalid_argument, SQLITE_TOOBIG, "blob binding exceeds SQLite limits"});
    }
    const int result =
        value.empty()
            ? sqlite3_bind_zeroblob(statement_, index, 0)
            : sqlite3_bind_blob(
                  statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (result != SQLITE_OK) {
        return std::unexpected(make_statement_error(statement_, result, "bind blob"));
    }
    return {};
}

std::expected<StepResult, Error> Statement::step() {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) {
        return StepResult::row;
    }
    if (result == SQLITE_DONE) {
        return StepResult::done;
    }
    const auto code = result == SQLITE_CONSTRAINT ? ErrorCode::constraint_violation
                                                  : ErrorCode::sqlite_error;
    return std::unexpected(make_statement_error(statement_, result, "step statement", code));
}

std::expected<void, Error> Statement::reset() {
    if (statement_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "statement is not active"});
    }
    int result = sqlite3_reset(statement_);
    if (result == SQLITE_OK) {
        result = sqlite3_clear_bindings(statement_);
    }
    if (result != SQLITE_OK) {
        return std::unexpected(make_statement_error(statement_, result, "reset statement"));
    }
    return {};
}

int Statement::column_count() const noexcept {
    return statement_ == nullptr ? 0 : sqlite3_column_count(statement_);
}

bool Statement::column_is_null(int index) const noexcept {
    return statement_ == nullptr || sqlite3_column_type(statement_, index) == SQLITE_NULL;
}

std::int64_t Statement::column_int64(int index) const noexcept {
    return statement_ == nullptr ? 0 : sqlite3_column_int64(statement_, index);
}

std::string Statement::column_text(int index) const {
    if (statement_ == nullptr) {
        return {};
    }
    const auto* value = sqlite3_column_text(statement_, index);
    const int size = sqlite3_column_bytes(statement_, index);
    if (value == nullptr || size <= 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

std::vector<std::byte> Statement::column_blob(int index) const {
    if (statement_ == nullptr) {
        return {};
    }
    const auto* value = static_cast<const std::byte*>(sqlite3_column_blob(statement_, index));
    const int size = sqlite3_column_bytes(statement_, index);
    if (value == nullptr || size <= 0) {
        return {};
    }
    return {value, value + size};
}

Database::Database(sqlite3* database) noexcept : database_(database) {}

Database::~Database() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
    }
}

Database::Database(Database&& other) noexcept
    : database_(std::exchange(other.database_, nullptr)) {}

Database& Database::operator=(Database&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
    }
    database_ = std::exchange(other.database_, nullptr);
    return *this;
}

std::expected<Database, Error> Database::open(
    const std::filesystem::path& path,
    std::span<const std::byte> key,
    OpenOptions options) {
    if (path.empty() || key.size() != 32) {
        return std::unexpected(Error{
            ErrorCode::invalid_argument,
            SQLITE_MISUSE,
            "database path must not be empty and key must be exactly 32 bytes"});
    }
    if (options.busy_timeout_ms < 0) {
        return std::unexpected(Error{
            ErrorCode::invalid_argument, SQLITE_MISUSE, "busy timeout cannot be negative"});
    }

    sqlite3* handle = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
    if (options.create_if_missing) {
        flags |= SQLITE_OPEN_CREATE;
    }
    const auto utf8_path = path.u8string();
    const int result =
        sqlite3_open_v2(reinterpret_cast<const char*>(utf8_path.c_str()), &handle, flags, nullptr);
    if (result != SQLITE_OK) {
        auto error = make_error(handle, result, "open database", ErrorCode::open_failed);
        if (handle != nullptr) {
            sqlite3_close_v2(handle);
        }
        return std::unexpected(std::move(error));
    }

    Database database(handle);
    std::string key_hex = hex_key(key);
    std::string key_command = "PRAGMA key = \"x'";
    key_command.append(key_hex);
    key_command.append("'\";");
    erase_string(key_hex);
    char* key_error_message = nullptr;
    const int key_native_result =
        sqlite3_exec(handle, key_command.c_str(), nullptr, nullptr, &key_error_message);
    erase_string(key_command);
    if (key_native_result != SQLITE_OK) {
        std::string message = "apply database key";
        if (key_error_message != nullptr) {
            message.append(": ");
            message.append(key_error_message);
            sqlite3_free(key_error_message);
        }
        return std::unexpected(
            Error{ErrorCode::open_failed, key_native_result, std::move(message)});
    }

    if (options.require_cipher) {
        auto cipher_version = database.scalar_text("PRAGMA cipher_version;");
        if (!cipher_version || cipher_version->empty()) {
            return std::unexpected(Error{
                ErrorCode::cipher_unavailable,
                SQLITE_NOTFOUND,
                "SQLCipher is required but PRAGMA cipher_version returned no value"});
        }
    }

    if (const int timeout_result = sqlite3_busy_timeout(handle, options.busy_timeout_ms);
        timeout_result != SQLITE_OK) {
        return std::unexpected(make_error(handle, timeout_result, "set busy timeout"));
    }

#ifdef SQLITE_DBCONFIG_DEFENSIVE
    int defensive_enabled = 0;
    const int defensive_result =
        sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, &defensive_enabled);
    if (defensive_result != SQLITE_OK || defensive_enabled != 1) {
        return std::unexpected(make_error(
            handle, defensive_result, "enable defensive database mode"));
    }
#endif
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
    int trusted_schema_enabled = 1;
    const int trusted_schema_result =
        sqlite3_db_config(handle, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &trusted_schema_enabled);
    if (trusted_schema_result != SQLITE_OK || trusted_schema_enabled != 0) {
        return std::unexpected(make_error(
            handle, trusted_schema_result, "disable trusted database schema"));
    }
#endif

    constexpr std::string_view initialization = R"sql(
        PRAGMA foreign_keys = ON;
        PRAGMA secure_delete = ON;
        PRAGMA temp_store = MEMORY;
        PRAGMA cipher_memory_security = ON;
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = FULL;
    )sql";
    if (auto init_result = database.execute(initialization); !init_result) {
        return std::unexpected(init_result.error());
    }

    const std::array<std::pair<std::string_view, std::int64_t>, 4> required_pragmas{{
        {"PRAGMA foreign_keys;", 1},
        {"PRAGMA secure_delete;", 1},
        {"PRAGMA temp_store;", 2},
        {"PRAGMA synchronous;", 2},
    }};
    for (const auto& [query, required_value] : required_pragmas) {
        auto value = database.scalar_int64(query);
        if (!value || *value != required_value) {
            return std::unexpected(Error{
                ErrorCode::open_failed,
                value ? SQLITE_MISUSE : value.error().native_code,
                "required database safety setting was not applied"});
        }
    }
    auto journal_mode = database.scalar_text("PRAGMA journal_mode;");
    if (!journal_mode || *journal_mode != "wal") {
        return std::unexpected(Error{
            ErrorCode::open_failed,
            journal_mode ? SQLITE_MISUSE : journal_mode.error().native_code,
            "write-ahead logging was not enabled"});
    }
    if (options.require_cipher) {
        auto memory_security = database.scalar_int64("PRAGMA cipher_memory_security;");
        if (!memory_security || *memory_security != 1) {
            return std::unexpected(Error{
                ErrorCode::open_failed,
                memory_security ? SQLITE_MISUSE : memory_security.error().native_code,
                "SQLCipher memory security was not enabled"});
        }
    }
    return database;
}

std::expected<void, Error> Database::execute(std::string_view sql) {
    if (database_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "database is closed"});
    }
    std::string statement(sql);
    char* message = nullptr;
    const int result = sqlite3_exec(database_, statement.c_str(), nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        std::string detail = "execute SQL";
        if (message != nullptr) {
            detail.append(": ");
            detail.append(message);
            sqlite3_free(message);
        }
        const auto code = result == SQLITE_CONSTRAINT ? ErrorCode::constraint_violation
                                                      : ErrorCode::sqlite_error;
        return std::unexpected(Error{code, result, std::move(detail)});
    }
    return {};
}

std::expected<Statement, Error> Database::prepare(std::string_view sql) {
    if (database_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, SQLITE_MISUSE, "database is closed"});
    }
    if (sql.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected(
            Error{ErrorCode::invalid_argument, SQLITE_TOOBIG, "SQL exceeds SQLite limits"});
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(
        database_, sql.data(), static_cast<int>(sql.size()), &statement, nullptr);
    if (result != SQLITE_OK) {
        return std::unexpected(make_error(database_, result, "prepare SQL"));
    }
    return Statement(statement);
}

std::expected<std::int64_t, Error> Database::scalar_int64(std::string_view sql) {
    auto statement = prepare(sql);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    auto result = statement->step();
    if (!result) {
        return std::unexpected(result.error());
    }
    if (*result != StepResult::row || statement->column_count() < 1) {
        return std::unexpected(
            Error{ErrorCode::not_found, SQLITE_NOTFOUND, "query returned no scalar value"});
    }
    return statement->column_int64(0);
}

std::expected<std::string, Error> Database::scalar_text(std::string_view sql) {
    auto statement = prepare(sql);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    auto result = statement->step();
    if (!result) {
        return std::unexpected(result.error());
    }
    if (*result != StepResult::row || statement->column_count() < 1) {
        return std::unexpected(
            Error{ErrorCode::not_found, SQLITE_NOTFOUND, "query returned no scalar value"});
    }
    return statement->column_text(0);
}

std::int64_t Database::last_insert_row_id() const noexcept {
    return database_ == nullptr ? 0 : sqlite3_last_insert_rowid(database_);
}

int Database::changes() const noexcept {
    return database_ == nullptr ? 0 : sqlite3_changes(database_);
}

bool Database::is_open() const noexcept {
    return database_ != nullptr;
}

sqlite3* Database::native_handle() const noexcept {
    return database_;
}

}  // namespace wealthtracker::storage
