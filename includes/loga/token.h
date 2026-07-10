#ifndef PROVA_ALIGN_TOKEN_H
#define PROVA_ALIGN_TOKEN_H

#include <cstddef>
#include <boost/icl/interval.hpp>
#include <boost/icl/split_interval_map.hpp>
#include <boost/icl/separate_interval_set.hpp>
#include <loga/colors.h>
#include <loga/alignment.h>
#include <boost/range/combine.hpp>
#include <boost/functional/hash.hpp>

namespace prova::loga{

struct token{
    struct coordinate{
        using collection_type = std::vector<std::size_t>;
        using const_iterator  = collection_type::const_iterator;
        using value_type      = collection_type::value_type;

        coordinate(std::size_t k, std::size_t count = 1): _coords(5, 0) {
            // assert(k < 4);
            _coords[k] = count;
        }

        auto begin() const { return _coords.begin(); }
        auto end()   const { return _coords.end(); }
        auto size()  const { return 5; }
        auto at(std::size_t i) const { return _coords.at(i); }
        void scale(std::size_t n) {
            std::transform(_coords.begin(), _coords.end(), _coords.begin(), [n](std::size_t& v){
                return v*n;
            });
        }

    private:
        std::vector<std::size_t> _coords;
    };

    friend bool operator==(const token& l, const token& r);

    using boundary_type = std::size_t;
    using interval_type = boost::icl::discrete_interval<boundary_type>;

    enum class category{
        none, alpha, digits, symbol, space, place
    };

    token() = default;
    token(boundary_type begin, boundary_type end, category cat);

    boundary_type lower()  const;
    boundary_type upper()  const;
    std::size_t   length() const;

    token::category cat() const;

    static token::category classify(std::uint8_t ch);
    static token::coordinate vector(token::category cat);

