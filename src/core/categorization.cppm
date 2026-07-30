module;

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module wt.core.categorization;
export import wt.core.models;

export namespace wealthtracker::core {

struct CategorizationRule {
    std::string id;
    CategoryId category_id;
    std::int32_t priority{0};
    bool enabled{true};
    std::optional<AccountId> account_id;
    std::optional<std::string> payee_contains;
    std::optional<std::string> description_contains;
    std::optional<std::int64_t> minimum_minor_units;
    std::optional<std::int64_t> maximum_minor_units;
    bool operator==(const CategorizationRule&) const = default;
};

enum class CategoryDecisionSource {
    existing,
    user_rule,
    remembered_payee,
    bank_mapping,
    transfer_classifier,
    investment_classifier,
    uncategorized,
};

struct CategoryDecision {
    std::optional<CategoryId> category_id;
    CategoryDecisionSource source{CategoryDecisionSource::uncategorized};
    std::optional<std::string> rule_id;
    bool operator==(const CategoryDecision&) const = default;
};

struct CategorizerConfig {
    std::vector<CategorizationRule> rules;
    std::unordered_map<std::string, CategoryId> remembered_payees;
    std::unordered_map<std::string, CategoryId> bank_category_mapping;
    CategoryId transfer_category{"transfer"};
    CategoryId investment_category{"investment"};
};

class Categorizer final {
public:
    explicit Categorizer(CategorizerConfig config);
    [[nodiscard]] CategoryDecision categorize(const Transaction& transaction) const;

private:
    CategorizerConfig config_;
};

struct TransferDetectionOptions {
    std::chrono::days maximum_date_distance{3};
};

struct TransferCandidate {
    TransactionId outgoing_transaction_id;
    TransactionId incoming_transaction_id;
    double confidence{0.0};
    std::vector<std::string> reasons;
    bool operator==(const TransferCandidate&) const = default;
};

[[nodiscard]] std::vector<TransferCandidate> find_transfer_candidates(
    std::span<const Transaction> transactions,
    TransferDetectionOptions options = {});

}  // namespace wealthtracker::core

