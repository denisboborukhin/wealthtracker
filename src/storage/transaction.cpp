module;
#include <expected>
#include <string_view>
#include <utility>

module wt.storage.transaction;

namespace wealthtracker::storage {

SqlTransaction::SqlTransaction(Database& database) noexcept
    : database_(&database), active_(true) {}

SqlTransaction::~SqlTransaction() {
    if (active_ && database_ != nullptr) {
        // A destructor must not throw. SQLite will also roll the transaction back
        // when the connection closes if this best-effort rollback fails.
        static_cast<void>(database_->execute("ROLLBACK;"));
    }
}

SqlTransaction::SqlTransaction(SqlTransaction&& other) noexcept
    : database_(std::exchange(other.database_, nullptr)),
      active_(std::exchange(other.active_, false)) {}

SqlTransaction& SqlTransaction::operator=(SqlTransaction&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (active_ && database_ != nullptr) {
        static_cast<void>(database_->execute("ROLLBACK;"));
    }
    database_ = std::exchange(other.database_, nullptr);
    active_ = std::exchange(other.active_, false);
    return *this;
}

std::expected<SqlTransaction, Error> SqlTransaction::begin(
    Database& database,
    TransactionMode mode) {
    const char* sql = "BEGIN IMMEDIATE;";
    switch (mode) {
        case TransactionMode::deferred:
            sql = "BEGIN DEFERRED;";
            break;
        case TransactionMode::immediate:
            break;
        case TransactionMode::exclusive:
            sql = "BEGIN EXCLUSIVE;";
            break;
    }
    if (auto result = database.execute(sql); !result) {
        return std::unexpected(result.error());
    }
    return SqlTransaction(database);
}

std::expected<void, Error> SqlTransaction::commit() {
    if (!active_ || database_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "transaction is not active"});
    }
    if (auto result = database_->execute("COMMIT;"); !result) {
        return result;
    }
    active_ = false;
    return {};
}

std::expected<void, Error> SqlTransaction::rollback() {
    if (!active_ || database_ == nullptr) {
        return std::unexpected(
            Error{ErrorCode::invalid_state, 0, "transaction is not active"});
    }
    if (auto result = database_->execute("ROLLBACK;"); !result) {
        return result;
    }
    active_ = false;
    return {};
}

bool SqlTransaction::active() const noexcept {
    return active_;
}

}  // namespace wealthtracker::storage
