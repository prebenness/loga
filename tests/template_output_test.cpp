#include <loga/colors.h>
#include <loga/template_output.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using prova::loga::labelled_pattern_sequence;
using prova::loga::pattern_sequence;
using prova::loga::template_output_error;
using prova::loga::zone;

int failures = 0;

void expect(bool condition, std::string_view description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void expect_error(
    const std::function<void()>& operation,
    std::string_view expected_text,
    std::string_view description) {
    try {
        operation();
        std::cerr << "FAIL: " << description << " (no exception)\n";
        ++failures;
    } catch (const template_output_error& error) {
        if (std::string_view(error.what()).find(expected_text) == std::string_view::npos) {
            std::cerr << "FAIL: " << description << " (" << error.what() << ")\n";
            ++failures;
        }
    }
}

std::string marker(std::size_t number) {
    return std::string(
               prova::loga::colors::palette[number % prova::loga::colors::palette.size()]) +
           "$" + std::to_string(number) +
           std::string(prova::loga::colors::reset);
}

std::string template_line(std::size_t label, std::string_view body) {
    return "  C" + std::to_string(label) + " " +
           std::string(prova::loga::colors::bright_yellow) + "\xE2\x97\x8F" +
           std::string(prova::loga::colors::reset) + " " + std::string(body) + "\n";
}

std::string summary(std::size_t count) {
    return "----------------------------\nSummary: " + std::to_string(count) +
           " clusters\n----------------------------\n";
}

void expect_segment(
    const pattern_sequence& pattern,
    std::size_t index,
    zone tag,
    std::string_view constant,
    std::string_view description) {
    expect(index < pattern.size(), description);
    if (index >= pattern.size()) {
        return;
    }

    const auto& segment = pattern.at(index);
    expect(segment.tag() == tag, description);
    if (tag == zone::constant) {
        expect(segment.tokens().raw() == constant, description);
    } else {
        expect(segment.tokens().raw().empty(), description);
    }
}

void test_last_summary_and_labels() {
    const std::string output =
        summary(1) + template_line(99, "$OLD") + "intermediate output\n" +
        summary(2) +
        template_line(7, std::string("$GPRMC,") + marker(0)) +
        template_line(42, "$RADTM,W84,,,,,,,*15");

    const auto templates = prova::loga::parse_template_output(output);
    expect(templates.size() == 2, "last summary determines template count");
    if (templates.size() != 2) {
        return;
    }

    expect(templates[0].label == 7, "first label is preserved");
    expect(templates[1].label == 42, "second label is preserved");
    expect_segment(
        templates[0].pattern, 0, zone::constant, "$GPRMC,",
        "constant before placeholder is preserved");
    expect_segment(
        templates[0].pattern, 1, zone::placeholder, {},
        "coloured placeholder is parsed");
    expect_segment(
        templates[1].pattern, 0, zone::constant, "$RADTM,W84,,,,,,,*15",
        "constant-only template is parsed");
}

void test_literal_dollar_and_coloured_markers() {
    const std::string body = "$" + marker(0) + "*" + marker(1);
    const auto templates = prova::loga::parse_template_output(
        summary(1) + template_line(3, body));

    expect(templates.size() == 1, "representative template is parsed");
    if (templates.empty()) {
        return;
    }

    const auto& pattern = templates[0].pattern;
    expect(pattern.size() == 4, "literal-dollar template has four segments");
    expect_segment(pattern, 0, zone::constant, "$", "literal dollar remains constant");
    expect_segment(pattern, 1, zone::placeholder, {}, "placeholder zero is parsed");
    expect_segment(pattern, 2, zone::constant, "*", "literal star remains constant");
    expect_segment(pattern, 3, zone::placeholder, {}, "placeholder one is parsed");
}

void test_ansi_distinguishes_literal_marker_text() {
    const std::string body = "literal-$0," + marker(0) + ",literal-$1";
    const auto templates = prova::loga::parse_template_output(
        summary(1) + template_line(0, body));

    const auto& pattern = templates.at(0).pattern;
    expect(pattern.size() == 3, "only coloured marker is a placeholder");
    expect_segment(
        pattern, 0, zone::constant, "literal-$0,",
        "uncoloured $0 remains literal text");
    expect_segment(pattern, 1, zone::placeholder, {}, "coloured $0 is a placeholder");
    expect_segment(
        pattern, 2, zone::constant, ",literal-$1",
        "uncoloured $1 remains literal text");
}

void test_plain_output_fallback() {
    const std::string output = summary(1) + " C8 \xE2\x97\x8F $$0*$1\r\n";
    std::istringstream stream(output);
    const auto templates = prova::loga::parse_template_output(stream);

    const auto& pattern = templates.at(0).pattern;
    expect(pattern.size() == 4, "plain output uses sequential marker convention");
    expect_segment(pattern, 0, zone::constant, "$", "plain literal dollar is retained");
    expect_segment(pattern, 1, zone::placeholder, {}, "plain $0 is parsed");
    expect_segment(pattern, 2, zone::constant, "*", "plain star is retained");
    expect_segment(pattern, 3, zone::placeholder, {}, "plain $1 is parsed");
}

void test_validation() {
    expect_error(
        [] { prova::loga::parse_template_output("no summary\n"); },
        "no 'Summary: N clusters'",
        "missing summary is rejected");

    expect_error(
        [] {
            prova::loga::parse_template_output(
                summary(2) + template_line(0, "$A"));
        },
        "declares 2 templates; parsed 1",
        "too few template lines are rejected");

    expect_error(
        [] {
            prova::loga::parse_template_output(
                summary(1) + template_line(0, "$A") + template_line(1, "$B"));
        },
        "additional template line",
        "too many template lines are rejected");

    expect_error(
        [] {
            prova::loga::parse_template_output(
                summary(2) + template_line(4, "$A") + template_line(4, "$B"));
        },
        "duplicate template label C4",
        "duplicate labels are rejected");

    expect_error(
        [] {
            const std::string wrong = std::string(prova::loga::colors::palette[0]) +
                                      "$1" +
                                      std::string(prova::loga::colors::reset);
            prova::loga::parse_template_output(summary(1) + template_line(0, wrong));
        },
        "expected $0, found $1",
        "nonsequential coloured placeholders are rejected");

    expect_error(
        [] {
            const std::string no_reset =
                std::string(prova::loga::colors::palette[0]) + "$0";
            prova::loga::parse_template_output(summary(1) + template_line(0, no_reset));
        },
        "no reset sequence",
        "malformed coloured placeholders are rejected");

    expect_error(
        [] {
            prova::loga::parse_template_output(
                summary(1) + " C0 \xE2\x97\x8F $0-$2\n");
        },
        "expected $1, found $2",
        "nonsequential plain placeholders are rejected");

    expect_error(
        [] {
            prova::loga::parse_template_output(
                summary(1) + template_line(0, "$A") +
                "Summary: 1 components\n"
                "T0 $A\n"
                "Phase 1 completed; run Loga again for phase 2\n");
        },
        "file may contain first-pass output",
        "first-pass output is rejected");
}

} // namespace

int main() {
    test_last_summary_and_labels();
    test_literal_dollar_and_coloured_markers();
    test_ansi_distinguishes_literal_marker_text();
    test_plain_output_fallback();
    test_validation();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
