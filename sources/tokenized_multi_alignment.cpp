#include "loga/tokenized_multi_alignment.h"
#include <iostream>
#include <format>
#include <array>
#include <iomanip>
#include <numeric>

prova::loga::tokenized_multi_alignment::region_map prova::loga::tokenized_multi_alignment::align(double cutoff) const {
    filter_type ignored_exclusions;
    return align(cutoff, ignored_exclusions);
}

prova::loga::tokenized_multi_alignment::region_map prova::loga::tokenized_multi_alignment::align(
    double cutoff,
    filter_type& cumulative_excluded) const {
    interval_map intervals;
    region_map regions;
    std::set<std::size_t> excluded;

    assert(_filter.contains(_base_index));

    while(true) {
        intervals.clear();
        regions.clear();
    
        // { construct intervals mapping [s, e) -> {t, s, r}
        //      by considering all paths starting from the base item to all reference items allowed by the filter
        //      where (s, e) are the offsets of the start and end position of the base string corresponding to a matched string segment
        //      t is the index of the reference string and r is the start position in the reference string that align with the matched segment
        for(const auto& [key, path]: _matrix) {
            if(key.first != _base_index) continue;
            if(!_filter.contains(key.second)) continue;
            if(excluded.contains(key.second)) continue;
            // std::cout << std::format("({},{})", key.first, key.second) << "| ";
            // std::cout << "Key: " << std::format("({},{})", key.first, key.second) << " " << path.size() << std::endl;
            for(const auto& s: path){
                prova::loga::index start = s.start();
                std::size_t start_pos = start.at(0);
                std::size_t end_pos   = start_pos + s.length();
                interval_type::type interval = interval_type::right_open(start_pos, end_pos);
                interval_val val;
                matched_val matched;
                matched.id = key.second;
                matched.base_pos = start_pos;
                matched.ref_pos = start.at(1);
                val.insert(matched);
                intervals.add(std::make_pair(interval, val)); //
                // std::cout << std::format("[{}, {})", start_pos, end_pos) << "-" << s.start();
                // std::cout << s << "~~~";
            }
            // std::cout << std::endl;
        }
        // }
        // postcondition: intervals only contains references allowed by the filter

        // { construct regions and empty regions map mapping t -> interval_set
        //      where interval_set is an association of [s, e) -> {constant, placeholder}
        //      and s, e are offsets in the reference string
        for(std::size_t i = 0; i != _collection.count(); ++i) {
            if(!_filter.contains(i)) continue;
            regions.insert(std::make_pair(i, interval_set{}));
        }
        // }
        // postcondition: all keys in the regions refers to an index pointing to an item allowed by the filter
        // postcondition: all values in the regions are empty interval_set

        // const prova::loga::tokenized& base_ref = _collection.at(_base_index);
        double consensus_requirement = cutoff;
        assert(consensus_requirement > 0.0 && consensus_requirement < 1.0);
        double consensus_leftover    = 1 - consensus_requirement;
        std::size_t max_possible     = (_filter.size()-1) - excluded.size();
        std::size_t threshold        = 0;
        if(max_possible <= 2)
            threshold = max_possible;
        else {
            threshold        = max_possible > std::floor(1.0/consensus_leftover)
                                    ? std::floor(0.9 * max_possible)
                                    : max_possible - 2;
        }

        std::set<std::size_t> accidentals;
        for(const auto& iv: intervals) {
            std::size_t num_references = iv.second.size();
            if(num_references == max_possible) {
                std::size_t len = iv.first.upper() - iv.first.lower();
                std::set<zone> zones;
                zones.insert(zone::constant);
                regions[_base_index].add(std::make_pair(region_type::right_open(iv.first.lower(), iv.first.lower() +len), zones));
                for(const matched_val& v: iv.second) {
                    std::size_t delta = iv.first.lower() - v.base_pos;
                    std::size_t ref_start = v.ref_pos+delta;
                    std::size_t ref_end   = delta+v.ref_pos+len;
                    std::set<zone> zones;
                    zones.insert(zone::constant);
                    regions[v.id].add(std::make_pair(region_type::right_open(ref_start, ref_end), zones));
                }
            } else {
                std::size_t offset = iv.first.lower();
                std::size_t len = iv.first.upper() - iv.first.lower();
                std::string substr = _collection.at(0).subset(offset, len).view();
                if(num_references > threshold) {
                    std::cout << "not voted: " << substr << " " << "votes: " << num_references << " threshold " << threshold << " max_possible " << max_possible<< std::endl;
                    std::size_t max = _collection.count()+1;
                    std::vector<std::size_t> responded_refs(_collection.count(), max);
                    responded_refs[_base_index] = _base_index;
                    for(const matched_val& v: iv.second) {
                        responded_refs[v.id] = v.id;
                    }

                    for(std::size_t i = 0; i < _collection.count(); ++i) {
                        if(responded_refs.at(i) == max) {
                            accidentals.insert(i);
                        }
                    }
                    std::cout << "unaligned ";
                    std::copy(accidentals.begin(), accidentals.end(), std::ostream_iterator<std::size_t>(std::cout, " "));
                    std::cout << std::endl;
                }
            }
        }

        if(accidentals.size() > 0) {
            std::cout << "excluding accidentals ";
            std::copy(accidentals.begin(), accidentals.end(), std::ostream_iterator<std::size_t>(std::cout, " "));
            std::cout << std::endl;
            for(std::size_t id: accidentals) {
                regions.erase(id);
                excluded.insert(id);
            }
        } else {
            break;
        }
    };

    for(auto& candidate: regions) {
        // std::cout << candidate.first << " {" << candidate.second.size() << "}" << std::endl;
        std::size_t last = 0;
        std::vector<region_type> placeholders;
        for(const auto& z: candidate.second) {
            if(z.first.lower() > last) {
                placeholders.push_back(region_type::right_open(last, z.first.upper()));
            }
            last = z.first.upper();
        }
        std::size_t end = _collection.at(candidate.first).count();
        if(end > last) {
            placeholders.push_back(region_type::right_open(last, end));
        }
        
        std::set<zone> zones;
        zones.insert(zone::placeholder);
        for(const auto& p: placeholders) {
            candidate.second.add(std::make_pair(p, zones));
        }
    }
    
    cumulative_excluded = excluded;
    return regions;
}