    token::coordinate vector() const;

private:
    interval_type _interval;
    category      _category;
};

struct index_pair_hasher{
    std::size_t operator()(const std::pair<std::size_t, std::size_t>& p) const noexcept{
        std::size_t seed = 0;
        boost::hash_combine(seed, p.first);
        boost::hash_combine(seed, p.second);
        return seed;
    }
};

template <typename T, std::enable_if_t<std::is_same_v<T, prova::loga::token::coordinate>, bool> = true>
double sim_score(const T& l_coordinate, const T& r_coordinate) {
    if(l_coordinate.at(4) || r_coordinate.at(4))
        return true;

    double d = 1.0f;
    for(const auto& z: boost::combine(l_coordinate, r_coordinate)){
        std::size_t l_scale = z.template get<0>();
        std::size_t r_scale = z.template get<1>();

        bool l_pos = l_scale > 0;
        bool r_pos = r_scale > 0;

        if(!(l_pos ^ r_pos)) {                            // either both are 0 or both > 0
            if(l_pos && r_pos) {
                if(l_scale == r_scale) {
                    d = 0.0f;                            // perfect 0 to get rid of floating point issues
                } else {
                    double bray_curtis_dissym = 1 - (2* std::min(l_scale, r_scale) / double(l_scale + r_scale));
                    d = bray_curtis_dissym;
                }
                break;
            }
        } else {
            d = 1.0f;                                  // break one first dimensional mismatch (tokens have different class)
            break;
        }
    }
    return d;
}

template <typename T, std::enable_if_t<!std::is_same_v<T, prova::loga::token::coordinate>, bool> = true>
double sim_score(const T& l, const T& r) {
    return l == r ? 0.0 : 1.0;
}

template <typename Iterator>
double levenshtein_distance_detail(Iterator l_begin, Iterator l_it, Iterator l_end, Iterator r_begin, Iterator r_it, Iterator r_end, std::unordered_map<std::pair<std::size_t, std::size_t>, double, index_pair_hasher>& memo) {
    std::size_t l_length = std::distance(l_it, l_end);
    std::size_t r_length = std::distance(r_it, r_end);

    auto key = std::make_pair(std::distance(l_begin, l_it), std::distance(r_begin, r_it));
    if(memo.contains(key)) return memo.at(key);

    if(r_length == 0) return memo.insert(std::make_pair(key, l_length)).first->second;
    if(l_length == 0) return memo.insert(std::make_pair(key, r_length)).first->second;

    double d = sim_score(*l_it, *r_it);

    Iterator l_tail = l_it;
    Iterator r_tail = r_it;

    std::advance(l_tail, 1);
    std::advance(r_tail, 1);

    auto ltrf_key = std::make_pair(std::distance(l_begin, l_tail),  std::distance(r_begin, r_it));
    auto lfrt_key = std::make_pair(std::distance(l_begin, l_it),    std::distance(r_begin, r_tail));
    auto ltrt_key = std::make_pair(std::distance(l_begin, l_tail),  std::distance(r_begin, r_tail));

    if(d == 0) return memo.contains(ltrt_key)
            ? memo.at(ltrt_key)
            : memo.insert(
                std::make_pair(
                    ltrt_key,
                    levenshtein_distance_detail(l_begin, l_tail, l_end, r_begin, r_tail, r_end, memo)
                )
              ).first->second;

    double ltrf = memo.contains(ltrf_key)
                      ? memo.at(ltrf_key)
                      : memo.insert(
                            std::make_pair(
                                ltrf_key,
                                levenshtein_distance_detail(l_begin, l_tail,  l_end, r_begin, r_it, r_end, memo)
                            )
                        ).first->second;
    double lfrt = memo.contains(lfrt_key)
                      ? memo.at(lfrt_key)
                      : memo.insert(
                            std::make_pair(
                                lfrt_key,
                                levenshtein_distance_detail(l_begin, l_it, l_end, r_begin, r_tail, r_end, memo)
                            )
                        ).first->second;
    double ltrt = memo.contains(ltrt_key)
                      ? memo.at(ltrt_key)
                      : memo.insert(
                            std::make_pair(
                                ltrt_key,
                                levenshtein_distance_detail(l_begin, l_tail,  l_end, r_begin, r_tail, r_end, memo)
                            )
                        ).first->second;

    const double best = std::min({1.0 + ltrf, 1.0 + lfrt, d + ltrt});
    return memo.insert(std::make_pair(key, best)).first->second;
}

template <typename Iterator>
double levenshtein_distance(Iterator l_begin, Iterator l_end, Iterator r_begin, Iterator r_end) {
    std::unordered_map<std::pair<std::size_t, std::size_t>, double, index_pair_hasher> memo;
    double distance = levenshtein_distance_detail(l_begin, l_begin, l_end, r_begin, r_begin, r_end, memo);
    return distance;
}

template <typename Iterator>
std::size_t lcs_detail(Iterator l_begin, Iterator l_it, Iterator l_end, Iterator r_begin, Iterator r_it, Iterator r_end, std::unordered_map<std::pair<std::size_t, std::size_t>, std::size_t, index_pair_hasher>& memo) {
    std::size_t l_length = std::distance(l_it, l_end);
    std::size_t r_length = std::distance(r_it, r_end);

    auto key = std::make_pair(std::distance(l_begin, l_it), std::distance(r_begin, r_it));
    if(memo.contains(key)) return memo.at(key);

    if(l_length == 0 || r_length == 0) return memo.insert(std::make_pair(key, 0)).first->second;

    const auto& l_value = *l_it;
    const auto& r_value = *r_it;

    bool matched = (l_value == r_value);

    std::size_t result = 0;
    if(matched) {
        result = 1+lcs_detail(l_begin, std::next(l_it), l_end, r_begin, std::next(r_it), r_end, memo);
    } else {
        result = std::max(
                lcs_detail(l_begin, std::next(l_it), l_end, r_begin, r_it,            r_end, memo),
                lcs_detail(l_begin, l_it,            l_end, r_begin, std::next(r_it), r_end, memo)
            );
    }

    return memo.insert(std::make_pair(key, result)).first->second;
}

template <typename Iterator>
double lcs(Iterator l_begin, Iterator l_end, Iterator r_begin, Iterator r_end){
    std::unordered_map<std::pair<std::size_t, std::size_t>, std::size_t, index_pair_hasher> memo;
    std::size_t distance = lcs_detail(l_begin, l_begin, l_end, r_begin, r_begin, r_end, memo);
    return distance;
}

inline bool operator==(const token::coordinate& lc, const token::coordinate& rc) {
    bool matched = true;
    for(const auto& zipped: boost::combine(lc, rc)){
        if(zipped.get<0>() != zipped.get<1>()) {
            matched = false;
            break;
        }
    }
    return matched;
}

inline std::ostream& operator<<(std::ostream& stream, const token::category& cat) {
    if(cat == token::category::alpha) {
        stream << "A";
    } else if(cat == token::category::digits) {
        stream << "D";
    } else if(cat == token::category::symbol) {
        stream << "@";
    } else if(cat == token::category::space) {
        stream << "S";
    } else if(cat == token::category::place) {
        stream << "$";
    }

    return stream;
}

struct tokenized;

struct wrapped{
    friend struct tokenized;

    wrapped(const std::string& str, const token& token);
    wrapped(const wrapped& other);

    // wrapped& operator=(const wrapped& other);

    std::string_view view() const;

    friend inline bool operator<(const wrapped& l, const wrapped& r) {
        return l.view() < r.view();
    }

