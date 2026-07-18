#ifndef PROVA_LOGA_TEMPLATE_OUTPUT_H
#define PROVA_LOGA_TEMPLATE_OUTPUT_H

#include <loga/pattern_sequence.h>

#include <cstddef>
#include <istream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace prova::loga {

struct labelled_pattern_sequence {
    std::size_t label;
    pattern_sequence pattern;
};

class template_output_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<labelled_pattern_sequence> parse_template_output(std::string_view output);
std::vector<labelled_pattern_sequence> parse_template_output(std::istream& output);

} // namespace prova::loga

#endif // PROVA_LOGA_TEMPLATE_OUTPUT_H
