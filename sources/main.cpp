#include <loga/token.h>
#include <loga/collection.h>
#include <loga/tokenized_collection.h>
#include <loga/tokenized_alignment.h>
#include <loga/alignment.h>
#include <loga/tokenized_distance.h>
#include <loga/cluster.h>
#include <loga/path.h>
#include <loga/tokenized_group.h>
#include <loga/group.h>
#include <loga/tokenized_multi_alignment.h>
#include <loga/multi_alignment.h>
#include <iostream>
#include <fstream>
#include <span>
#include <filesystem>
#include <cereal/archives/portable_binary.hpp>
#include <igraph/igraph.h>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <loga/graph.h>
#include <boost/graph/connected_components.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/copy.hpp>
#include <boost/algorithm/string/join.hpp>
#include <loga/community.h>
#include <loga/automata.h>
#include <loga/pattern_sequence.h>
#include <loga/outliers.h>

class parsed{
    std::size_t _id;
    std::size_t _cluster;
    prova::loga::tokenized_multi_alignment::interval_set _intervals;

public:
    inline explicit parsed(std::size_t id, std::size_t cluster, prova::loga::tokenized_multi_alignment::interval_set&& intervals): _id(id), _cluster(cluster), _intervals(std::move(intervals)) {}
    const prova::loga::tokenized_multi_alignment::interval_set& intervals() const { return _intervals; }
    std::size_t id() const { return _id; }
    std::size_t cluster() const { return _cluster; }
};

template <typename WeightT>
struct intercluster_graph{
    using weight_type = std::conditional_t<std::is_integral_v<WeightT>, std::size_t, double>;
    using matrix_type = arma::Mat<weight_type>;

    struct vertex_property{
        std::size_t id;
    };

    struct edge_property{
        weight_type weight;
    };

    using graph_type   = boost::adjacency_list<boost::setS, boost::vecS, boost::bidirectionalS, vertex_property, edge_property>;
    using vertex_type  = boost::graph_traits<graph_type>::vertex_descriptor;
    using edge_type    = boost::graph_traits<graph_type>::edge_descriptor;

    graph_type _graph;
    std::vector<vertex_type> _vertices;

    intercluster_graph(std::size_t N){
        _vertices.resize(N);
        for(std::size_t i = 0; i != N; ++i){
            vertex_type v = boost::add_vertex(_graph);
            _graph[v].id = i;
            _vertices[i] = v;
        }
    }

    const graph_type& graph() const { return _graph; }

    void update(const arma::dmat& coverage, const arma::imat& captures, std::size_t K, bool soft) {
        assert(coverage.n_rows == coverage.n_cols);
        assert(captures.n_rows == captures.n_cols);
        std::size_t N = coverage.n_rows;

        for(std::size_t i = 0; i != N; ++i){
            std::vector<std::pair<std::size_t, WeightT>> weights;
            for(std::size_t j = 0; j != N; ++j){
                if(i == j) continue;

                auto w = coverage(i, j) /** captures(i, j)*/;
                weights.push_back(std::make_pair(j, w));
            }
            std::sort(weights.begin(), weights.end(), [](const std::pair<std::size_t, WeightT>& l, const std::pair<std::size_t, WeightT>& r){
                return l.second > r.second;
            });
            // if !soft then neighbourhood is limited to size k which implies that degree of a vertex may
            //     eventually be much less than k due to presence of a reverse edge.
            // if soft then neighbourhood is unrestricted at first to enforce consistent neighbourhood size
            //     the k is enforced as a break condition
            const auto neighbourhood = std::span(weights.cbegin(), soft ? N-1 : std::min(K, N - 1));
            std::size_t counter = 0;
            for(const auto& [j, w]: neighbourhood) {
                vertex_type u = _vertices[i];
                vertex_type v = _vertices[j];

                if(w > 0) {
                    auto [e, inserted] = boost::add_edge(v, u, _graph);
                    if (inserted) {
                        _graph[e].weight = w;
                        ++counter;
                    } else {
                        _graph[e].weight = std::min(_graph[e].weight, w);
                    }
                }

                if(soft && counter >= K) {
                    break;
                }
            }
        }
    }
};

