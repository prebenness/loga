#include <loga/template_output.h>

#include <loga/colors.h>

#include <charconv>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace {

using prova::loga::labelled_pattern_sequence;
using prova::loga::pattern_sequence;
using prova::loga::template_output_error;
using prova::loga::zone;

constexpr std::string_view summary_prefix = "Summary:";
constexpr std::string_view summary_suffix = "clusters";
constexpr std::string_view bullet = "\xE2\x97\x8F";

bool is_horizontal_space(char ch) {
    return ch == ' ' || ch == '\t';
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && is_horizontal_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_horizontal_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> split_lines(std::string_view output) {
    std::vector<std::string_view> lines;
    std::size_t begin = 0;

    while (begin < output.size()) {
        const std::size_t end = output.find('\n', begin);
        std::string_view line = end == std::string_view::npos
                                    ? output.substr(begin)
                                    : output.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);

        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }

    return lines;
}

std::optional<std::size_t> csi_end(std::string_view text, std::size_t begin) {
    if (begin + 2 > text.size() || text[begin] != '\x1b' || text[begin + 1] != '[') {
        return std::nullopt;
    }

    for (std::size_t i = begin + 2; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 0x40 && ch <= 0x7e) {
            return i + 1;
        }
        if (ch < 0x20 || ch > 0x3f) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::string strip_ansi(std::string_view text) {
    std::string plain;
    plain.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        if (const auto end = csi_end(text, i)) {
            i = *end;
        } else {
            plain.push_back(text[i]);
            ++i;
        }
    }

    return plain;
}

std::size_t parse_number(std::string_view digits, std::string_view description) {
    if (digits.empty()) {
        throw template_output_error(std::string(description) + " is missing");
    }

    std::size_t value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
        throw template_output_error(std::string(description) + " is invalid");
    }
    return value;
}

std::optional<std::size_t> parse_summary(std::string_view line) {
    const std::string plain_storage = strip_ansi(line);
    std::string_view plain = trim(plain_storage);
    if (!plain.starts_with(summary_prefix)) {
        return std::nullopt;
    }

    plain.remove_prefix(summary_prefix.size());
    plain = trim(plain);

    std::size_t digit_count = 0;
    while (digit_count < plain.size() && plain[digit_count] >= '0' && plain[digit_count] <= '9') {
        ++digit_count;
    }
    if (digit_count == 0) {
        return std::nullopt;
    }

    const std::size_t count = parse_number(plain.substr(0, digit_count), "cluster count");
    plain.remove_prefix(digit_count);
    plain = trim(plain);
    if (plain != summary_suffix) {
        return std::nullopt;
    }

    return count;
}

bool is_separator(std::string_view line) {
    const std::string plain_storage = strip_ansi(line);
    const std::string_view plain = trim(plain_storage);
    if (plain.empty()) {
        return true;
    }
    for (const char ch : plain) {
        if (ch != '-') {
            return false;
        }
    }
    return true;
}

void add_constant(pattern_sequence& pattern, std::string& constant) {
    if (constant.empty()) {
        return;
    }

    pattern.add(pattern_sequence::segment(zone::constant, constant));
    constant.clear();
}

std::pair<std::size_t, std::size_t> numeric_marker(
    std::string_view body,
    std::size_t dollar,
    std::string_view description) {
    std::size_t end = dollar + 1;
    while (end < body.size() && body[end] >= '0' && body[end] <= '9') {
        ++end;
    }
    if (end == dollar + 1) {
        throw template_output_error(std::string(description) + " has no number");
    }

    const std::string_view digits = body.substr(dollar + 1, end - dollar - 1);
    if (digits.size() > 1 && digits.front() == '0') {
        throw template_output_error(std::string(description) + " has a non-canonical number");
    }
    return {parse_number(digits, description), end};
}

std::optional<std::size_t> palette_index_at(std::string_view text, std::size_t begin) {
    for (std::size_t i = 0; i < prova::loga::colors::palette.size(); ++i) {
        if (text.substr(begin).starts_with(prova::loga::colors::palette[i])) {
            return i;
        }
    }
    return std::nullopt;
}

pattern_sequence parse_coloured_pattern(std::string_view body) {
    pattern_sequence pattern;
    std::string constant;
    std::size_t expected_placeholder = 0;

    for (std::size_t position = 0; position < body.size();) {
        if (body[position] != '\x1b') {
            constant.push_back(body[position]);
            ++position;
            continue;
        }

        const auto palette_index = palette_index_at(body, position);
        if (!palette_index) {
            throw template_output_error("unexpected ANSI sequence in template text");
        }
        if (*palette_index != expected_placeholder % prova::loga::colors::palette.size()) {
            throw template_output_error("placeholder colour is inconsistent with its position");
        }
        position += prova::loga::colors::palette[*palette_index].size();

        if (position >= body.size() || body[position] != '$') {
            throw template_output_error("coloured placeholder marker is malformed");
        }
        const auto [placeholder, marker_end] = numeric_marker(
            body, position, "coloured placeholder marker");
        if (placeholder != expected_placeholder) {
            throw template_output_error(
                "placeholder markers are not sequential: expected $" +
                std::to_string(expected_placeholder) + ", found $" +
                std::to_string(placeholder));
        }
        position = marker_end;

        if (!body.substr(position).starts_with(prova::loga::colors::reset)) {
            throw template_output_error("coloured placeholder marker has no reset sequence");
        }
        position += prova::loga::colors::reset.size();

        add_constant(pattern, constant);
        pattern.add(pattern_sequence::segment(zone::placeholder));
        ++expected_placeholder;
    }

    add_constant(pattern, constant);
    if (pattern.size() == 0) {
        throw template_output_error("template text is empty");
    }
    return pattern;
}

