#include <loga/token.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

double distance(const std::string& left, const std::string& right) {
    return prova::loga::levenshtein_distance(
        left.cbegin(), left.cend(), right.cbegin(), right.cend());
}

bool expect_distance(const std::string& left, const std::string& right, double expected) {
    const double actual = distance(left, right);
    if (actual == expected) {
        return true;
    }

    std::cerr << "distance(\"" << left << "\", \"" << right << "\") = "
              << actual << "; expected " << expected << '\n';
    return false;
}

} // namespace

int main() {
    bool passed = true;
    passed &= expect_distance("", "", 0.0);
    passed &= expect_distance("abc", "abc", 0.0);
    passed &= expect_distance("abc", "adc", 1.0);
    passed &= expect_distance("abc", "xyz", 3.0);
    passed &= expect_distance("abc", "abxc", 1.0);
    passed &= expect_distance("abxc", "abc", 1.0);
    passed &= expect_distance("kitten", "sitting", 3.0);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
