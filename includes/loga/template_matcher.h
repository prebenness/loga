#ifndef PROVA_LOGA_TEMPLATE_MATCHER_H
#define PROVA_LOGA_TEMPLATE_MATCHER_H

#include <loga/pattern_sequence.h>

#include <cstddef>
#include <string>
#include <vector>

namespace prova::loga {

struct template_capture {
    std::size_t placeholder_index;
    std::size_t token_begin;
    std::size_t token_end;
    std::string value;
};

struct template_match_result {
    bool accepted = false;
    bool captures_ambiguous = false;
    std::vector<template_capture> captures;
};

/**
 * Match one tokenized line against a pattern produced by Loga.
 *
 * Constants must equal complete input tokens. A placeholder may consume zero
 * or more complete input tokens. The match is anchored at both ends. Captures
 * are returned only when exactly one assignment of tokens to placeholders
 * accepts the line; captures_ambiguous is true when more than one assignment
 * accepts it.
 */
template_match_result match_template(
    const pattern_sequence& pattern,
    const tokenized& line);

} // namespace prova::loga

#endif // PROVA_LOGA_TEMPLATE_MATCHER_H