    token::boundary_type lower()  const;
    token::boundary_type upper()  const;
    std::size_t   length() const;
    const prova::loga::token& tkn() const { return _token; }

    std::size_t hash() const { return _hash; }

    token::category category() const;
private:
    const std::string& _str;
    prova::loga::token _token;
    std::size_t        _hash;
};

/**
 * @brief operator == compares the span of the token irrespective of the actual content
 * @param l
 * @param r
 * @return
 */
bool operator==(const token& l, const token& r);

/**
 * @brief operator == compares the actual strings irrespective of their positions when two wrapped tockens are compared
 * @param lw
 * @param rw
 * @return
 */
bool operator==(const wrapped& lw, const wrapped& rw);

struct tokenizer{
    template <typename InIterator, typename OutIterator>
    static std::size_t parse(InIterator begin, InIterator end, OutIterator out) {
        if (begin == end) return 0 ;

        std::size_t count      = 0;
        std::size_t last_stop  = 0;
        std::size_t buffer_len = 0;
        token::category last_cat = token::category::none;
        for(InIterator it = begin; it != end; it++) {
            token::category cat = token::classify(static_cast<std::uint8_t>(*it));
            assert(cat != token::category::none);

            if(cat == last_cat || it == begin){
                ++buffer_len;
            } else {
                std::size_t current_end = last_stop + buffer_len;
                *(out++) = token(last_stop, current_end, last_cat);

                last_stop  = current_end;
                buffer_len = 1;

                ++count;
            }
            last_cat = cat;
        }
        std::size_t current_end = last_stop + buffer_len;
        *(out++) = token(last_stop, current_end, last_cat);
        ++count;

        return count;
    }
};

struct subset{
    using token_list     = std::vector<wrapped>;
    using const_iterator = typename token_list::const_iterator;

    subset(const_iterator begin, const_iterator end): _begin(begin), _end(end) {}
    const_iterator begin() const { return _begin; }
    const_iterator end() const { return _end; }
    std::size_t count() const { return std::distance(_begin, _end); }

    std::string view() const {
        std::string str;
        for(const_iterator it = _begin; it != _end; ++it) {
            const token& tkn = it->tkn();
            std::string_view view = it->view();
            str += view;
        }
        return str;
    }

private:
    const_iterator _begin;
    const_iterator _end;
};

struct tokenized{
    using token_list     = std::vector<wrapped>;
    using const_iterator = typename token_list::const_iterator;
    using structure_type = std::vector<token::coordinate>;

    static tokenized nothing() { return tokenized(""); }

    friend inline bool operator<(const tokenized& l, const tokenized& r){
        return l._str < r._str;
    }

    friend inline std::ostream& operator<<(std::ostream& stream, const tokenized& sentence) {
        for(const wrapped& w: sentence) {
            // stream << "<" << w.category() << w.length() << "|" << w.view() << ">";
            auto cat = w.category();
            if(cat == token::category::alpha) {
                stream << loga::colors::blue;
            } else if(cat == token::category::digits) {
                stream << loga::colors::red;
            } else if(cat == token::category::symbol) {
                stream << loga::colors::green;
            } else if(cat == token::category::space) {
                stream << loga::colors::cyan;
            } else if(cat == token::category::place) {
                stream << loga::colors::yellow;
            }

            if(cat != token::category::space)
                stream << w.view();
            else
                stream << "¶";
            stream << loga::colors::reset;
        }
        return stream;
    }

    tokenized(const tokenized&);
    tokenized(tokenized&& other);
    tokenized(const std::string& str);

    tokenized& operator=(tokenized&& other);

    const_iterator begin() const;
    const_iterator end() const;
    std::size_t count() const;
    const wrapped& at(std::size_t i) const;

    const structure_type& structure() const;

    prova::loga::subset subset(std::size_t offset, std::size_t length) const {
        return prova::loga::subset(begin()+offset, begin()+offset+length);
    }

    const std::string& raw() const;

private:
    std::string _str;
    token_list  _tokens;
    structure_type _characteristics;

};

}

namespace std {
template <>
struct hash<prova::loga::token> {
    std::size_t operator()(const prova::loga::token& t) const noexcept {
        std::size_t h1 = std::hash<std::size_t>{}(t.lower());
        std::size_t h2 = std::hash<std::size_t>{}(t.upper());
        std::size_t h3 = std::hash<int>{}(static_cast<int>(t.cat()));

        // combine hashes (boost-like)
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

template <>
struct hash<prova::loga::wrapped> {
    std::size_t operator()(const prova::loga::wrapped& w) const noexcept {
        std::string_view v = w.view();
        return std::hash<std::string_view>{}(v);
    }
};

}

#endif // PROVA_ALIGN_TOKEN_H
