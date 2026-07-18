#include "match_command.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool expect(bool condition, std::string_view description) {
    if (condition) {
        return true;
    }
    std::cerr << "failed: " << description << '\n';
    return false;
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) {
        throw std::runtime_error("failed to write test fixture");
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

int invoke(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    return prova::loga::run_match_command(
        static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
                           "loga-match-command-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory);

    const auto templates = directory / "templates.log";
    const auto input = directory / "input.log";
    const auto output = directory / "matches.jsonl";

    write_file(
        templates,
        "----------------------------\n"
        "Summary: 2 clusters\n"
        "----------------------------\n"
        "C3 $A,$0,B\n"
        "C7 $A$0\n");
    write_file(input, "  $A,12,B  \n\n$X\n");

    bool passed = true;
    passed &= expect(
        invoke({
            "match",
            "--templates", templates.string(),
            "--input", input.string(),
            "--output", output.string()}) == 0,
        "batch match command succeeds");

    const std::string expected =
        "{\"line\":1,\"message\":\"$A,12,B\",\"matches\":["
        "{\"template\":\"C3\",\"capture_ambiguous\":false,\"captures\":["
        "{\"placeholder\":0,\"token_begin\":3,\"token_end\":4,\"value\":\"12\"}]},"
        "{\"template\":\"C7\",\"capture_ambiguous\":false,\"captures\":["
        "{\"placeholder\":0,\"token_begin\":2,\"token_end\":6,\"value\":\",12,B\"}]}]}\n"
        "{\"line\":3,\"message\":\"$X\",\"matches\":[]}\n";
    passed &= expect(read_file(output) == expected,
                     "JSONL output is complete and deterministic");
    passed &= expect(!std::filesystem::exists(directory / "input.log_d"),
                     "match command creates no training artefacts");

    passed &= expect(
        invoke({
            "match",
            "--templates", templates.string(),
            "--input", input.string(),
            "--output", input.string()}) != 0,
        "command rejects an output path that would overwrite its input");

    const auto hard_link = directory / "input-hard-link.log";
    std::filesystem::create_hard_link(input, hard_link, error);
    if (!error) {
        passed &= expect(
            invoke({
                "match",
                "--templates", templates.string(),
                "--input", input.string(),
                "--output", hard_link.string()}) != 0,
            "command rejects a hard link to its input as output");
    }

    std::filesystem::remove_all(directory, error);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
