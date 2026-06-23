#include <benchmark/benchmark.h>
#include <dartfrog.hpp>
#include <datalog/predicate.hpp>
#include <random>

using namespace df::datalog;

// Random directed graph: each node gets k random out-edges.
// Strongly connected with high probability for k >= 2, so TC ≈ N^2.
static std::vector<std::array<int, 2>>
make_random_kout(int n, int k, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pick(0, n - 1);
    std::vector<std::array<int, 2>> edges;
    edges.reserve(n * k);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < k; ++j) {
            int t = pick(rng);
            if (t != i) edges.push_back({i, t});
        }
    return edges;
}

// DAG with bounded-reach: each node has k forward edges within a window W.
// TC size is O(N * W) rather than O(N^2), making large N tractable.
static std::vector<std::array<int, 2>>
make_windowed_dag(int n, int k, int w, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::vector<std::array<int, 2>> edges;
    edges.reserve(n * k);
    for (int i = 0; i < n; ++i) {
        int hi = std::min(n - 1, i + w);
        if (hi <= i) continue;
        std::uniform_int_distribution<int> pick(i + 1, hi);
        for (int j = 0; j < k && i + 1 <= hi; ++j)
            edges.push_back({i, pick(rng)});
    }
    return edges;
}

static df::Relation<std::array<int, 2>>
to_relation(std::vector<std::array<int, 2>> edges) {
    return df::Relation<std::array<int, 2>>::from_vec(std::move(edges));
}

static size_t run_tc(const df::Relation<std::array<int, 2>> &graph) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> tc(dl);
    Var<0> x; Var<1> y; Var<2> z;
    dl.add_rule(tc(x, y) <<= edge(x, y));
    dl.add_rule(tc(x, z) <<= tc(x, y) && edge(y, z));
    edge.insert(graph);
    dl.solve();
    return tc.extract().size();
}

// Dense random (density=0.1) — baseline from before.
static void BM_TC_Dense(benchmark::State &state) {
    const int n = state.range(0);
    std::mt19937_64 rng(42);
    std::bernoulli_distribution keep(0.1);
    std::vector<std::array<int, 2>> edges;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j && keep(rng)) edges.push_back({i, j});
    auto graph = to_relation(std::move(edges));

    size_t tc_size = 0;
    for (auto _ : state) tc_size = run_tc(graph);
    state.counters["edges"]   = (double)graph.size();
    state.counters["tc_size"] = (double)tc_size;
}

// Sparse k-out random graph (k=5). TC ≈ N^2 once strongly connected.
static void BM_TC_Sparse(benchmark::State &state) {
    const int n = state.range(0);
    auto graph = to_relation(make_random_kout(n, 5));

    size_t tc_size = 0;
    for (auto _ : state) tc_size = run_tc(graph);
    state.counters["edges"]   = (double)graph.size();
    state.counters["tc_size"] = (double)tc_size;
}

// Windowed DAG (k=5, W=100): each node reaches at most W successors.
// TC size ≈ N*W — scales linearly in N.
static void BM_TC_Windowed(benchmark::State &state) {
    const int n = state.range(0);
    auto graph = to_relation(make_windowed_dag(n, 5, 100));

    size_t tc_size = 0;
    for (auto _ : state) tc_size = run_tc(graph);
    state.counters["edges"]   = (double)graph.size();
    state.counters["tc_size"] = (double)tc_size;
}

// Magic-sets TC: computes tc(x,y) restricted to demanded x values.
// Equivalent to full TC for the queried tuples, not just reachability.
//
// magic_tc(start)
// tc(x,y) :- magic_tc(x), edge(x,y)
// tc(x,z) :- magic_tc(x), tc(x,y), edge(y,z)
//
// For adornment bf, magic_tc stays {start} (no propagation rule needed),
// so only tc(start,y) tuples are derived — same answer as full TC for that query.
static size_t run_tc_magic(const df::Relation<std::array<int, 2>> &graph, int start) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> tc(dl);
    Predicate<int, 1> magic_tc(dl);
    Var<0> x; Var<1> y; Var<2> z;
    dl.add_rule(tc(x, y) <<= magic_tc(x) && edge(x, y));
    dl.add_rule(tc(x, z) <<= magic_tc(x) && tc(x, y) && edge(y, z));
    edge.insert(graph);
    magic_tc.insert(df::Relation<std::array<int, 1>>::from_vec({{{start}}}));
    dl.solve();
    return tc.extract().size();
}

static void BM_TCMagic_Windowed(benchmark::State &state) {
    const int n = state.range(0);
    auto graph = to_relation(make_windowed_dag(n, 5, 100));

    size_t tc_size = 0;
    for (auto _ : state) tc_size = run_tc_magic(graph, 0);
    state.counters["edges"]   = (double)graph.size();
    state.counters["tc_size"] = (double)tc_size;
}

static void BM_TCMagic_Sparse(benchmark::State &state) {
    const int n = state.range(0);
    auto graph = to_relation(make_random_kout(n, 5));

    size_t tc_size = 0;
    for (auto _ : state) tc_size = run_tc_magic(graph, 0);
    state.counters["edges"]   = (double)graph.size();
    state.counters["tc_size"] = (double)tc_size;
}

BENCHMARK(BM_TC_Dense)
    ->Arg(200)->Arg(500)->Arg(1000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_TC_Sparse)
    ->Arg(200)->Arg(500)->Arg(1000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_TC_Windowed)
    ->Arg(1000)->Arg(5000)->Arg(10000)
    ->Unit(benchmark::kMillisecond);

// Magic-sets TC: sizes full TC can't handle
BENCHMARK(BM_TCMagic_Windowed)
    ->Arg(10000)->Arg(100000)->Arg(1000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_TCMagic_Sparse)
    ->Arg(10000)->Arg(100000)->Arg(1000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
