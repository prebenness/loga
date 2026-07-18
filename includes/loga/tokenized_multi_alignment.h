#ifndef PROVA_ALIGN_TOKENIZED_MULTI_ALIGNMENT_H
#define PROVA_ALIGN_TOKENIZED_MULTI_ALIGNMENT_H

#include <loga/fwd.h>
#include <cstddef>
#include <loga/tokenized_alignment.h>
#include <boost/icl/interval.hpp>
#include <boost/icl/split_interval_map.hpp>
#include <boost/icl/separate_interval_set.hpp>
#include <loga/zone.h>
#include <loga/path.h>
#include <numeric>

namespace prova {
namespace loga {

class tokenized_multi_alignment{
public:
    struct matched_val{
        std::size_t id;
        std::size_t ref_pos;
        std::size_t base_pos;
        std::size_t len;
        
        bool operator<(const matched_val& other) const;
        bool operator==(const matched_val& other) const;
    };
    
private:
    const tokenized_collection& _collection;
    const prova::loga::tokenized_alignment::matrix_type& _matrix;
    std::size_t _base_index;
    std::set<std::size_t> _filter;
    
public:
    using interval_val  = std::set<matched_val>;
    using interval_map  = boost::icl::split_interval_map<std::size_t, interval_val>;
    using interval_set  = boost::icl::split_interval_map<std::size_t, std::set<zone>>;
    using region_map    = std::map<std::size_t, interval_set>;
    using region_type   = interval_set::interval_type;
    using interval_type = interval_map::interval_type;
    using filter_type   = std::set<std::size_t>;
    using path_f_type   = std::function<prova::loga::path (std::size_t)>;
public:
    tokenized_multi_alignment(const tokenized_collection& collection, const tokenized_alignment::matrix_type& matrix, const filter_type& filter, std::size_t base_index);
    tokenized_multi_alignment(const tokenized_collection& collection, const tokenized_alignment::matrix_type& matrix, std::size_t base_index);
    region_map align(double cutoff = 0.9f) const;
    region_map align(double cutoff, filter_type& cumulative_excluded) const;