namespace wealthtracker::core {
namespace {

std::string normalized_text(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    bool previous_was_space = true;
    for (const unsigned char character : value) {
        if (std::isspace(character) != 0) {
            if (!previous_was_space) {
                result.push_back(' ');
            }
            previous_was_space = true;
        } else {
            result.push_back(character < 128 ? static_cast<char>(std::tolower(character))
                                             : static_cast<char>(character));
            previous_was_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

bool contains_normalized(const std::string& value, const std::string& needle) {
    return normalized_text(value).contains(normalized_text(needle));
}

bool rule_matches(const CategorizationRule& rule, const Transaction& transaction) {
    if (!rule.enabled ||
        (rule.account_id && *rule.account_id != transaction.account_id) ||
        (rule.payee_contains &&
         !contains_normalized(transaction.payee, *rule.payee_contains)) ||
        (rule.description_contains &&
         !contains_normalized(transaction.description, *rule.description_contains)) ||
        (rule.minimum_minor_units &&
         transaction.amount.minor_units() < *rule.minimum_minor_units) ||
        (rule.maximum_minor_units &&
         transaction.amount.minor_units() > *rule.maximum_minor_units)) {
        return false;
    }
    return true;
}

bool is_transfer_kind(const TransactionKind kind) {
    return kind == TransactionKind::transfer ||
           kind == TransactionKind::credit_card_repayment;
}

bool is_investment_kind(const TransactionKind kind) {
    return kind == TransactionKind::investment_purchase ||
           kind == TransactionKind::investment_sale;
}

std::uint64_t magnitude(const std::int64_t value) {
    return value >= 0 ? static_cast<std::uint64_t>(value)
                      : static_cast<std::uint64_t>(-(value + 1)) + 1;
}

bool has_transfer_word(const Transaction& transaction) {
    const std::string text = normalized_text(transaction.payee + " " + transaction.description);
    constexpr std::array<std::string_view, 5> markers{
        "transfer", "between accounts", "перевод", "между счет", "пополн"};
    return std::ranges::any_of(markers, [&](const std::string_view marker) {
        return text.contains(marker);
    });
}

}  // namespace

Categorizer::Categorizer(CategorizerConfig config) : config_(std::move(config)) {
    std::ranges::stable_sort(config_.rules, [](const auto& left, const auto& right) {
        return left.priority != right.priority ? left.priority > right.priority
                                               : left.id < right.id;
    });
    decltype(config_.remembered_payees) normalized_payees;
    for (auto& [payee, category] : config_.remembered_payees) {
        normalized_payees.insert_or_assign(normalized_text(payee), std::move(category));
    }
    config_.remembered_payees = std::move(normalized_payees);

    decltype(config_.bank_category_mapping) normalized_bank_categories;
    for (auto& [bank_category, category] : config_.bank_category_mapping) {
        normalized_bank_categories.insert_or_assign(
            normalized_text(bank_category),
            std::move(category));
    }
    config_.bank_category_mapping = std::move(normalized_bank_categories);
}

CategoryDecision Categorizer::categorize(const Transaction& transaction) const {
    if (transaction.category_id) {
        return {*transaction.category_id, CategoryDecisionSource::existing, std::nullopt};
    }
    const auto matching_rule = std::ranges::find_if(config_.rules, [&](const auto& rule) {
        return rule_matches(rule, transaction);
    });
    if (matching_rule != config_.rules.end()) {
        return {
            matching_rule->category_id,
            CategoryDecisionSource::user_rule,
            matching_rule->id};
    }
    const auto remembered = config_.remembered_payees.find(normalized_text(transaction.payee));
    if (remembered != config_.remembered_payees.end()) {
        return {remembered->second, CategoryDecisionSource::remembered_payee, std::nullopt};
    }
    if (transaction.bank_category) {
        const auto mapped =
            config_.bank_category_mapping.find(normalized_text(*transaction.bank_category));
        if (mapped != config_.bank_category_mapping.end()) {
            return {mapped->second, CategoryDecisionSource::bank_mapping, std::nullopt};
        }
    }
    if (is_transfer_kind(transaction.kind)) {
        return {
            config_.transfer_category,
            CategoryDecisionSource::transfer_classifier,
            std::nullopt};
    }
    if (is_investment_kind(transaction.kind)) {
        return {
            config_.investment_category,
            CategoryDecisionSource::investment_classifier,
            std::nullopt};
    }
    return {std::nullopt, CategoryDecisionSource::uncategorized, std::nullopt};
}

std::vector<TransferCandidate> find_transfer_candidates(
    const std::span<const Transaction> transactions,
    const TransferDetectionOptions options) {
    std::vector<TransferCandidate> result;
    for (const Transaction& outgoing : transactions) {
        if (!outgoing.amount.is_negative() || outgoing.transfer_pair_id ||
            outgoing.kind != TransactionKind::regular) {
            continue;
        }
        for (const Transaction& incoming : transactions) {
            if (!incoming.amount.is_positive() || incoming.transfer_pair_id ||
                incoming.kind != TransactionKind::regular ||
                outgoing.account_id == incoming.account_id ||
                outgoing.amount.currency() != incoming.amount.currency() ||
                magnitude(outgoing.amount.minor_units()) !=
                    magnitude(incoming.amount.minor_units())) {
                continue;
            }
            auto date_distance = incoming.posting_date - outgoing.posting_date;
            if (date_distance < std::chrono::days::zero()) {
                date_distance = -date_distance;
            }
            if (date_distance > options.maximum_date_distance) {
                continue;
            }

            TransferCandidate candidate{
                .outgoing_transaction_id = outgoing.id,
                .incoming_transaction_id = incoming.id,
                .confidence = 0.8,
                .reasons = {"opposite amounts", "different owned accounts"}};
            if (date_distance == std::chrono::days::zero()) {
                candidate.confidence += 0.1;
                candidate.reasons.emplace_back("same posting date");
            }
            if (has_transfer_word(outgoing) || has_transfer_word(incoming)) {
                candidate.confidence += 0.1;
                candidate.reasons.emplace_back("transfer wording");
            }
            result.push_back(std::move(candidate));
        }
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.confidence != right.confidence) {
            return left.confidence > right.confidence;
        }
        return left.outgoing_transaction_id != right.outgoing_transaction_id
                   ? left.outgoing_transaction_id < right.outgoing_transaction_id
                   : left.incoming_transaction_id < right.incoming_transaction_id;
    });
    return result;
}

}  // namespace wealthtracker::core
