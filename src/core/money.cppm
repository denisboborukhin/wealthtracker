module;

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <compare>
#include <cctype>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

export module wt.core.money;
export import wt.core.decimal;

export namespace wealthtracker::core {

enum class MoneyError {
    invalid_currency,
    currency_mismatch,
    overflow,
    invalid_rate,
};

class Currency final {
public:
    Currency() = default;

    [[nodiscard]] static std::expected<Currency, MoneyError> parse(std::string_view code);
    [[nodiscard]] static Currency rub();

    [[nodiscard]] std::string code() const;
    [[nodiscard]] auto operator<=>(const Currency&) const = default;

private:
    explicit Currency(std::array<char, 3> code);

    std::array<char, 3> code_{'R', 'U', 'B'};
};

class Money final {
public:
    Money() = default;

    [[nodiscard]] static std::expected<Money, MoneyError> from_minor(
        std::int64_t minor_units,
        std::string_view currency);
    [[nodiscard]] static Money from_minor(std::int64_t minor_units, Currency currency);

    [[nodiscard]] std::int64_t minor_units() const noexcept;
    [[nodiscard]] const Currency& currency() const noexcept;
    [[nodiscard]] bool is_zero() const noexcept;
    [[nodiscard]] bool is_positive() const noexcept;
    [[nodiscard]] bool is_negative() const noexcept;

    [[nodiscard]] std::expected<Money, MoneyError> add(const Money& other) const;
    [[nodiscard]] std::expected<Money, MoneyError> subtract(const Money& other) const;
    [[nodiscard]] std::expected<Money, MoneyError> negated() const;
    [[nodiscard]] std::expected<Money, MoneyError> multiplied_by(
        const Decimal& factor) const;

    [[nodiscard]] auto operator<=>(const Money&) const = default;

private:
    Money(std::int64_t minor_units, Currency currency);

    std::int64_t minor_units_{0};
    Currency currency_{};
};

}  // namespace wealthtracker::core

namespace wealthtracker::core {

Currency::Currency(const std::array<char, 3> code) : code_(code) {}

std::expected<Currency, MoneyError> Currency::parse(const std::string_view code) {
    if (code.size() != 3) {
        return std::unexpected(MoneyError::invalid_currency);
    }
    std::array<char, 3> normalized{};
    for (std::size_t index = 0; index < code.size(); ++index) {
        const auto character = static_cast<unsigned char>(code[index]);
        if (character > 127 || std::isalpha(character) == 0) {
            return std::unexpected(MoneyError::invalid_currency);
        }
        normalized[index] = static_cast<char>(std::toupper(character));
    }
    return Currency{normalized};
}

Currency Currency::rub() {
    return Currency{{'R', 'U', 'B'}};
}

std::string Currency::code() const {
    return {code_.begin(), code_.end()};
}

Money::Money(const std::int64_t minor_units, Currency currency)
    : minor_units_(minor_units), currency_(std::move(currency)) {}

std::expected<Money, MoneyError> Money::from_minor(
    const std::int64_t minor_units,
    const std::string_view currency) {
    auto parsed_currency = Currency::parse(currency);
    if (!parsed_currency) {
        return std::unexpected(parsed_currency.error());
    }
    return Money{minor_units, *parsed_currency};
}

Money Money::from_minor(const std::int64_t minor_units, Currency currency) {
    return Money{minor_units, std::move(currency)};
}

std::int64_t Money::minor_units() const noexcept {
    return minor_units_;
}

const Currency& Money::currency() const noexcept {
    return currency_;
}

bool Money::is_zero() const noexcept {
    return minor_units_ == 0;
}

bool Money::is_positive() const noexcept {
    return minor_units_ > 0;
}

bool Money::is_negative() const noexcept {
    return minor_units_ < 0;
}

std::expected<Money, MoneyError> Money::add(const Money& other) const {
    if (currency_ != other.currency_) {
        return std::unexpected(MoneyError::currency_mismatch);
    }
    if ((other.minor_units_ > 0 &&
         minor_units_ > std::numeric_limits<std::int64_t>::max() - other.minor_units_) ||
        (other.minor_units_ < 0 &&
         minor_units_ < std::numeric_limits<std::int64_t>::min() - other.minor_units_)) {
        return std::unexpected(MoneyError::overflow);
    }
    return Money{minor_units_ + other.minor_units_, currency_};
}

std::expected<Money, MoneyError> Money::subtract(const Money& other) const {
    if (currency_ != other.currency_) {
        return std::unexpected(MoneyError::currency_mismatch);
    }
    if ((other.minor_units_ > 0 &&
         minor_units_ < std::numeric_limits<std::int64_t>::min() + other.minor_units_) ||
        (other.minor_units_ < 0 &&
         minor_units_ > std::numeric_limits<std::int64_t>::max() + other.minor_units_)) {
        return std::unexpected(MoneyError::overflow);
    }
    return Money{minor_units_ - other.minor_units_, currency_};
}

std::expected<Money, MoneyError> Money::negated() const {
    if (minor_units_ == std::numeric_limits<std::int64_t>::min()) {
        return std::unexpected(MoneyError::overflow);
    }
    return Money{-minor_units_, currency_};
}

std::expected<Money, MoneyError> Money::multiplied_by(const Decimal& factor) const {
    auto product = Decimal::from_integer(minor_units_).multiply(factor);
    if (!product) {
        return std::unexpected(MoneyError::invalid_rate);
    }
    auto rounded = product->rescale(0);
    if (!rounded) {
        return std::unexpected(MoneyError::invalid_rate);
    }
    using boost::multiprecision::cpp_int;
    const cpp_int minimum = std::numeric_limits<std::int64_t>::min();
    const cpp_int maximum = std::numeric_limits<std::int64_t>::max();
    if (rounded->mantissa() < minimum || rounded->mantissa() > maximum) {
        return std::unexpected(MoneyError::overflow);
    }
    return Money{rounded->mantissa().convert_to<std::int64_t>(), currency_};
}

}  // namespace wealthtracker::core
