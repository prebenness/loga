#include <loga/template_matcher.h>

#include <cstdint>
#include <vector>

namespace {

using match_count = std::uint8_t;

constexpr match_count no_matches = 0;
constexpr match_count one_match = 1;
constexpr match_count multiple_matches = 2;
constexpr match_count not_computed = 3;

class matcher {
public:
    matcher(
        const prova::loga::pattern_sequence& pattern,
        const prova::loga::tokenized& line)
        : _pattern(pattern),
          _line(line),
          _memo(pattern.size() + 1,
                std::vector<match_count>(line.count() + 1, not_computed)) {}

    prova::loga::template_match_result run() {
        const match_count count = count_matches(0, 0);

        prova::loga::template_match_result result;
        result.accepted = count != no_matches;
        result.captures_ambiguous = count == multiple_matches;

        if (count == one_match) {
            result.captures = unique_captures();
        }
        return result;
    }

private:
    const prova::loga::pattern_sequence& _pattern;
    const prova::loga::tokenized& _line;
    std::vector<std::vector<match_count>> _memo;

    static match_count add_capped(match_count left, match_count right) {
        if (left == multiple_matches || right == multiple_matches ||
            left + right >= multiple_matches) {
            return multiple_matches;
        }
        return static_cast<match_count>(left + right);
    }

    bool constant_matches(
        const prova::loga::tokenized& constant,
        std::size_t token_index) const {
        if (token_index > _line.count() ||
            constant.count() > _line.count() - token_index) {
            return false;
        }

        for (std::size_t i = 0; i < constant.count(); ++i) {
            if (constant.at(i).view() != _line.at(token_index + i).view()) {
                return false;
            }
        }
        return true;
    }

    match_count count_matches(
        std::size_t segment_index,
        std::size_t token_index) {
        match_count& cached = _memo.at(segment_index).at(token_index);
        if (cached != not_computed) {
            return cached;
        }

        if (segment_index == _pattern.size()) {
            cached = token_index == _line.count() ? one_match : no_matches;
            return cached;
        }

        const auto& segment = _pattern.at(segment_index);
        if (segment.tag() == prova::loga::zone::constant) {
            if (!constant_matches(segment.tokens(), token_index)) {
                cached = no_matches;
            } else {
                cached = count_matches(
                    segment_index + 1,
                    token_index + segment.tokens().count());
            }
            return cached;
        }

        cached = no_matches;
        for (std::size_t endpoint = token_index;
             endpoint <= _line.count();
             ++endpoint) {
            cached = add_capped(
                cached,
                count_matches(segment_index + 1, endpoint));
            if (cached == multiple_matches) {
                break;
            }
        }
        return cached;
    }

    std::vector<prova::loga::template_capture> unique_captures() {
        std::vector<prova::loga::template_capture> captures;
        std::size_t segment_index = 0;
        std::size_t token_index = 0;
        std::size_t placeholder_index = 0;

        while (segment_index < _pattern.size()) {
            const auto& segment = _pattern.at(segment_index);
            if (segment.tag() == prova::loga::zone::constant) {
                token_index += segment.tokens().count();
                ++segment_index;
                continue;
            }

            std::size_t endpoint = token_index;
            for (; endpoint <= _line.count(); ++endpoint) {
                if (count_matches(segment_index + 1, endpoint) != no_matches) {
                    break;
                }
            }

            captures.push_back(prova::loga::template_capture{
                placeholder_index,
                token_index,
                endpoint,
                _line.subset(token_index, endpoint - token_index).view()});

            token_index = endpoint;
            ++placeholder_index;
            ++segment_index;
        }

        return captures;
    }
};

} // namespace

prova::loga::template_match_result prova::loga::match_template(
    const prova::loga::pattern_sequence& pattern,
    const prova::loga::tokenized& line) {
    return matcher(pattern, line).run();
}
