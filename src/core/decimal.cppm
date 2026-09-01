module;

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

export module wt.core.decimal;

export namespace wealthtracker::core {

enum class DecimalError {
    invalid_format,
    scale_out_of_range,
    division_by_zero,
    out_of_range,
};

class Decimal final {
public:
    static constexpr std::int32_t max_scale = 38;

    Decimal() = default;

    [[nodiscard]] static Decimal from_integer(std::int64_t value);
    [[nodiscard]] static std::expected<Decimal, DecimalError> from_parts(
        boost::multiprecision::cpp_int mantissa,
        std::int32_t scale);
    [[nodiscard]] static std::expected<Decimal, DecimalError> parse(std::string_view value);

    [[nodiscard]] const boost::multiprecision::cpp_int& mantissa() const noexcept;
    [[nodiscard]] std::int32_t scale() const noexcept;
    [[nodiscard]] bool is_zero() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::expected<std::int64_t, DecimalError> to_int64_rounded() const;

    [[nodiscard]] std::expected<Decimal, DecimalError> add(const Decimal& other) const;
    [[nodiscard]] std::expected<Decimal, DecimalError> subtract(const Decimal& other) const;
    [[nodiscard]] std::expected<Decimal, DecimalError> multiply(const Decimal& other) const;
    [[nodiscard]] std::expected<Decimal, DecimalError> divide(
        const Decimal& other,
        std::int32_t result_scale = 12) const;
    [[nodiscard]] std::expected<Decimal, DecimalError> rescale(
        std::int32_t target_scale) const;

    [[nodiscard]] std::strong_ordering operator<=>(const Decimal& other) const;
    [[nodiscard]] bool operator==(const Decimal& other) const;

private:
    Decimal(boost::multiprecision::cpp_int mantissa, std::int32_t scale);
    void normalize();

    boost::multiprecision::cpp_int mantissa_{0};
    std::int32_t scale_{0};
};

}  // namespace wealthtracker::core

