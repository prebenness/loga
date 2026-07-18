#include <loga/template_matcher.h>

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using prova::loga::pattern_sequence;
using prova::loga::template_match_result;
using prova::loga::tokenized;
using prova::loga::zone;

pattern_sequence make_pattern(
    std::initializer_list<std::pair<zone, const char*>> segments) {
    pattern_sequence pattern;
    for (const auto& [tag, text] : segments) {
        if (tag == zone::constant) {
            pattern.add(pattern_sequence::segment(tag, text));
        } else {
            pattern.add(pattern_sequence::segment(tag));
        }
    }
    return pattern;
}

bool expect(
    bool condition,
    const std::string& description) {
    if (condition) {
        return true;
    }
    std::cerr << "failed: " << description << '\n';
    return false;
}

template_match_result match(
    const pattern_sequence& pattern,
    const std::string& line) {
    return prova::loga::match_template(pattern, tokenized(line));
}

struct reference_match_result {
    bool accepted;
    bool captures_ambiguous;
};

unsigned int reference_match_count(
    const pattern_sequence& pattern,
    const tokenized& line,
    std::size_t segment_index,
    std::size_t token_index) {
    if (segment_index == pattern.size()) {
        return token_index == line.count() ? 1U : 0U;
    }

    const auto& segment = pattern.at(segment_index);
    if (segment.tag() == zone::constant) {
        if (token_index > line.count() ||
            segment.tokens().count() > line.count() - token_index) {
            return 0U;
        }
        for (std::size_t i = 0; i < segment.tokens().count(); ++i) {
            if (segment.tokens().at(i).view() !=
                line.at(token_index + i).view()) {
                return 0U;
            }
        }
        return reference_match_count(
            pattern,
            line,
            segment_index + 1,
            token_index + segment.tokens().count());
    }

    unsigned int count = 0;
    for (std::size_t endpoint = token_index;
         endpoint <= line.count();
         ++endpoint) {
        count += reference_match_count(
            pattern, line, segment_index + 1, endpoint);
        if (count >= 2U) {
            return 2U;
        }
    }
    return count;
}

reference_match_result reference_match(
    const pattern_sequence& pattern,
    const tokenized& line) {
    const unsigned int count = reference_match_count(pattern, line, 0, 0);
    return {count != 0U, count >= 2U};
}

struct alphabet_token {
    const char* text;
    prova::loga::token::category category;
};

const std::vector<alphabet_token> exhaustive_alphabet{
    {"A", prova::loga::token::category::alpha},
    {"B", prova::loga::token::category::alpha},
    {"1", prova::loga::token::category::digits},
    {"$", prova::loga::token::category::place},
};

using pattern_spec = std::vector<std::pair<zone, std::string>>;

pattern_sequence make_pattern(const pattern_spec& segments) {
    pattern_sequence pattern;
    for (const auto& [tag, text] : segments) {
        if (tag == zone::constant) {
            pattern.add(pattern_sequence::segment(tag, text));
        } else {
            pattern.add(pattern_sequence::segment(tag));
        }
    }
    return pattern;
}

void generate_patterns(
    std::size_t remaining_segments,
    zone next_tag,
    pattern_spec& current,
    std::vector<pattern_sequence>& patterns) {
    if (remaining_segments == 0) {
        patterns.push_back(make_pattern(current));
        return;
    }

    const zone following_tag = next_tag == zone::constant
        ? zone::placeholder
        : zone::constant;
    if (next_tag == zone::constant) {
        for (const auto& token : exhaustive_alphabet) {
            current.emplace_back(zone::constant, token.text);
            generate_patterns(
                remaining_segments - 1,
                following_tag,
                current,
                patterns);
            current.pop_back();
        }
        return;
    }

    current.emplace_back(zone::placeholder, "");
    generate_patterns(
        remaining_segments - 1,
        following_tag,
        current,
        patterns);
    current.pop_back();
}

