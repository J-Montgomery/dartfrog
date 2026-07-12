#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

#include "benchmark/benchmark.h"

#include "datalog.hpp"
#include "dartfrog.hpp"

using namespace df::datalog;
namespace fs = std::filesystem;

template <int32_t N>
std::vector<std::array<int32_t, N>> load_csv(const std::filesystem::path &path) {
    std::vector<std::array<int32_t, N>> rows;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::array<int32_t, N> row;
        std::istringstream ss(line);
        std::string token;
        for (int32_t i = 0; i < N; ++i) {
            std::getline(ss, token, ',');
            row[i] = std::stoi(token);
        }
        rows.push_back(row);
    }
    return rows;
}

size_t countLines(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }
    return count;
}

struct GalenData {
    std::vector<std::array<int32_t, 2>> p;
    std::vector<std::array<int32_t, 3>> q;
    std::vector<std::array<int32_t, 3>> r;
    std::vector<std::array<int32_t, 3>> c;
    std::vector<std::array<int32_t, 3>> u;
    std::vector<std::array<int32_t, 2>> s;
};

GalenData load_galen_data(const std::filesystem::path &data_dir) {
    GalenData data;
    data.p = load_csv<2>(data_dir / "p.txt");
    data.q = load_csv<3>(data_dir / "q.txt");
    data.r = load_csv<3>(data_dir / "r.txt");
    data.c = load_csv<3>(data_dir / "c.txt");
    data.u = load_csv<3>(data_dir / "u.txt");
    data.s = load_csv<2>(data_dir / "s.txt");
    return data;
}

std::array<size_t, 2> run_dartfrog_galen(const GalenData &data) {

    Datalog dl;
    Predicate<int32_t, 2> p_rel(dl);
    Predicate<int32_t, 2> p_by_z(dl);
    Predicate<int32_t, 3> q_rel(dl);
    Predicate<int32_t, 3> q_by_z(dl);
    Predicate<int32_t, 3> q_by_yr(dl);
    Predicate<int32_t, 3> q_by_r(dl);
    Predicate<int32_t, 3> r_rel(dl);
    Predicate<int32_t, 3> c_rel(dl);
    Predicate<int32_t, 3> u_rel(dl);
    Predicate<int32_t, 2> s_rel(dl);

    dl.make_reindexed<1, 0>(p_rel, p_by_z);
    dl.make_reindexed<2, 0, 1>(q_rel, q_by_z);
    dl.make_reindexed<2, 1, 0>(q_rel, q_by_yr);
    dl.make_reindexed<1, 0, 2>(q_rel, q_by_r);

    // p(X,Z) :- p(X,Y), p(Y,Z)
    dl.add_rule(p_rel(Var<0>{}, Var<2>{}) %=
                p_by_z(Var<1>{}, Var<0>{}) && p_rel(Var<1>{}, Var<2>{}));

    // q(X,R,Z) :- p(X,Y), q(Y,R,Z)
    dl.add_rule(q_rel(Var<0>{}, Var<2>{}, Var<3>{}) %=
                p_by_z(Var<1>{}, Var<0>{}) && q_rel(Var<1>{}, Var<2>{}, Var<3>{}));

    // p(X,Z) :- p(Y,W), u(W,R,Z), q(X,R,Y)
    dl.add_rule(p_rel(Var<4>{}, Var<3>{}) %= p_rel(Var<0>{}, Var<1>{}) &&
                                             u_rel(Var<1>{}, Var<2>{}, Var<3>{}) &&
                                             q_by_yr(Var<0>{}, Var<2>{}, Var<4>{}));

    // p(X,Z) :- c(Y,W,Z), p(X,W), p(X,Y)
    dl.add_rule(p_rel(Var<3>{}, Var<2>{}) %= c_rel(Var<0>{}, Var<1>{}, Var<2>{}) &&
                                             p_by_z(Var<1>{}, Var<3>{}) &&
                                             p_rel(Var<3>{}, Var<0>{}));

    // q(X,Q,Z) :- q(X,R,Z), s(R,Q)
    dl.add_rule(q_rel(Var<0>{}, Var<3>{}, Var<2>{}) %=
                q_by_r(Var<1>{}, Var<0>{}, Var<2>{}) && s_rel(Var<1>{}, Var<3>{}));

    // q(X,E,O) :- q(X,Y,Z), r(Y,U,E), q(Z,U,O)
    dl.add_rule(q_rel(Var<0>{}, Var<4>{}, Var<5>{}) %= q_by_z(Var<2>{}, Var<0>{}, Var<1>{}) &&
                                                       r_rel(Var<1>{}, Var<3>{}, Var<4>{}) &&
                                                       q_by_r(Var<3>{}, Var<2>{}, Var<5>{}));

    p_rel.insert(df::Relation<std::array<int32_t, 2>>::from_vec(std::vector(data.p)));
    q_rel.insert(df::Relation<std::array<int32_t, 3>>::from_vec(std::vector(data.q)));
    r_rel.insert(df::Relation<std::array<int32_t, 3>>::from_vec(std::vector(data.r)));
    c_rel.insert(df::Relation<std::array<int32_t, 3>>::from_vec(std::vector(data.c)));
    u_rel.insert(df::Relation<std::array<int32_t, 3>>::from_vec(std::vector(data.u)));
    s_rel.insert(df::Relation<std::array<int32_t, 2>>::from_vec(std::vector(data.s)));

    dl.solve();

    auto p_result = p_rel.extract();
    auto q_result = q_rel.extract();
    benchmark::DoNotOptimize(p_result.data());
    benchmark::DoNotOptimize(q_result.data());
    benchmark::ClobberMemory();

    return {p_result.size(), q_result.size()};
}

void BM_Dartfrog_Galen(benchmark::State &state) {
    fs::path data_dir(DATA_DIR);
    const GalenData data =
        load_galen_data(data_dir.string());
    std::array<size_t, 2> results;
    for (auto _ : state) {
        results = run_dartfrog_galen(data);
    }
    state.counters["p"] = static_cast<double>(results[0]);
    state.counters["q"] = static_cast<double>(results[1]);
}

void BM_Souffle_Galen(benchmark::State &state) {
    const std::string bin_path = BIN_PATH;
    const std::string data_dir = DATA_DIR;
    const std::string souffle_args = SOUFFLE_ARGS;
    const fs::path output_dir(OUT_DIR);

    std::string cmd = bin_path + " -F " + data_dir + " -D " + output_dir.string() + " " + souffle_args;

    for (auto _ : state) {
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            state.SkipWithError("Souffle execution failed");
            break;
        }
    }
    state.counters["p"] = static_cast<double>(countLines(output_dir / "p.csv"));
    state.counters["q"] = static_cast<double>(countLines(output_dir / "q.csv"));
}

BENCHMARK(BM_Dartfrog_Galen)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Souffle_Galen)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