template <typename Stream>
Stream& print_interval_set(Stream& stream, const prova::loga::tokenized_multi_alignment::interval_set& base_zones, const prova::loga::tokenized& base){
    std::size_t placeholder_count = 0;
    for(const auto& z: base_zones) {
        prova::loga::zone tag = *z.second.cbegin(); // set has only one item
        std::size_t offset = z.first.lower();
        std::size_t len = z.first.upper()-z.first.lower();
        std::string substr = base.subset(offset, len).view();
        if(tag == prova::loga::zone::constant)
            std::cout << substr;
        else {
            const auto& color = prova::loga::colors::palette.at(placeholder_count % prova::loga::colors::palette.size());
            std::cout << color << std::format("${}", placeholder_count) << prova::loga::colors::reset;
            ++placeholder_count;
        }
    }
    return stream;
}

prova::loga::community_detection_algorithm parse_algo(const std::string& name) {
    if (name == "leiden")      return prova::loga::community_detection_algorithm::leiden;
    if (name == "louvain")     return prova::loga::community_detection_algorithm::louvain;
    if (name == "components")  return prova::loga::community_detection_algorithm::weakly_connected;

    throw boost::program_options::validation_error(boost::program_options::validation_error::invalid_option_value, "cluster/refine-algo", name);
}