namespace wealthtracker::core {
namespace {

using boost::multiprecision::cpp_int;

cpp_int power_of_ten(std::int32_t exponent) {
    cpp_int value = 1;
    for (std::int32_t index = 0; index < exponent; ++index) {
        value *= 10;
    }
    return value;
}

cpp_int absolute(cpp_int value) {
    return value < 0 ? -value : value;
}

cpp_int divide_round_half_away_from_zero(const cpp_int& numerator, const cpp_int& denominator) {
    cpp_int quotient = numerator / denominator;
    const cpp_int remainder = numerator % denominator;
    if (absolute(remainder) * 2 >= absolute(denominator) && remainder != 0) {
        quotient += ((numerator < 0) != (denominator < 0)) ? -1 : 1;
    }
    return quotient;
}

}  // namespace

Decimal::Decimal(cpp_int mantissa, const std::int32_t scale)
    : mantissa_(std::move(mantissa)), scale_(scale) {
    normalize();
}

Decimal Decimal::from_integer(const std::int64_t value) {
    return Decimal{value, 0};
}

std::expected<Decimal, DecimalError> Decimal::from_parts(
    cpp_int mantissa,
    const std::int32_t scale) {
    if (scale < 0 || scale > max_scale) {
        return std::unexpected(DecimalError::scale_out_of_range);
    }
    return Decimal{std::move(mantissa), scale};
}

std::expected<Decimal, DecimalError> Decimal::parse(const std::string_view value) {
    if (value.empty()) {
        return std::unexpected(DecimalError::invalid_format);
    }

    std::size_t cursor = 0;
    bool negative = false;
    if (value[cursor] == '+' || value[cursor] == '-') {
        negative = value[cursor] == '-';
        ++cursor;
    }
    if (cursor == value.size()) {
        return std::unexpected(DecimalError::invalid_format);
    }

    cpp_int mantissa = 0;
    std::int32_t scale = 0;
    bool found_digit = false;
    bool found_separator = false;
    for (; cursor < value.size(); ++cursor) {
        const char character = value[cursor];
        if (character == '.') {
            if (found_separator) {
                return std::unexpected(DecimalError::invalid_format);
            }
            found_separator = true;
            continue;
        }
        if (character < '0' || character > '9') {
            return std::unexpected(DecimalError::invalid_format);
        }
        found_digit = true;
        mantissa *= 10;
        mantissa += character - '0';
        if (found_separator) {
            if (scale == max_scale) {
                return std::unexpected(DecimalError::scale_out_of_range);
            }
            ++scale;
        }
    }
    if (!found_digit) {
        return std::unexpected(DecimalError::invalid_format);
    }
    if (negative) {
        mantissa = -mantissa;
    }
    return Decimal{std::move(mantissa), scale};
}

const cpp_int& Decimal::mantissa() const noexcept {
    return mantissa_;
}

std::int32_t Decimal::scale() const noexcept {
    return scale_;
}

bool Decimal::is_zero() const noexcept {
    return mantissa_ == 0;
}

std::string Decimal::to_string() const {
    const bool negative = mantissa_ < 0;
    std::string digits = absolute(mantissa_).convert_to<std::string>();
    if (scale_ == 0) {
        return negative ? "-" + digits : digits;
    }
    if (digits.size() <= static_cast<std::size_t>(scale_)) {
        digits.insert(0, static_cast<std::size_t>(scale_) + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - static_cast<std::size_t>(scale_), 1, '.');
    return negative ? "-" + digits : digits;
}

std::expected<std::int64_t, DecimalError> Decimal::to_int64_rounded() const {
    auto rounded = rescale(0);
    if (!rounded) {
        return std::unexpected(rounded.error());
    }
    const cpp_int minimum = std::numeric_limits<std::int64_t>::min();
    const cpp_int maximum = std::numeric_limits<std::int64_t>::max();
    if (rounded->mantissa_ < minimum || rounded->mantissa_ > maximum) {
        return std::unexpected(DecimalError::out_of_range);
    }
    return rounded->mantissa_.convert_to<std::int64_t>();
}

std::expected<Decimal, DecimalError> Decimal::add(const Decimal& other) const {
    const std::int32_t target_scale = std::max(scale_, other.scale_);
    const cpp_int left = mantissa_ * power_of_ten(target_scale - scale_);
    const cpp_int right = other.mantissa_ * power_of_ten(target_scale - other.scale_);
    return Decimal{left + right, target_scale};
}

std::expected<Decimal, DecimalError> Decimal::subtract(const Decimal& other) const {
    const std::int32_t target_scale = std::max(scale_, other.scale_);
    const cpp_int left = mantissa_ * power_of_ten(target_scale - scale_);
    const cpp_int right = other.mantissa_ * power_of_ten(target_scale - other.scale_);
    return Decimal{left - right, target_scale};
}

std::expected<Decimal, DecimalError> Decimal::multiply(const Decimal& other) const {
    if (scale_ > max_scale - other.scale_) {
        return std::unexpected(DecimalError::scale_out_of_range);
    }
    return Decimal{mantissa_ * other.mantissa_, scale_ + other.scale_};
}

std::expected<Decimal, DecimalError> Decimal::divide(
    const Decimal& other,
    const std::int32_t result_scale) const {
    if (other.is_zero()) {
        return std::unexpected(DecimalError::division_by_zero);
    }
    if (result_scale < 0 || result_scale > max_scale) {
        return std::unexpected(DecimalError::scale_out_of_range);
    }

    cpp_int numerator = mantissa_;
    cpp_int denominator = other.mantissa_;
    const std::int32_t exponent = result_scale + other.scale_ - scale_;
    if (exponent >= 0) {
        numerator *= power_of_ten(exponent);
    } else {
        denominator *= power_of_ten(-exponent);
    }
    return Decimal{divide_round_half_away_from_zero(numerator, denominator), result_scale};
}

std::expected<Decimal, DecimalError> Decimal::rescale(const std::int32_t target_scale) const {
    if (target_scale < 0 || target_scale > max_scale) {
        return std::unexpected(DecimalError::scale_out_of_range);
    }
    if (target_scale == scale_) {
        return *this;
    }
    if (target_scale > scale_) {
        return Decimal{mantissa_ * power_of_ten(target_scale - scale_), target_scale};
    }
    return Decimal{
        divide_round_half_away_from_zero(mantissa_, power_of_ten(scale_ - target_scale)),
        target_scale};
}

std::strong_ordering Decimal::operator<=>(const Decimal& other) const {
    const std::int32_t target_scale = std::max(scale_, other.scale_);
    const cpp_int left = mantissa_ * power_of_ten(target_scale - scale_);
    const cpp_int right = other.mantissa_ * power_of_ten(target_scale - other.scale_);
    if (left < right) {
        return std::strong_ordering::less;
    }
    if (left > right) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

bool Decimal::operator==(const Decimal& other) const {
    return (*this <=> other) == std::strong_ordering::equal;
}

void Decimal::normalize() {
    if (mantissa_ == 0) {
        scale_ = 0;
        return;
    }
    while (scale_ > 0 && mantissa_ % 10 == 0) {
        mantissa_ /= 10;
        --scale_;
    }
}

}  // namespace wealthtracker::core
