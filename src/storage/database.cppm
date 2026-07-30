module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

export module wt.storage.database;

export import wt.storage.error;

export namespace wealthtracker::storage {

enum class StepResult {
    row,
    done,
};

class Statement final {
public:
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    [[nodiscard]] std::expected<void, Error> bind_null(int index);
    [[nodiscard]] std::expected<void, Error> bind(int index, std::int64_t value);
    [[nodiscard]] std::expected<void, Error> bind(int index, double value);
    [[nodiscard]] std::expected<void, Error> bind(int index, std::string_view value);
    [[nodiscard]] std::expected<void, Error> bind(int index, std::span<const std::byte> value);
    [[nodiscard]] std::expected<StepResult, Error> step();
    [[nodiscard]] std::expected<void, Error> reset();

    [[nodiscard]] int column_count() const noexcept;
    [[nodiscard]] bool column_is_null(int index) const noexcept;
    [[nodiscard]] std::int64_t column_int64(int index) const noexcept;
    [[nodiscard]] double column_double(int index) const noexcept;
    [[nodiscard]] std::string column_text(int index) const;
    [[nodiscard]] std::vector<std::byte> column_blob(int index) const;

private:
    friend class Database;
    explicit Statement(sqlite3_stmt* statement) noexcept;

    sqlite3_stmt* statement_{};
};

struct OpenOptions {
    bool create_if_missing{true};
    bool require_cipher{true};
    int busy_timeout_ms{5'000};
};

class Database final {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    [[nodiscard]] static std::expected<Database, Error> open(
        const std::filesystem::path& path,
        std::span<const std::byte> key,
        OpenOptions options = {});

    [[nodiscard]] std::expected<void, Error> execute(std::string_view sql);
    [[nodiscard]] std::expected<Statement, Error> prepare(std::string_view sql);
    [[nodiscard]] std::expected<std::int64_t, Error> scalar_int64(std::string_view sql);
    [[nodiscard]] std::expected<std::string, Error> scalar_text(std::string_view sql);

    [[nodiscard]] std::int64_t last_insert_row_id() const noexcept;
    [[nodiscard]] int changes() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] sqlite3* native_handle() const noexcept;

private:
    explicit Database(sqlite3* database) noexcept;

    sqlite3* database_{};
};

}  // namespace wealthtracker::storage