int main(int argc, char** argv) {
    boost::program_options::variables_map vm;
    try{
        boost::program_options::options_description desc("Allowed options");
        desc.add_options()
            ("help,h",            "Print help message")
            ("input",             boost::program_options::value<std::string>(),  "Log file path")
            ("cluster-algo,C",    boost::program_options::value<std::string>()->default_value("leiden"),     "Phase-1 clustering algorithm (leiden|louvain|components)")
            ("refine-algo,R",     boost::program_options::value<std::string>()->default_value("components"), "Phase-2 clustering algorithm (leiden|louvain|components)")
            ("threshold,T",       boost::program_options::value<double>()->default_value(0.9, "0.9"),        "threshold for minimum consensus in Phase-2")
            ("outlier,L",         boost::program_options::value<double>()->default_value(1.5, "1.5"),        "threshold used for outlier detection")
        ;
        boost::program_options::positional_options_description p;
        p.add("input", 1);

        auto options = boost::program_options::command_line_parser(argc, argv).options(desc).positional(p).run();
        boost::program_options::store(options, vm);
        boost::program_options::notify(vm);

        if (vm.count("help")) {
            std::cout << "Usage: " << argv[0] << " <path>\n";
            std::cout << desc << "\n";
            return 0;
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    if (!vm.count("input")) {
        throw boost::program_options::error("missing required positional argument <path>");
    }

    std::filesystem::path log_path = vm["input"].as<std::string>();

    std::string log_name  = log_path.filename().string();
    std::string base_name = std::filesystem::path(log_name).filename().string()+"_d";
    std::filesystem::path output_dir(base_name);
    {
        std::error_code ec;
        if (!std::filesystem::exists(output_dir) && !std::filesystem::create_directories(output_dir, ec)) {
            throw std::runtime_error( "Failed to create output directory '" + output_dir.string() + "': " + ec.message());
        }
    }

    std::string cli_p1_algo         = vm["cluster-algo"].as<std::string>();
    std::string cli_p2_algo         = vm["refine-algo"] .as<std::string>();
    double      cli_p2_threshold    = vm["threshold"]   .as<double>();
    double      cli_p1_outlier      = vm["outlier"]     .as<double>();

    std::filesystem::path archive_file_path        = output_dir / std::format("{}.1g.paths",           log_name);
    std::filesystem::path dist_file_path           = output_dir / std::format("{}.lev.dmat",           log_name);
    std::filesystem::path graphml_file_path        = output_dir / std::format("{}.1nn.graphml",        log_name);
    std::filesystem::path labels_file_path         = output_dir / std::format("{}.labels",             log_name);
    std::filesystem::path automata_dot_file_path   = output_dir / std::format("{}.automata.dot",       log_name);
    std::filesystem::path components_file_path     = output_dir / std::format("{}.components",         log_name);
    std::filesystem::path phase2_graphml_file_path = output_dir / std::format("{}.components.graphml", log_name);

    prova::loga::tokenized_collection collection;
    std::ifstream log(log_path);
    collection.parse(log);

    prova::loga::tokenized_distance distance(collection.count());
    if(std::filesystem::exists(dist_file_path)){
        std::ifstream dist_file(dist_file_path, std::ios::binary);
        if(!distance.load(dist_file)){
            std::cout << "failed to load dmat from " << dist_file_path << std::endl;
            return 1;
        }
    } else {
        distance.compute(collection);
        std::ofstream dist_file(dist_file_path, std::ios::binary);
        if(!distance.save(dist_file)){
            std::cout << "failed to save dmat to " << dist_file_path << std::endl;
            return 1;
        }
    }
    arma::mat dmat = distance.matrix();
    assert(dmat.n_rows == collection.count());
    assert(dmat.n_cols == collection.count());

    auto bgl_graph = distance.knn_graph(1, false);
    {
        std::ofstream graphml(graphml_file_path);
        prova::loga::print_graphml(bgl_graph, graphml);
    }
    arma::Row<std::size_t> labels(collection.count());
    if(std::filesystem::exists(labels_file_path)){
        std::ifstream labels_file(labels_file_path, std::ios::binary);
        if(!labels.load(labels_file)){
            std::cout << "failed to load labels" << std::endl;
            return 1;
        }
    } else {
        std::map<prova::loga::tokenized_distance::unvertex_type, std::size_t> vertex_cluster_map;
        prova::loga::community_detection_algorithm algo_phase1 = parse_algo(cli_p1_algo);
        std::size_t num_clusters = prova::loga::detect_communities(bgl_graph, algo_phase1, vertex_cluster_map);
        for (auto ve = vertices(bgl_graph); ve.first != ve.second; ++ve.first) {
            prova::loga::tokenized_distance::unvertex_type v = *ve.first;
            const auto& vp = bgl_graph[v];
            labels[vp.id] = vertex_cluster_map.at(v);
        }
        std::ofstream labels_file(labels_file_path, std::ios::binary);
        if(!labels.save(labels_file)){
            std::cout << "failed to save labels" << std::endl;
            return 1;
        }
        {
            std::ofstream labels_txt(labels_file_path.string() + ".txt");
            for (arma::uword i = 0; i < labels.n_elem; ++i)
                labels_txt << labels(i) << "\n";
        }
    }

    bool components_detected = false;
    if(std::filesystem::exists(components_file_path)){
        arma::Row<std::size_t> components(collection.count());
        std::ifstream components_file(components_file_path, std::ios::binary);
        if(!components.load(components_file)){
            std::cout << "failed to load components" << std::endl;
            return 1;
        } else {
            components_detected = true;
            labels = components;
            std::cout << "Loaded components" << std::endl;
        }
    }

    prova::loga::tokenized_alignment::matrix_type all_paths;
    std::map<std::size_t, parsed> patterns;
    prova::loga::tokenized_group group(collection, labels);
    std::cout << "Clusters: " << group.labels() << std::endl;
    std::map<std::size_t, prova::loga::tokenized_multi_alignment::interval_set> cluster_patterns;
    std::map<std::size_t, prova::loga::tokenized> cluster_samples;
    for(std::size_t c = 0; /*c != group.labels()*/; ++c) {
        if(c == group.labels()) {
            if(group.unclustered() == 0){
                break;
            } else {
                prova::loga::tokenized_group::label_proxy proxy = group.proxy(std::numeric_limits<std::size_t>::max());
                std::cout << std::format("Unlabeled: ({})", proxy.count()) << std::endl;
                for(std::size_t i = 0; i != proxy.count(); ++i) {
                    auto v = proxy.at(i);
                    std::cout << v.str() << std::endl;
                }
                break;
            }
        }
        prova::loga::tokenized_group::label_proxy proxy = group.proxy(c);
        std::size_t count = proxy.count();

        std::string cluster_name = std::format("C{}", c);
        std::cout << std::endl << prova::loga::colors::bright_yellow << "◪" << prova::loga::colors::reset << " Label: " << cluster_name << std::format(" ({})", count) << std::endl;

        prova::loga::tokenized_collection subcollection;
        std::vector<std::size_t> references;                                    // global ids of all items belonging to the same cluster
        for(std::size_t i = 0; i < count; ++i) {
            prova::loga::tokenized_group::label_proxy::value v = proxy.at(i);   // v: {str, id} where the id is the global id and the str is the string (not token list) value of the item
            const std::string& str = v.str();
            subcollection.add(str);
            references.push_back(v.id());
        }

        std::vector<std::string> outliers;
        std::vector<double> confidence;
        arma::vec lof(count, arma::fill::none);
        if(count > 2){
            lof = prova::loga::outliers::lof(subcollection);

            std::vector<std::size_t> excluded;
            for (arma::uword i = 0; i < count; ++i) {
                if(lof(i) >= cli_p1_outlier){
                    excluded.push_back(i);
                }
                confidence.push_back(lof(i));
            }

            for(auto it = excluded.rbegin(); it != excluded.rend(); ++it) {
                std::size_t index = *it;
                outliers.push_back(subcollection.at(index).raw());
                subcollection.remove(index);
                references.erase(references.begin() + index);
                confidence.erase(confidence.begin() + index);
            }
        }

        std::size_t base = 0;
        std::size_t cache_hits = 0;
        prova::loga::tokenized_alignment::matrix_type paths;
        std::size_t ref_c_i = 0;
        for(auto ref_i = references.cbegin(); ref_i != references.cend(); ++ref_i) {
            std::size_t ref_c_j = 0;
            for(auto ref_j = references.cbegin(); ref_j != references.cend(); ++ref_j) {
                if(ref_i != ref_j){
                    auto global_key = std::make_pair(*ref_i, *ref_j);
                    auto it = all_paths.find(global_key);
                    if(it != all_paths.cend()) {
                        if(ref_c_i == base) {
                            auto local_key = std::make_pair(ref_c_i, ref_c_j);
                            paths.insert(std::make_pair(local_key, it->second));
                            ++cache_hits;
                        }
                    }
                }
                ++ref_c_j;
            }
            ++ref_c_i;
        }
        std::cout << std::format("Cache hits {}/{}", cache_hits, references.size()-1) << std::endl;

        prova::loga::tokenized_alignment subalignment(subcollection);
        subalignment.bubble_all_pairwise(paths, subcollection.begin(), 1, false);

        for(const auto& [key, path]: paths) {
            auto global_key = std::make_pair(references.at(key.first), references.at(key.second));
            if(!all_paths.contains(global_key)) {
                all_paths.insert(std::make_pair(global_key, path));
            }
        }

        prova::loga::tokenized_multi_alignment malign(subcollection, paths, base);
        prova::loga::tokenized_multi_alignment::region_map regions = malign.align(cli_p2_threshold);
        const auto& base_zones = regions.at(0);
        std::size_t placeholder_count = 0;
        std::cout << std::right << std::setw(5) << "     " << prova::loga::colors::bright_yellow << "●" << prova::loga::colors::reset << " " << std::resetiosflags(std::ios::showbase);
        for(const auto& z: base_zones) {
            prova::loga::zone tag = *z.second.cbegin(); // set has only one item
            std::size_t offset = z.first.lower();
            std::size_t len = z.first.upper()-z.first.lower();
            std::string substr = subcollection.at(0).subset(offset, len).view();
            if(tag == prova::loga::zone::constant)
                std::cout << substr;
            else {
                const auto& color = prova::loga::colors::palette.at(placeholder_count % prova::loga::colors::palette.size());
                std::cout << color << std::format("${}", placeholder_count) << prova::loga::colors::reset;
                ++placeholder_count;
            }
        }
        std::cout << prova::loga::colors::reset;
        std::cout << std::endl << "------------------------------------------" << std::endl;
        for(const auto& [id, zones]: regions) {
            std::cout << std::right << std::setw(5) << id;
            if(count > 2) {
                std::cout << ":" << std::fixed << std::setprecision(2) << std::abs(confidence.at(id) - 1.0f);
            }
            std::cout << "|" << std::resetiosflags(std::ios::showbase);
            prova::loga::tokenized_multi_alignment::print_interval_set(zones, subcollection.at(id), std::cout);
            std::cout << std::endl;
        }

        if(outliers.size() > 0){
            std::cout << prova::loga::colors::bright_red << "    Excluded " << outliers.size() /*<< std::fixed << std::setprecision(2) << lof.t() << prova::loga::colors::reset*/ << std::endl;
            for(const std::string& outlier: outliers){
                std::cout << prova::loga::colors::bright_red << "    X| " << prova::loga::colors::reset << outlier << std::endl;
            }
            std::cout << std::endl;
        }

        auto pattern_zones = base_zones;
        parsed cluster_pattern(references.at(0), c, std::move(pattern_zones));
        patterns.insert(std::make_pair(c, cluster_pattern));

        cluster_patterns.insert(std::make_pair(c, base_zones));
        cluster_samples.insert(std::make_pair(c, subcollection.at(0)));
    }

    if(!components_detected){
        std::ofstream archive_file(archive_file_path, std::ios::binary);
        cereal::PortableBinaryOutputArchive archive(archive_file);
        archive(all_paths);
    }

    std::cout << "----------------------------" << std::endl;
    std::cout << "Summary: " << std::format("{} clusters", group.labels()) << std::endl;
    std::cout << "----------------------------" << std::endl;

    std::size_t total_segments = 0;
    std::vector<prova::loga::pattern_sequence> pseqs;
    for(const auto& [c, p]: patterns) {
        std::string cluster_name = std::format("C{}", c);
        const auto& base_zones = p.intervals();
        std::size_t placeholder_count = 0;
        std::cout << std::setw(4) << cluster_name << " " << prova::loga::colors::bright_yellow << "●" << prova::loga::colors::reset << " " << std::resetiosflags(std::ios::showbase);
        prova::loga::pattern_sequence pseq;
        for(const auto& z: base_zones) {
            prova::loga::zone tag   = *z.second.cbegin(); // set has only one item
            std::size_t offset      = z.first.lower();
            std::size_t len         = z.first.upper()-z.first.lower();
            std::string substr      = collection.at(p.id()).subset(offset, len).view();

            prova::loga::pattern_sequence::segment seg(tag);
            if(tag == prova::loga::zone::constant){
                std::cout << substr;
                seg = substr;
                ++total_segments;
            } else {
                const auto& color = prova::loga::colors::palette.at(placeholder_count % prova::loga::colors::palette.size());
                std::cout << color << std::format("${}", placeholder_count) << prova::loga::colors::reset;
                ++placeholder_count;
            }
            pseq.add(std::move(seg));
        }
        std::cout << std::endl;
        pseqs.emplace_back(std::move(pseq));
    }

    if(components_detected)
        return 0;

    prova::loga::automata automata(pseqs.cbegin(), pseqs.cend());
    automata.build();
    arma::dmat coverage;
    arma::imat capture;
    automata.generialize(coverage, capture, false);
    std::ofstream astream(automata_dot_file_path, std::ios::binary);
    automata.graphviz(astream);
    using intercluster_graph_type = intercluster_graph<std::size_t>;
    intercluster_graph_type intercluster(pseqs.size());
    intercluster.update(coverage, capture, 1, false);
    {
        auto cluster_graph = intercluster.graph();
        std::ofstream graphml_knn(phase2_graphml_file_path, std::ios::binary);
        prova::loga::print_graphml(cluster_graph, graphml_knn);
    }
    std::vector<int> comp;
    intercluster_graph_type::graph_type digraph = intercluster.graph();

    std::map<intercluster_graph_type::vertex_type, std::size_t> vertex_components_map;
    prova::loga::community_detection_algorithm algo_phase2 = parse_algo(cli_p2_algo);
    std::size_t num_components = prova::loga::detect_communities(digraph, algo_phase2, vertex_components_map);

    std::multimap<std::size_t, intercluster_graph_type::vertex_type> components_vertex_map;
    for(const auto& [v, c]: vertex_components_map) {
        components_vertex_map.insert(std::make_pair(c, v));
    }

    std::cout << "----------------------------" << std::endl;
    std::cout << "Summary: " << std::format("{} components", num_components) << std::endl;
    std::cout << "----------------------------" << std::endl;

    std::map<std::size_t, prova::loga::pattern_sequence> component_patterns;
    std::set<std::size_t> outliers;
    for (auto it = components_vertex_map.cbegin(); it != components_vertex_map.cend(); ) {
        int component_id = it->first;

        auto range = components_vertex_map.equal_range(component_id);
        std::size_t count = std::distance(range.first, range.second);

        if (count == 1) {
            // single-vertex component
            std::size_t vi = range.first->second;
            auto v = boost::vertex(vi, digraph);
            std::size_t cluster = digraph[v].id;
            outliers.insert(cluster);

            it = range.second;
            continue;
        }

        // collect vertex indices of this component
        std::vector<intercluster_graph_type::vertex_type> verts;
        verts.reserve(count);
        for (auto jt = range.first; jt != range.second; ++jt) {
            verts.push_back(jt->second);                                         // vertex index
        }

        std::sort(verts.begin(), verts.end(), [&](intercluster_graph_type::vertex_type u, intercluster_graph_type::vertex_type v) {
                                                      return boost::in_degree(u, digraph) > boost::in_degree(v, digraph);
                                                  });
        std::set<std::size_t> ref_members;
        for (std::size_t vi : verts) {
            auto v = boost::vertex(vi, digraph);
            std::size_t cluster = digraph[v].id;                // recover cluster id
            ref_members.insert(cluster);
        }


        // now iterate in sorted order
        std::size_t first_i = verts.at(0);
        auto first_v = boost::vertex(first_i, digraph);
        std::size_t first_c = digraph[first_v].id;
        prova::loga::pattern_sequence merged = automata.merge(first_c, ref_members);
        component_patterns.insert(std::make_pair(component_id, merged));
        std::cout << prova::loga::colors::bright_yellow << "◈" << prova::loga::colors::reset << " T" << component_id << " " << merged;
        std::cout << std::endl;
        std::cout << "-----------------------------";

        {
            std::set<std::size_t> members = ref_members;
            members.insert(first_c);
            auto component_subgraph = output_dir / std::format("{}.component.{}.automata.dot", log_name, first_c);
            std::ofstream component_subgraph_stream(component_subgraph, std::ios::binary);
            prova::loga::automata::thompson_digraph_type subgraph;
            automata.subgraph(ref_members, subgraph);
            prova::loga::automata::graphviz(component_subgraph_stream, subgraph);
        }

        std::cout << std::endl;
        for (std::size_t vi : verts) {
            auto v = boost::vertex(vi, digraph);
            std::size_t cluster = digraph[v].id;                // recover cluster id
            const prova::loga::pattern_sequence& pat = pseqs.at(cluster);
            std::cout << "    " << std::setw(4) << cluster << ": " << pat;
            std::cout << std::endl;
        }

        it = range.second;
        std::cout << std::endl;
    }

    std::cout << "Components list: " << std::endl;
    for(const auto& [component_id, pat]: component_patterns) {
        std::cout << prova::loga::colors::bright_yellow << "◈" << prova::loga::colors::reset << " T" << component_id << " " << pat << std::endl;
    }

    std::cout << std::format("Singletons: ({})", outliers.size()) << std::endl;
    for(std::size_t cluster: outliers) {
        std::cout << "    " << std::setw(4) << cluster << ": ";
        const auto& pat    = cluster_patterns.at(cluster);
        const auto& sample = cluster_samples.at(cluster);
        print_interval_set(std::cout, pat, sample);
        std::cout << std::endl;

        auto component_subgraph = output_dir / std::format("{}.component.{}.automata.dot", log_name, cluster);
        std::ofstream component_subgraph_stream(component_subgraph, std::ios::binary);
        prova::loga::automata::thompson_digraph_type subgraph;
        automata.subgraph(cluster, subgraph);
        prova::loga::automata::graphviz(component_subgraph_stream, subgraph);


        // automata.directional_subset_generialize();
    }

    prova::loga::cluster::labels_type components(collection.count());
    for (auto it = components_vertex_map.cbegin(); it != components_vertex_map.cend(); ) {
        int component_id = it->first;
        auto range = components_vertex_map.equal_range(component_id);
        for (auto jt = range.first; jt != range.second; ++jt) {
            std::size_t cluster = jt->second;
            prova::loga::tokenized_group::label_proxy proxy = group.proxy(cluster);
            std::size_t count = proxy.count();                     // global ids of all items belonging to the same cluster
            for(std::size_t i = 0; i < count; ++i) {
                prova::loga::tokenized_group::label_proxy::value v = proxy.at(i);   // v: {str, id} where the id is the global id and the str is the string (not token list) value of the item
                std::size_t global_id = v.id();
                components[global_id] = component_id;
            }
        }
        it = range.second;
    }
    std::ofstream components_file(components_file_path, std::ios::binary);
    if(!components.save(components_file)){
        std::cout << "failed to save components" << std::endl;
        return 1;
    }
    {
        std::ofstream components_txt(components_file_path.string() + ".txt");
        for (arma::uword i = 0; i < components.n_elem; ++i)
            components_txt << components(i) << "\n";
    }

    std::cout << "Plase 1 completed run loga again for phase 2" << std::endl;
    return 0;
}