    template <typename Iterator, typename PathF>
    region_map align(Iterator begin, Iterator end, PathF path_f) const {
        interval_map intervals;
        region_map regions;

        assert(_filter.contains(_base_index));

        std::map<std::size_t, prova::loga::path> replacements;
        std::size_t tries = 0;
        do {
            intervals.clear();
            regions.clear();

            // { construct intervals mapping [s, e) -> {t, s, r}
            //      by considering all paths starting from the base item to all reference items allowed by the filter
            //      where (s, e) are the offsets of the start and end position of the base string corresponding to a matched string segment
            //      t is the index of the reference string and r is the start position in the reference string that align with the matched segment
            for(Iterator it = begin; it != end; ++it) {
                std::size_t id = it->first.second;
                const prova::loga::path& path = replacements.contains(id) ? replacements.at(id) : it->second;
                for(const auto& s: path){
                    prova::loga::index start = s.start();
                    std::size_t start_pos = start.at(0);
                    std::size_t end_pos   = start_pos + s.length();
                    interval_type::type interval = interval_type::right_open(start_pos, end_pos);
                    interval_val val;
                    matched_val matched;
                    matched.id = id;
                    matched.base_pos = start_pos;
                    matched.ref_pos = start.at(1);
                    matched.len = s.length();
                    assert(_collection.at(id).count() >= matched.ref_pos + matched.len);
                    val.insert(matched);
                    intervals.add(std::make_pair(interval, val));
                }
            }

            replacements.clear();

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
            // std::vector<std::size_t> accidentally_aligned_wrong_position;
            std::set<std::size_t> accidentals;
            for(const auto& iv: intervals) {
                std::size_t num_references = iv.second.size();
                if(num_references == _filter.size()-1) {
                    std::size_t len = iv.first.upper() - iv.first.lower();
                    std::set<zone> zones;
                    zones.insert(zone::constant);
                    regions[_base_index].add(std::make_pair(region_type::right_open(iv.first.lower(), iv.first.lower() +len), zones));
                    for(const matched_val& concensus_ref: iv.second) {
                        // original [a, b) -> after split interval -> [u, v) where u >= a and v <= b; \delta = (a-u)
                        const std::size_t u = iv.first.lower();
                        const std::size_t v = iv.first.upper();
                        const std::size_t a = concensus_ref.base_pos;
                        const std::size_t b = concensus_ref.base_pos + concensus_ref.len;
                        const std::size_t p = concensus_ref.ref_pos;
                        const std::size_t q = concensus_ref.ref_pos + concensus_ref.len;

                        std::size_t delta_l   = u - a;       // \delta_{l} = (u-a)
                        std::size_t delta_r   = b - v;       // \delta_{r} = (b-v)
                        std::size_t ref_start = p + delta_l; // ref [p, q) -> apply the same split -> [p+\delta_l, q-\delta_r)
                        std::size_t ref_end   = q - delta_r;
                        assert(_collection.at(concensus_ref.id).count() >= ref_end);
                        std::set<zone> zones;
                        zones.insert(zone::constant);
                        regions[concensus_ref.id].add(std::make_pair(region_type::right_open(ref_start, ref_end), zones));
                    }
                } else if(tries < 1) {
                    std::size_t offset = iv.first.lower();
                    std::size_t len = iv.first.upper() - iv.first.lower();
                    std::string substr = _collection.at(_base_index).subset(offset, len).view();

                    std::cout << "constant " << substr << " concensus: " << num_references << " expected: " << (_filter.size()-1) << std::endl;

                    if(num_references >= (4*(_filter.size()-1))/5) {
                        std::cout << "near unanimous: " << substr << " " << "votes: " << num_references << std::endl;
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
                    }
                }
            }

            if(accidentals.size() > 0) {
                std::cout << "accidentals ";
                std::copy(accidentals.begin(), accidentals.end(), std::ostream_iterator<std::size_t>(std::cout, " "));
                std::cout << std::endl;

                for(std::size_t id: accidentals) {
                    std::cout << "additional alignment for " << id << std::endl;
                    std::cout << _collection.at(id) << std::endl;
                    prova::loga::path alt_path = path_f(id);
                    replacements.insert(std::make_pair(id, alt_path));
                }
                accidentals.clear();
                ++tries;
                continue;
            }

            for(auto& candidate: regions) {
                // std::cout << candidate.first << " {" << candidate.second.size() << "}" << std::endl;
                std::size_t last = 0;
                std::vector<region_type> placeholders;
                for(const auto& z: candidate.second) {
                    if(z.first.lower() > last) {
                        placeholders.push_back(region_type::right_open(last, z.first.lower()));
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

                for(const auto& z: candidate.second) {
                    std::size_t nzones = z.second.size();
                    if(nzones == 2) {
                        std::cout << "Issue" << std::endl;
                    }
                }
            }
        } while(!replacements.empty());

        return regions;
    }

    region_map fixture_word_boundary(const region_map &regions) const;
    
public:
    std::ostream& print_regions(const region_map& regions, std::ostream& stream);
    std::ostream& print_regions_string(const region_map& regions, std::ostream& stream);
    static std::ostream& print_interval_set(const interval_set& interval, const std::string& str, std::ostream& stream);
    static std::ostream& print_interval_set(const interval_set& interval, const prova::loga::tokenized& ref, std::ostream& stream);
};

std::ostream& operator<<(std::ostream& stream, const prova::loga::tokenized_multi_alignment::matched_val& mval);
std::ostream& operator<<(std::ostream& stream, const std::set<prova::loga::tokenized_multi_alignment::matched_val>& set);

} // namespace algorithms
} // namespace prova

#endif // PROVA_ALIGN_TOKENIZED_MULTI_ALIGNMENT_H
