module;

#include <expected>

export module wt.storage.transaction;

export import wt.storage.database;

export namespace wealthtracker::storage {

enum class TransactionMode {
    deferred,
    immediate,
    exclusive,
};

class SqlTransaction final {
public:
    ~SqlTransaction();

    SqlTransaction(const SqlTransaction&) = delete;
    SqlTransaction& operator=(const SqlTransaction&) = delete;
    SqlTransaction(SqlTransaction&& other) noexcept;
    SqlTransaction& operator=(SqlTransaction&& other) noexcept;

    [[nodiscard]] static std::expected<SqlTransaction, Error> begin(
        Database& database,
        TransactionMode mode = TransactionMode::immediate);

    [[nodiscard]] std::expected<void, Error> commit();
    [[nodiscard]] std::expected<void, Error> rollback();
    [[nodiscard]] bool active() const noexcept;

private:
    explicit SqlTransaction(Database& database) noexcept;

    Database* database_{};
    bool active_{};
};

}  // namespace wealthtracker::storage