void generate_lines(
    std::size_t remaining_tokens,
    prova::loga::token::category previous_category,
    std::string& current,
    std::vector<std::string>& lines) {
    if (remaining_tokens == 0) {
        lines.push_back(current);
        return;
    }

    for (const auto& token : exhaustive_alphabet) {
        if (token.category == previous_category) {
            continue;
        }
        const std::size_t previous_length = current.size();
        current += token.text;
        generate_lines(
            remaining_tokens - 1,
            token.category,
            current,
            lines);
        current.resize(previous_length);
    }
}

bool test_exact_constants_and_anchoring() {
    const auto pattern = make_pattern({{zone::constant, "$GGA,"}});
    bool passed = true;
    passed &= expect(match(pattern, "$GGA,").accepted,
                     "an exact constant matches");
    passed &= expect(!match(pattern, "$RMC,").accepted,
                     "constant text is compared, not only token categories");
    passed &= expect(!match(pattern, "0$GGA,").accepted,
                     "a constant match is anchored at the start");
    passed &= expect(!match(pattern, "$GGA,0").accepted,
                     "a constant match is anchored at the end");
    return passed;
}

bool test_token_boundaries() {
    const auto pattern = make_pattern({
        {zone::constant, "A"},
        {zone::placeholder, ""},
    });
    return expect(!match(pattern, "AB").accepted,
                  "a constant cannot match part of an input token");
}

bool test_unique_capture() {
    const auto pattern = make_pattern({
        {zone::constant, "$A"},
        {zone::placeholder, ""},
        {zone::constant, ",B"},
    });
    const auto result = match(pattern, "$A12,B");

    bool passed = true;
    passed &= expect(result.accepted, "an internal placeholder consumes tokens");
    passed &= expect(!result.captures_ambiguous,
                     "the internal capture is unique");
    passed &= expect(result.captures.size() == 1,
                     "one placeholder produces one capture");
    if (result.captures.size() == 1) {
        const auto& capture = result.captures.front();
        passed &= expect(capture.placeholder_index == 0,
                         "capture records its placeholder index");
        passed &= expect(capture.token_begin == 2 && capture.token_end == 3,
                         "capture records a half-open token span");
        passed &= expect(capture.value == "12",
                         "capture preserves the source text");
    }
    return passed;
}

bool test_empty_leading_and_trailing_captures() {
    bool passed = true;

    const auto internal = make_pattern({
        {zone::constant, "$A"},
        {zone::placeholder, ""},
        {zone::constant, ",B"},
    });
    const auto empty = match(internal, "$A,B");
    passed &= expect(empty.accepted && !empty.captures_ambiguous,
                     "a placeholder may consume zero tokens");
    passed &= expect(empty.captures.size() == 1 &&
                         empty.captures.front().value.empty() &&
                         empty.captures.front().token_begin ==
                             empty.captures.front().token_end,
                     "an empty capture has an empty token span");

    const auto leading = make_pattern({
        {zone::placeholder, ""},
        {zone::constant, "$A"},
    });
    const auto leading_result = match(leading, "12$A");
    passed &= expect(leading_result.accepted &&
                         leading_result.captures.size() == 1 &&
                         leading_result.captures.front().value == "12",
                     "a leading placeholder captures source text");

    const auto trailing = make_pattern({
        {zone::constant, "$A"},
        {zone::placeholder, ""},
    });
    const auto trailing_result = match(trailing, "$A,12");
    passed &= expect(trailing_result.accepted &&
                         trailing_result.captures.size() == 1 &&
                         trailing_result.captures.front().value == ",12",
                     "a trailing placeholder captures source text");
    return passed;
}

bool test_repeated_constant_requires_later_endpoint() {
    const auto pattern = make_pattern({
        {zone::constant, "$A"},
        {zone::placeholder, ""},
        {zone::constant, ",B"},
    });
    const auto result = match(pattern, "$A,B$,B");

    bool passed = true;
    passed &= expect(result.accepted,
                     "matching tries a later occurrence of a repeated constant");
    passed &= expect(!result.captures_ambiguous &&
                         result.captures.size() == 1 &&
                         result.captures.front().value == ",B$",
                     "the later occurrence gives the unique complete match");
    return passed;
}

