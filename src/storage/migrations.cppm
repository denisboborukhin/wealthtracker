module;

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

export module wt.storage.migrations;

export import wt.storage.database;

export namespace wealthtracker::storage {

struct Migration {
    std::int64_t version;
    std::string_view name;
    std::string_view sql;
};

class MigrationRunner final {
public:
    explicit MigrationRunner(Database& database) noexcept;

    [[nodiscard]] std::expected<std::int64_t, Error> current_version();
    [[nodiscard]] std::expected<void, Error> apply(std::span<const Migration> migrations);

private:
    Database& database_;
};

[[nodiscard]] std::span<const Migration> default_migrations() noexcept;

}  // namespace wealthtracker::storage
