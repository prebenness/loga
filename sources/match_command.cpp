#include "match_command.h"

#include <loga/template_matcher.h>
#include <loga/template_output.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/program_options.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string json_string(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u00"
                       << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<unsigned int>(ch)
                       << std::dec;
            } else {
                output << static_cast<char>(ch);
            }
        }
    }
    output << '"';
    return output.str();
}

void write_match(
    std::ostream& output,
    const prova::loga::labelled_pattern_sequence& labelled_pattern,
    const prova::loga::template_match_result& match) {
    output << "{\"template\":\"C" << labelled_pattern.label << "\",";
    output << "\"capture_ambiguous\":"
           << (match.captures_ambiguous ? "true" : "false") << ',';

    if (match.captures_ambiguous) {
        output << "\"captures\":null}";
        return;
    }

    output << "\"captures\":[";
    for (std::size_t i = 0; i < match.captures.size(); ++i) {
        if (i > 0) {
            output << ',';
        }
        const auto& capture = match.captures.at(i);
        output << "{\"placeholder\":" << capture.placeholder_index << ','
               << "\"token_begin\":" << capture.token_begin << ','
               << "\"token_end\":" << capture.token_end << ','
               << "\"value\":" << json_string(capture.value) << '}';
    }
    output << "]}";
}

bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(left, right, equivalent_error) &&
        !equivalent_error) {
        return true;
    }

    std::error_code left_error;
    std::error_code right_error;
    const auto canonical_left = std::filesystem::weakly_canonical(left, left_error);
    const auto canonical_right = std::filesystem::weakly_canonical(right, right_error);
    return !left_error && !right_error && canonical_left == canonical_right;
}

} // namespace

int prova::loga::run_match_command(int argc, char** argv) {
    namespace po = boost::program_options;

    try {
        po::options_description options("Match options");
        options.add_options()
            ("help,h", "Print help message")
            ("templates", po::value<std::string>(),
                "Raw second-pass Loga output containing final templates")
            ("input", po::value<std::string>(), "Log file to match")
            ("output", po::value<std::string>(), "JSONL result file");

        po::variables_map values;
        po::store(po::command_line_parser(argc, argv).options(options).run(), values);

        if (values.count("help")) {
            std::cout << "Usage: loga match"
                      << " --templates <pass-02-output> --input <log>"
                         " --output <matches.jsonl>\n";
            std::cout << options << '\n';
            return 0;
        }

        for (const std::string_view required : {"templates", "input", "output"}) {
            if (!values.count(std::string(required))) {
                throw po::error(
                    "missing required option --" + std::string(required));
            }
        }
        po::notify(values);

        const std::filesystem::path templates_path =
            values["templates"].as<std::string>();
        const std::filesystem::path input_path = values["input"].as<std::string>();
        const std::filesystem::path output_path = values["output"].as<std::string>();

        if (same_path(output_path, templates_path) || same_path(output_path, input_path)) {
            throw std::runtime_error(
                "output path must differ from the template and input paths");
        }

        std::ifstream template_stream(templates_path, std::ios::binary);
        if (!template_stream) {
            throw std::runtime_error(
                "cannot open template output '" + templates_path.string() + "'");
        }
        const auto templates = parse_template_output(template_stream);
        if (templates.empty()) {
            throw std::runtime_error("template output contains no final templates");
        }

        std::ifstream input(input_path);
        if (!input) {
            throw std::runtime_error(
                "cannot open input log '" + input_path.string() + "'");
        }

        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "cannot open output file '" + output_path.string() + "'");
        }

        std::string line;
        std::size_t source_line = 0;
        while (std::getline(input, line)) {
            ++source_line;
            boost::algorithm::trim(line);
            if (line.empty()) {
                continue;
            }

            const tokenized tokenized_line(line);
            output << "{\"line\":" << source_line
                   << ",\"message\":" << json_string(line)
                   << ",\"matches\":[";

            bool first_match = true;
            for (const auto& labelled_pattern : templates) {
                const auto match =
                    match_template(labelled_pattern.pattern, tokenized_line);
                if (!match.accepted) {
                    continue;
                }
                if (!first_match) {
                    output << ',';
                }
                write_match(output, labelled_pattern, match);
                first_match = false;
            }
            output << "]}\n";
        }

        if (!input.eof()) {
            throw std::runtime_error(
                "failed while reading input log '" + input_path.string() + "'");
        }
        if (!output) {
            throw std::runtime_error(
                "failed while writing output file '" + output_path.string() + "'");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