bool test_ambiguous_capture() {
    const auto pattern = make_pattern({
        {zone::placeholder, ""},
        {zone::constant, ","},
        {zone::placeholder, ""},
    });
    const auto result = match(pattern, "$A,$B,");

    bool passed = true;
    passed &= expect(result.accepted, "an ambiguous template still accepts");
    passed &= expect(result.captures_ambiguous,
                     "multiple placeholder divisions are reported");
    passed &= expect(result.captures.empty(),
                     "ambiguous capture assignments are not enumerated");
    return passed;
}

bool test_placeholder_only_and_adjacent_placeholders() {
    bool passed = true;

    const auto only = make_pattern({{zone::placeholder, ""}});
    const auto any_line = match(only, "$A,12");
    passed &= expect(any_line.accepted && !any_line.captures_ambiguous &&
                         any_line.captures.size() == 1 &&
                         any_line.captures.front().value == "$A,12",
                     "a placeholder-only template uniquely captures a line");
    passed &= expect(match(only, "").accepted,
                     "a placeholder-only template accepts an empty line");

    const auto adjacent = make_pattern({
        {zone::placeholder, ""},
        {zone::placeholder, ""},
    });
    const auto ambiguous = match(adjacent, "$A");
    passed &= expect(ambiguous.accepted && ambiguous.captures_ambiguous,
                     "adjacent placeholders report alternative divisions");

    const auto uniquely_empty = match(adjacent, "");
    passed &= expect(uniquely_empty.accepted &&
                         !uniquely_empty.captures_ambiguous &&
                         uniquely_empty.captures.size() == 2,
                     "adjacent placeholders have one division on an empty line");
    return passed;
}

bool test_empty_pattern() {
    const pattern_sequence pattern;
    bool passed = true;
    passed &= expect(match(pattern, "").accepted,
                     "an empty pattern accepts an empty line");
    passed &= expect(!match(pattern, "$A").accepted,
                     "an empty pattern rejects a non-empty line");
    return passed;
}

bool test_exhaustive_reference_agreement() {
    std::vector<pattern_sequence> patterns;
    patterns.emplace_back();
    pattern_spec current_pattern;
    for (std::size_t length = 1; length <= 5; ++length) {
        generate_patterns(
            length, zone::constant, current_pattern, patterns);
        generate_patterns(
            length, zone::placeholder, current_pattern, patterns);
    }

    std::vector<std::string> lines;
    std::string current_line;
    for (std::size_t length = 0; length <= 4; ++length) {
        generate_lines(
            length,
            prova::loga::token::category::none,
            current_line,
            lines);
    }

    for (std::size_t pattern_index = 0;
         pattern_index < patterns.size();
         ++pattern_index) {
        for (const std::string& raw_line : lines) {
            const tokenized line(raw_line);
            const auto expected = reference_match(patterns.at(pattern_index), line);
            const auto actual =
                prova::loga::match_template(patterns.at(pattern_index), line);
            if (actual.accepted != expected.accepted ||
                actual.captures_ambiguous != expected.captures_ambiguous) {
                std::cerr << "failed: exhaustive reference disagreement for "
                          << "pattern " << pattern_index << " and line \""
                          << raw_line << "\"\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main() {
    bool passed = true;
    passed &= test_exact_constants_and_anchoring();
    passed &= test_token_boundaries();
    passed &= test_unique_capture();
    passed &= test_empty_leading_and_trailing_captures();
    passed &= test_repeated_constant_requires_later_endpoint();
    passed &= test_ambiguous_capture();
    passed &= test_placeholder_only_and_adjacent_placeholders();
    passed &= test_empty_pattern();
    passed &= test_exhaustive_reference_agreement();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