prova::loga::tokenized_multi_alignment::region_map prova::loga::tokenized_multi_alignment::fixture_word_boundary(const region_map &regions) const{
    region_map result;

    return result;

}

prova::loga::tokenized_multi_alignment::tokenized_multi_alignment(const tokenized_collection &collection, const tokenized_alignment::matrix_type &matrix, const filter_type &filter, std::size_t base_index): _collection(collection), _matrix(matrix), _filter(filter), _base_index(base_index) {}

prova::loga::tokenized_multi_alignment::tokenized_multi_alignment(const tokenized_collection &collection, const tokenized_alignment::matrix_type &matrix, std::size_t base_index): _collection(collection), _matrix(matrix), _base_index(base_index)  {
    for(std::size_t i = 0; i != _collection.count(); ++i) {
        _filter.insert(i);
    }
}

std::ostream &prova::loga::tokenized_multi_alignment::print_regions(const region_map &regions, std::ostream &stream){
    for(auto& candidate: regions) {
        for(const auto& z: candidate.second) {
            prova::loga::zone tag = *z.second.cbegin(); // set has only one item
            const prova::loga::tokenized& ref = _collection.at(candidate.first);
            std::size_t offset = z.first.lower();
            std::size_t len = z.first.upper()-z.first.lower();
            stream << z.first << " <" << ref.subset(offset, len).view() << "> " << tag << std::endl;
        }
        
        stream << std::endl;
    }
    return stream;
}

std::ostream &prova::loga::tokenized_multi_alignment::print_regions_string(const region_map &regions, std::ostream &stream){
    constexpr std::string_view red     = "\033[0;31m";
    constexpr std::string_view green   = "\033[0;32m";
    constexpr std::string_view yellow  = "\033[0;33m";
    constexpr std::string_view blue    = "\033[0;34m";
    constexpr std::string_view magenta = "\033[0;35m";
    constexpr std::string_view cyan    = "\033[0;36m";
    constexpr std::string_view bright_black   = "\033[1;30m";
    constexpr std::string_view bright_red     = "\033[1;31m";
    constexpr std::string_view bright_green   = "\033[1;32m";
    constexpr std::string_view bright_yellow  = "\033[1;33m";
    constexpr std::string_view bright_blue    = "\033[1;34m";
    constexpr std::string_view bright_magenta = "\033[1;35m";
    constexpr std::string_view bright_cyan    = "\033[1;36m";

    constexpr std::array<std::string_view, 13> palette = {
        red, green, yellow, blue, magenta,
        cyan, bright_red, bright_green, bright_yellow,
        bright_blue, bright_magenta, bright_cyan, bright_black
    };

    constexpr std::string_view reset = "\033[0m";

    for(const auto& [id, zones]: regions) {
        stream << std::right << std::setw(5) << id << "|" << std::resetiosflags(std::ios::showbase);
        print_interval_set(zones, _collection.at(id), stream);
        stream << std::endl;
    }
    return stream;
}

