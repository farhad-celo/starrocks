#include "like.h"

#include "exprs/celonis/match_pattern_util.h"

namespace starrocks::celonis::like {

namespace {

[[nodiscard]] bool starts_with(const std::string_view input, const std::string_view prefix) noexcept {
    return input.starts_with(prefix);
}

[[nodiscard]] bool ends_with(const std::string_view input, const std::string_view suffix) noexcept {
    return input.ends_with(suffix);
}

[[nodiscard]] bool is_valid_escape(const char escaped) {
    return (escaped == MATCH_ANY || escaped == MATCH_ONE || escaped == ESCAPE_CHAR);
}

struct wildcard_count_result_t {
    bool escaped_wildcard_found{};
    size_t count{};
};

/**
 * Determines pattern type based on pattern preprocessing information.
 */
pattern_type determine_pattern_type(const std::string_view trimmed_pattern,
                                    const wildcard_count_result_t& wildcard_count_result) noexcept {
    if (wildcard_count_result.count == 0) {
        return wildcard_count_result.escaped_wildcard_found ? pattern_type::NO_WILDCARD_BUT_WITH_ESCAPED_WILDCARDS
                                                            : pattern_type::NO_WILDCARD_AND_NO_ESCAPED_WILDCARDS;
    }

    // We do not check optimizations for patterns containing escaped wildcards
    if (wildcard_count_result.escaped_wildcard_found) {
        return pattern_type::WILDCARD;
    }

    // Check if optimizations are possible
    if (wildcard_count_result.count == 1) {
        if (trimmed_pattern.front() == MATCH_ANY) {
            return pattern_type::WILDCARD_ENDS_WITH;
        }
        if (trimmed_pattern.back() == MATCH_ANY) {
            return pattern_type::WILDCARD_STARTS_WITH;
        }
        // fallthrough
    } else if (wildcard_count_result.count == 2 && trimmed_pattern.front() == MATCH_ANY &&
               trimmed_pattern.back() == MATCH_ANY) {
        return pattern_type::WILDCARD_CONTAINS;
    }

    // Otherwise, fall back to the general wildcard type
    return pattern_type::WILDCARD;
}

/**
 * Checks the reduced pattern for occurrences of wildcards and counts them. Escaped wildcards are ignored and a flag is
 * returned indicating whether any ignored wildcards were encountered. Additionally, sets a warning for when misused
 * escape characters are found.
 */
[[nodiscard]] wildcard_count_result_t count_wildcards(const std::string_view original_pattern,
                                                      const std::string_view reduced_pattern) {
    size_t wildcard_count{0};
    bool escaped_wildcard_found{false};

    auto itr = reduced_pattern.cbegin();
    while (itr != reduced_pattern.cend()) {
        const char curr = *itr;

        switch (curr) {
        case MATCH_ANY:
        case MATCH_ONE:
            ++wildcard_count;
            break;
        case ESCAPE_CHAR: {
            // Skip escape char
            ++itr;

            if (itr == reduced_pattern.cend()) {
                continue;
            }

            const char escaped = *itr;
            if (is_valid_escape(escaped)) {
                escaped_wildcard_found = true;
            }
            break;
        }
        default:
            break;
        }

        ++itr;
    }

    return {escaped_wildcard_found, wildcard_count};
}

/* Removes first '%' (intended for WILDCARD_ENDS_WITH) */
[[nodiscard]] std::string_view remove_prefix(const std::string_view input) {
    return input.substr(1);
}
/* Removes last '%' (intended for WILDCARD_STARTS_WITH) */
[[nodiscard]] std::string_view remove_suffix(const std::string_view input) {
    return input.substr(0, input.length() - 1);
}
/* Removes first/last '%' (intended for WILDCARD_CONTAINS) */
[[nodiscard]] std::string_view remove_prefix_and_suffix(const std::string_view input) {
    return input.substr(1, input.length() - 2);
}

} // namespace

preprocessed_pattern preprocess_pattern(const pattern_t original_pattern) {
    auto trimmed_pattern{original_pattern.get()};

    // If the original_pattern is empty we match on everything
    if (trimmed_pattern.empty()) {
        return {trimmed_pattern, pattern_type::MATCH_EVERYTHING};
    }

    // Trim multiple leading '%', e.g. '%%%original_pattern' becomes '%original_pattern'
    while (*trimmed_pattern.begin() == MATCH_ANY && *std::next(trimmed_pattern.begin()) == MATCH_ANY) {
        trimmed_pattern.remove_prefix(1);
    }

    if (trimmed_pattern == "%") {
        return {trimmed_pattern, pattern_type::MATCH_EVERYTHING};
    }

    // Trim multiple trailing '%', e.g. 'original_pattern%%%' becomes 'original_pattern%'
    // Note: It is not checked whether an escape character is escaped itself. Therefore, the original_pattern 'abc\\%%%'
    // results in 'abc\\%%'
    if (trimmed_pattern.length() >= 2) {
        while (*std::prev(trimmed_pattern.cend()) == MATCH_ANY && *std::prev(trimmed_pattern.cend(), 2) == MATCH_ANY &&
               *std::prev(trimmed_pattern.cend(), 3) != ESCAPE_CHAR) {
            trimmed_pattern.remove_suffix(1);
        }
    }

    const auto wildcard_count_result{count_wildcards(original_pattern.get(), trimmed_pattern)};

    const pattern_type type = determine_pattern_type(trimmed_pattern, wildcard_count_result);

    return {trimmed_pattern, type};
}

std::string sanitize_pattern(const std::string_view pattern) {
    std::string sanitized;
    sanitized.reserve(pattern.length());

    for (const auto* itr = pattern.begin(); itr != pattern.end(); ++itr) {
        const char curr = *itr;
        switch (curr) {
        case MATCH_ANY:
        case MATCH_ONE:
            break;
        case ESCAPE_CHAR: {
            if (itr + 1 != pattern.end()) {
                // Skip escape char
                ++itr;
                const char escaped = *itr;
                if (!is_valid_escape(escaped)) {
                    sanitized.push_back(ESCAPE_CHAR);
                }
                sanitized.push_back(escaped);
            }
            break;
        }
        default:
            sanitized.push_back(curr);
        }
    }

    return sanitized;
}

void lower_string_utf8_inplace(std::string& pattern) {
    for (auto i = 0; i < pattern.size(); ++i) {
        if (pattern[i] >= 'A' && pattern[i] <= 'Z') {
            pattern[i] += 32;
            continue;
        }

        if (pattern[i] == '\xC3' && !(i + 1 < pattern.size())) {
            throw std::runtime_error{"like: Pattern is not correctly UTF-8 encoded."};
        }

        // Character: Ä | UTF-8 Bytes: ['0xC3', '0x84']
        // Character: Ö | UTF-8 Bytes: ['0xC3', '0x96']
        // Character: Ü | UTF-8 Bytes: ['0xC3', '0x9C']
        if (pattern[i] == '\xC3' && (i + 1 < pattern.size()) &&
            ((pattern[i + 1] == '\x84') || (pattern[i + 1] == '\x96') || (pattern[i + 1] == '\x9C'))) {
            pattern[i + 1] += 32;
        }
    }
}

std::string lower_string_utf8(std::string_view pattern) {
    std::string copy(pattern);
    lower_string_utf8_inplace(copy);
    return copy;
}

like_reusable::like_reusable(const pattern_t pattern) {
    const auto [trimmed_pattern, type] = preprocess_pattern(pattern);
    pattern_type_ = type;

    switch (pattern_type_) {
    case pattern_type::NO_WILDCARD_AND_NO_ESCAPED_WILDCARDS:
        pattern_ = lower_string_utf8(trimmed_pattern);
        is_case_sensitive_ = false;
        break;
    case pattern_type::NO_WILDCARD_BUT_WITH_ESCAPED_WILDCARDS:
        pattern_ = sanitize_pattern(trimmed_pattern);
        lower_string_utf8_inplace(pattern_);
        is_case_sensitive_ = false;
        break;
    case pattern_type::MATCH_EVERYTHING:
        // pattern is not accessed
        break;
    case pattern_type::WILDCARD_STARTS_WITH:
        pattern_ = std::string{remove_suffix(trimmed_pattern)};
        break;
    case pattern_type::WILDCARD_ENDS_WITH:
        pattern_ = std::string{remove_prefix(trimmed_pattern)};
        break;
    case pattern_type::WILDCARD_CONTAINS:
        pattern_ = std::string{remove_prefix_and_suffix(trimmed_pattern)};
        break;
    case pattern_type::WILDCARD:
        pattern_ = std::string{trimmed_pattern};
        break;
    }
}

bool like_reusable::operator()(std::string_view input) const {
    switch (pattern_type_) {
    case pattern_type::NO_WILDCARD_AND_NO_ESCAPED_WILDCARDS:
    case pattern_type::NO_WILDCARD_BUT_WITH_ESCAPED_WILDCARDS:
    case pattern_type::WILDCARD_CONTAINS:
        return input.find(pattern_) != std::string::npos;
    case pattern_type::MATCH_EVERYTHING:
        return true;
    case pattern_type::WILDCARD_STARTS_WITH:
        return starts_with(input, pattern_);
    case pattern_type::WILDCARD_ENDS_WITH:
        return ends_with(input, pattern_);
    case pattern_type::WILDCARD:
        return match_pattern(input, pattern_t{pattern_});
    }

    throw std::runtime_error{"like_reusable: Non-exhaustive switch-case"};
}

bool like_reusable::is_case_sensitive() const {
    return is_case_sensitive_;
}

bool input_row_pattern_matcher::match(const like_reusable& pattern) {
    if (!pattern.is_case_sensitive()) {
        if (!lower_cased_input_.has_value()) {
            lower_cased_input_ = lower_string_utf8(input_);
        }
        return pattern(std::string_view(lower_cased_input_.value()));
    } else {
        return pattern(input_);
    }
}

} // namespace starrocks::celonis::like
