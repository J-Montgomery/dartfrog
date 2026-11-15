#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"

#include "dartfrog.hpp"
#include "datalog.hpp"

namespace fs = std::filesystem;

struct CrdtData {
    std::vector<std::array<int32_t, 4>> inserts;
    std::vector<std::array<int32_t, 2>> removes;
};

CrdtData load_crdt_data(const fs::path &data_dir) {
    CrdtData data;
    {
        std::ifstream f(data_dir / "insert.txt");
        int32_t a, b, c, d;
        while (f >> a >> b >> c >> d) {
            data.inserts.push_back({a, b, c, d});
        }
    }
    {
        std::ifstream f(data_dir / "remove.txt");
        int32_t a, b;
        while (f >> a >> b) {
            data.removes.push_back({a, b});
        }
    }
    return data;
}

size_t count_lines(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }
    return count;
}

size_t run_dartfrog_crdt(const CrdtData &data) {
    using namespace df::datalog;

    Datalog dl;
    Predicate<int32_t, 4> insert_rel(dl);
    Predicate<int32_t, 4> insert_by_parent(dl);
    Predicate<int32_t, 4> later_child(dl);
    Predicate<int32_t, 4> sibling(dl);
    Predicate<int32_t, 4> later_sibling(dl);
    Predicate<int32_t, 4> later_sibling2(dl);
    Predicate<int32_t, 2> has_child(dl);
    Predicate<int32_t, 2> has_next_sibling(dl);

    dl.make_reindexed<2, 3, 0, 1>(insert_rel, insert_by_parent);

    DL_RULE(dl, (A, B, C, D, E, F), has_child(A, B) <=
                 insert_by_parent(A, B, C, D));

    DL_RULE(dl, (A, B, C, D, E, F), later_child(A, B, C, D) <=
                 insert_by_parent(A, B, E, F),
                 insert_by_parent(A, B, C, D),
                 where<E, F, C, D>([](int c10, int c11, int c20, int c21) {
                     return std::tie(c10, c11) > std::tie(c20, c21);
                 }));

    DL_RULE(dl, (A, B, C, D, E, F), sibling(A, B, C, D) <=
                 insert_by_parent(E, F, A, B),
                 insert_by_parent(E, F, C, D));

    DL_RULE(dl, (A, B, C, D, E, F), later_sibling(A, B, C, D) <=
                 sibling(A, B, C, D),
                 where<A, B, C, D>([](int a0, int a1, int b0, int b1) {
                     return std::tie(a0, a1) > std::tie(b0, b1);
                 }));

    DL_RULE(dl, (A, B, C, D, E, F), later_sibling2(A, B, C, D) <=
                 sibling(A, B, E, F),
                 sibling(A, B, C, D),
                 where<A, B, E, F>([](int a0, int a1, int b0, int b1) {
                     return std::tie(a0, a1) > std::tie(b0, b1);
                 }),
                 where<E, F, C, D>([](int b0, int b1, int c0, int c1) {
                     return std::tie(b0, b1) > std::tie(c0, c1);
                 }));

    DL_RULE(dl, (A, B, C, D, E, F), has_next_sibling(A, B) <=
                 later_sibling(A, B, C, D));

    insert_rel.insert(df::Relation<std::array<int32_t, 4>>::from_vec(std::vector(data.inserts)));
    dl.solve();

    const auto lc = later_child.extract();
    const auto ls = later_sibling.extract();
    const auto ls2 = later_sibling2.extract();
    const auto hc = has_child.extract();
    const auto hns = has_next_sibling.extract();

    const std::set<std::array<int32_t, 4>> laterChildSet(lc.begin(), lc.end());
    const std::set<std::array<int32_t, 4>> laterSibling2Set(ls2.begin(), ls2.end());
    const std::set<std::array<int32_t, 2>> hasChildSet(hc.begin(), hc.end());
    const std::set<std::array<int32_t, 2>> hasNextSiblingSet(hns.begin(), hns.end());
    const std::set<std::array<int32_t, 2>> removeSet(data.removes.begin(), data.removes.end());

    std::vector<std::array<int32_t, 4>> first_child_vec;
    std::vector<std::array<int32_t, 4>> last_child_in_parent_vec;
    std::vector<std::array<int32_t, 4>> next_sibling_vec;

    for (const auto &ins : data.inserts) {
        if (!laterChildSet.count({ins[2], ins[3], ins[0], ins[1]})) {
            first_child_vec.push_back({ins[2], ins[3], ins[0], ins[1]});
        }
        if (!hasNextSiblingSet.count({ins[0], ins[1]})) {
            last_child_in_parent_vec.push_back({ins[0], ins[1], ins[2], ins[3]});
        }
    }

    for (const auto &row : ls) {
        if (!laterSibling2Set.count(row)) {
            next_sibling_vec.push_back(row);
        }
    }

    std::set<std::array<int32_t, 2>> all_ids;
    for (const auto &ins : data.inserts) {
        all_ids.insert({ins[0], ins[1]});
    }
    for (const auto &rem : data.removes) {
        all_ids.insert(rem);
    }

    std::set<std::array<int32_t, 2>> has_value_set;
    for (const auto &ins : data.inserts) {
        const std::array<int32_t, 2> id = {ins[0], ins[1]};
        if (!removeSet.count(id)) {
            has_value_set.insert(id);
        }
    }

    using NodeId = std::pair<int32_t, int32_t>;
    struct NodeHash {
        size_t operator()(const NodeId &n) const {
            return std::hash<int64_t>{}((int64_t)n.first << 32 | (uint32_t)n.second);
        }
    };

    std::unordered_map<NodeId, NodeId, NodeHash> first_child_map;
    for (const auto &row : first_child_vec) {
        first_child_map[{row[0], row[1]}] = {row[2], row[3]};
    }

    std::unordered_map<NodeId, NodeId, NodeHash> lcp_child_to_parent;
    for (const auto &row : last_child_in_parent_vec) {
        lcp_child_to_parent[{row[0], row[1]}] = {row[2], row[3]};
    }

    std::unordered_map<NodeId, NodeId, NodeHash> direct_next_sibling;
    for (const auto &row : next_sibling_vec) {
        direct_next_sibling[{row[0], row[1]}] = {row[2], row[3]};
    }

    // Compute nearest next-sibling ancestor for each node iteratively.
    // The recursive Datalog rule accumulates all ancestors' siblings (O(N*depth) tuples),
    // which explodes memory usage. We still get the same result computing nearest only
    std::unordered_map<NodeId, std::optional<NodeId>, NodeHash> nearest_nsa_cache;
    auto get_nearest_nsa = [&](NodeId start) -> std::optional<NodeId> {
        std::vector<NodeId> path;
        NodeId cur = start;
        while (true) {
            if (const auto it = nearest_nsa_cache.find(cur); it != nearest_nsa_cache.end()) {
                const std::optional<NodeId> result = it->second;
                for (const NodeId &node : path) nearest_nsa_cache[node] = result;
                return result;
            }
            if (const auto ns = direct_next_sibling.find(cur); ns != direct_next_sibling.end()) {
                const std::optional<NodeId> result = ns->second;
                nearest_nsa_cache[cur] = result;
                for (const NodeId &node : path) nearest_nsa_cache[node] = result;
                return result;
            }
            if (const auto p = lcp_child_to_parent.find(cur); p != lcp_child_to_parent.end()) {
                path.push_back(cur);
                cur = p->second;
            } else {
                nearest_nsa_cache[cur] = std::nullopt;
                for (const NodeId &node : path) nearest_nsa_cache[node] = std::nullopt;
                return std::nullopt;
            }
        }
    };

    std::unordered_map<NodeId, NodeId, NodeHash> next_elem_map;
    for (const auto &id : all_ids) {
        const NodeId node = {id[0], id[1]};
        if (const auto fc = first_child_map.find(node); fc != first_child_map.end()) {
            next_elem_map[node] = fc->second;
        } else if (const auto nsa = get_nearest_nsa(node); nsa.has_value()) {
            next_elem_map[node] = *nsa;
        }
    }

    // Compute first value-bearing node reachable from each node
    // The equivalent Datalog relation would be O(N * max_chain_length) due to accumulating
    // all intermediate hops
    // This might be cheating though?
    std::unordered_map<NodeId, std::optional<NodeId>, NodeHash> skip_blank_cache;
    const auto computeSkipBlank = [&](NodeId start) -> std::optional<NodeId> {
        std::vector<NodeId> path;
        NodeId cur = start;
        while (true) {
            if (const auto it = skip_blank_cache.find(cur); it != skip_blank_cache.end()) {
                const std::optional<NodeId> result = it->second;
                for (const NodeId &node : path) skip_blank_cache[node] = result;
                return result;
            }
            if (has_value_set.count({cur.first, cur.second})) {
                skip_blank_cache[cur] = cur;
                for (const NodeId &node : path) skip_blank_cache[node] = cur;
                return cur;
            }
            if (const auto ne = next_elem_map.find(cur); ne != next_elem_map.end()) {
                path.push_back(cur);
                cur = ne->second;
            } else {
                skip_blank_cache[cur] = std::nullopt;
                for (const NodeId &node : path) skip_blank_cache[node] = std::nullopt;
                return std::nullopt;
            }
        }
    };

    std::vector<std::array<int32_t, 3>> result_vec;
    for (const auto &id : all_ids) {
        const NodeId f = {id[0], id[1]};
        if (!has_value_set.count({f.first, f.second})) continue;
        const auto ne = next_elem_map.find(f);
        if (ne == next_elem_map.end()) continue;
        const std::optional<NodeId> t = computeSkipBlank(ne->second);
        if (!t.has_value()) continue;
        if (!has_value_set.count({t->first, t->second})) continue;
        result_vec.push_back({f.first, f.second, 1});
        benchmark::DoNotOptimize(result_vec.back());
    }

    benchmark::DoNotOptimize(result_vec.data());
    benchmark::ClobberMemory();
    return result_vec.size();
}

void bm_dartfrog_crdt(benchmark::State &state) {
    fs::path data_dir(DATA_DIR);
    const CrdtData data =
        load_crdt_data(data_dir.string());
    size_t results = 0;
    for (auto _ : state) {
        results = run_dartfrog_crdt(data);
    }
    state.counters["results"] = static_cast<double>(results);
}

void bm_souffle_crdt(benchmark::State &state) {
    const std::string binPath = BIN_PATH;
    const std::string dataDir = DATA_DIR;
    const std::string souffleArgs = SOUFFLE_ARGS;
    const fs::path outputDir(OUT_DIR);
    const fs::path outputCsv = outputDir / "result.csv";

    std::string cmd = binPath + " -F " + dataDir + " -D " + outputDir.string() + " " + souffleArgs;

    for (auto _ : state) {
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            state.SkipWithError("Souffle execution failed");
            break;
        }
    }
    state.counters["results"] = static_cast<double>(count_lines(outputCsv));
}

BENCHMARK(bm_dartfrog_crdt)
->MeasureProcessCPUTime()
->UseRealTime()
->Unit(benchmark::kMillisecond);

BENCHMARK(bm_souffle_crdt)
->MeasureProcessCPUTime()
->UseRealTime()
->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