std::ostream &prova::loga::tokenized_multi_alignment::print_interval_set(const interval_set &interval, const std::string &ref, std::ostream &stream){
    constexpr std::string_view red     = "\033[0;31m";
    constexpr std::string_view green   = "\033[0;32m";
    constexpr std::string_view yellow  = "\033[0;33m";
    constexpr std::string_view blue    = "\033[0;34m";
    constexpr std::string_view magenta = "\033[0;35m";
    constexpr std::string_view cyan    = "\033[0;36m";
    constexpr std::string_view bright_black   = "\033[1;30m";
    constexpr std::string_view bright_red     = "\033[1;31m";
    constexpr std::string_view bright_green   = "\033[1;32m";
    constexpr std::string_view bright_yellow  = "\033[1;33m";
    constexpr std::string_view bright_blue    = "\033[1;34m";
    constexpr std::string_view bright_magenta = "\033[1;35m";
    constexpr std::string_view bright_cyan    = "\033[1;36m";

    constexpr std::array<std::string_view, 13> palette = {
        red, green, yellow, blue, magenta,
        cyan, bright_red, bright_green, bright_yellow,
        bright_blue, bright_magenta, bright_cyan, bright_black
    };

    constexpr std::string_view reset = "\033[0m";

    std::size_t placeholder_count = 0;
    for(const auto& z: interval) {
        prova::loga::zone tag = *z.second.cbegin(); // set has only one item
        std::size_t offset = z.first.lower();
        std::size_t len = z.first.upper()-z.first.lower();
        std::string substr = ref.substr(offset, len);
        std::transform(substr.cbegin(), substr.cend(), substr.begin(), [](const char& c){
            return c == ' ' ? '~' : c;
        });
        if(tag == zone::constant)
            stream << substr;
        else {
            const auto& color = palette.at(placeholder_count++ % palette.size());
            stream << color << substr << reset;
        }
    }
    return stream;
}

std::ostream& prova::loga::tokenized_multi_alignment::print_interval_set(const interval_set& interval, const prova::loga::tokenized& ref, std::ostream& stream){
    constexpr std::string_view red     = "\033[0;31m";
    constexpr std::string_view green   = "\033[0;32m";
    constexpr std::string_view yellow  = "\033[0;33m";
    constexpr std::string_view blue    = "\033[0;34m";
    constexpr std::string_view magenta = "\033[0;35m";
    constexpr std::string_view cyan    = "\033[0;36m";
    constexpr std::string_view bright_black   = "\033[1;30m";
    constexpr std::string_view bright_red     = "\033[1;31m";
    constexpr std::string_view bright_green   = "\033[1;32m";
    constexpr std::string_view bright_yellow  = "\033[1;33m";
    constexpr std::string_view bright_blue    = "\033[1;34m";
    constexpr std::string_view bright_magenta = "\033[1;35m";
    constexpr std::string_view bright_cyan    = "\033[1;36m";

    constexpr std::array<std::string_view, 13> palette = {
        red, green, yellow, blue, magenta,
        cyan, bright_red, bright_green, bright_yellow,
        bright_blue, bright_magenta, bright_cyan, bright_black
    };

    constexpr std::string_view reset = "\033[0m";

    std::size_t placeholder_count = 0;
    for(const auto& z: interval) {
        prova::loga::zone tag = *z.second.cbegin(); // set has only one item
        std::size_t offset = z.first.lower();
        std::size_t len = z.first.upper()-z.first.lower();
        assert(ref.count() >= offset + len);
        std::string substr = ref.subset(offset, len).view();
        // std::transform(substr.cbegin(), substr.cend(), substr.begin(), [](const char& c){
        //     return c == ' ' ? '~' : c;
        // });
        if(tag == zone::constant)
            stream << substr;
        else {
            const auto& color = palette.at(placeholder_count++ % palette.size());
            stream << color << substr << reset;
        }
    }
    return stream;
}

bool prova::loga::tokenized_multi_alignment::matched_val::operator<(const matched_val &other) const {
    return id < other.id;
}

bool prova::loga::tokenized_multi_alignment::matched_val::operator==(const matched_val &other) const {
    return id == other.id && ref_pos == other.ref_pos && base_pos == other.base_pos;
}

std::ostream &prova::loga::operator<<(std::ostream &stream, const tokenized_multi_alignment::matched_val &mval) {
    stream << std::format("{}:{}>{}", mval.id, mval.base_pos, mval.ref_pos);
    return stream;
}

std::ostream &prova::loga::operator<<(std::ostream &stream, const std::set<tokenized_multi_alignment::matched_val> &set) {
    std::copy(set.cbegin(), set.cend(), std::ostream_iterator<tokenized_multi_alignment::matched_val>(stream, ", "));
    stream << std::endl;
    return stream;
}