pattern_sequence parse_plain_pattern(std::string_view body) {
    pattern_sequence pattern;
    std::string constant;
    std::size_t expected_placeholder = 0;

    for (std::size_t position = 0; position < body.size();) {
        if (body[position] != '$' || position + 1 == body.size() ||
            body[position + 1] < '0' || body[position + 1] > '9') {
            constant.push_back(body[position]);
            ++position;
            continue;
        }

        const auto [placeholder, marker_end] = numeric_marker(
            body, position, "placeholder marker");
        if (placeholder != expected_placeholder) {
            throw template_output_error(
                "placeholder markers are not sequential: expected $" +
                std::to_string(expected_placeholder) + ", found $" +
                std::to_string(placeholder));
        }

        add_constant(pattern, constant);
        pattern.add(pattern_sequence::segment(zone::placeholder));
        ++expected_placeholder;
        position = marker_end;
    }

    add_constant(pattern, constant);
    if (pattern.size() == 0) {
        throw template_output_error("template text is empty");
    }
    return pattern;
}

pattern_sequence parse_pattern(std::string_view body) {
    if (body.find('\x1b') != std::string_view::npos) {
        return parse_coloured_pattern(body);
    }
    return parse_plain_pattern(body);
}

struct parsed_template_line {
    std::size_t label;
    std::string_view body;
};

std::optional<parsed_template_line> parse_template_line(std::string_view line) {
    std::size_t position = 0;
    while (position < line.size() && is_horizontal_space(line[position])) {
        ++position;
    }
    if (position == line.size() || line[position] != 'C') {
        return std::nullopt;
    }
    ++position;

    const std::size_t label_begin = position;
    while (position < line.size() && line[position] >= '0' && line[position] <= '9') {
        ++position;
    }
    if (position == label_begin) {
        return std::nullopt;
    }
    const std::size_t label = parse_number(
        line.substr(label_begin, position - label_begin), "template label");

    if (position == line.size() || !is_horizontal_space(line[position])) {
        throw template_output_error("template label is not followed by template text");
    }
    while (position < line.size() && is_horizontal_space(line[position])) {
        ++position;
    }

    std::size_t after_bullet = position;
    while (const auto end = csi_end(line, after_bullet)) {
        after_bullet = *end;
    }
    if (line.substr(after_bullet).starts_with(bullet)) {
        after_bullet += bullet.size();
        while (const auto end = csi_end(line, after_bullet)) {
            after_bullet = *end;
        }
        if (after_bullet == line.size() || !is_horizontal_space(line[after_bullet])) {
            throw template_output_error("template bullet is not followed by template text");
        }
        ++after_bullet;
        position = after_bullet;
    }

    if (position == line.size()) {
        throw template_output_error("template text is empty");
    }
    return parsed_template_line{label, line.substr(position)};
}

template_output_error line_error(std::size_t line, const template_output_error& error) {
    return template_output_error(
        "line " + std::to_string(line + 1) + ": " + error.what());
}

} // namespace

std::vector<labelled_pattern_sequence> prova::loga::parse_template_output(
    std::string_view output) {
    const std::vector<std::string_view> lines = split_lines(output);

    std::optional<std::size_t> summary_line;
    std::size_t expected_count = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (const auto count = parse_summary(lines[i])) {
            summary_line = i;
            expected_count = *count;
        }
    }
    if (!summary_line) {
        throw template_output_error("output contains no 'Summary: N clusters' block");
    }

    std::size_t cursor = *summary_line + 1;
    while (cursor < lines.size() && is_separator(lines[cursor])) {
        ++cursor;
    }

    std::vector<labelled_pattern_sequence> templates;
    templates.reserve(expected_count);
    std::set<std::size_t> labels;

    for (std::size_t i = 0; i < expected_count; ++i) {
        if (cursor >= lines.size()) {
            throw template_output_error(
                "final Summary declares " + std::to_string(expected_count) +
                " templates; parsed " + std::to_string(templates.size()));
        }

        std::optional<parsed_template_line> parsed;
        try {
            parsed = parse_template_line(lines[cursor]);
        } catch (const template_output_error& error) {
            throw line_error(cursor, error);
        }
        if (!parsed) {
            throw template_output_error(
                "final Summary declares " + std::to_string(expected_count) +
                " templates; parsed " + std::to_string(templates.size()));
        }
        if (!labels.insert(parsed->label).second) {
            throw template_output_error(
                "line " + std::to_string(cursor + 1) +
                ": duplicate template label C" + std::to_string(parsed->label));
        }

        try {
            templates.push_back(
                labelled_pattern_sequence{parsed->label, parse_pattern(parsed->body)});
        } catch (const template_output_error& error) {
            throw line_error(cursor, error);
        }
        ++cursor;
    }

    while (cursor < lines.size() && is_separator(lines[cursor])) {
        ++cursor;
    }
    if (cursor < lines.size()) {
        try {
            if (parse_template_line(lines[cursor])) {
                throw template_output_error(
                    "final Summary declares " + std::to_string(expected_count) +
                    " templates; additional template line found at line " +
                    std::to_string(cursor + 1));
            }
        } catch (const template_output_error& error) {
            throw line_error(cursor, error);
        }
        throw template_output_error(
            "unexpected output after final template block at line " +
            std::to_string(cursor + 1) +
            "; the file may contain first-pass output");
    }

    return templates;
}

std::vector<labelled_pattern_sequence> prova::loga::parse_template_output(
    std::istream& output) {
    std::ostringstream buffer;
    buffer << output.rdbuf();
    if (output.bad()) {
        throw template_output_error("failed to read template output");
    }
    return parse_template_output(buffer.str());
}
