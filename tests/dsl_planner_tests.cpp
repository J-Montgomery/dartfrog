#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include "gtest/gtest.h"

#include "dartfrog.hpp"
#include "datalog.hpp"

namespace {

using df::datalog::Datalog;
using df::datalog::Predicate;
using df::datalog::Var;
using Edge = std::array<int32_t, 2>;
using Triple = std::array<int32_t, 3>;

template <size_t N>
std::vector<std::array<int32_t, N>> su(std::vector<std::array<int32_t, N>> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// transitive closure: tc(X,Z) :- edge(X,Z); tc(X,Z) :- edge(X,Y), tc(Y,Z).
std::set<Edge> reference_tc(const std::vector<Edge> &edges) {
    std::set<Edge> tc(edges.begin(), edges.end());
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Edge> add;
        for (const Edge &xy : edges)
            for (const Edge &yz : tc)
                if (xy[1] == yz[0] && !tc.count({xy[0], yz[1]}))
                    add.push_back({xy[0], yz[1]});
        for (const Edge &e : add)
            if (tc.insert(e).second)
                changed = true;
    }
    return tc;
}

std::set<Edge> dartfrog_sg(const std::vector<Edge> &edges) {
    Datalog dl;
    Predicate<int32_t, 2> edge(dl);
    Predicate<int32_t, 2> tc(dl);
    dl.add_rule(tc(Var<0>{}, Var<1>{}) %= edge(Var<0>{}, Var<1>{}));
    dl.add_rule(tc(Var<0>{}, Var<2>{}) %=
                edge(Var<0>{}, Var<1>{}) && tc(Var<1>{}, Var<2>{}));
    edge.insert(df::Relation<Edge>::from_vec(std::vector<Edge>(edges)));
    dl.solve();
    const std::vector<Edge> out = tc.extract();
    return std::set<Edge>(out.begin(), out.end());
}

// same generation: sg(X,Y) :- flat(X,Y);
// sg(X,Y):-up(X,A),sg(A,B),down(B,Y)
std::set<Edge> reference_sg(const std::vector<Edge> &up,
                            const std::vector<Edge> &down,
                            const std::vector<Edge> &flat) {
    std::set<Edge> sg(flat.begin(), flat.end());
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Edge> add;
        for (const Edge &ab : sg)
            for (const Edge &xa : up)
                if (xa[1] == ab[0])
                    for (const Edge &by : down)
                        if (by[0] == ab[1] && !sg.count({xa[0], by[1]}))
                            add.push_back({xa[0], by[1]});
        for (const Edge &e : add)
            if (sg.insert(e).second)
                changed = true;
    }
    return sg;
}

std::set<Edge> dartfrog_sg(const std::vector<Edge> &up,
                           const std::vector<Edge> &down,
                           const std::vector<Edge> &flat) {
    Datalog dl;
    Predicate<int32_t, 2> up_p(dl);
    Predicate<int32_t, 2> down_p(dl);
    Predicate<int32_t, 2> flat_p(dl);
    Predicate<int32_t, 2> sg(dl);
    dl.add_rule(sg(Var<0>{}, Var<1>{}) %= flat_p(Var<0>{}, Var<1>{}));
    dl.add_rule(sg(Var<0>{}, Var<3>{}) %= up_p(Var<0>{}, Var<1>{}) &&
                                          sg(Var<1>{}, Var<2>{}) &&
                                          down_p(Var<2>{}, Var<3>{}));
    up_p.insert(df::Relation<Edge>::from_vec(std::vector<Edge>(up)));
    down_p.insert(df::Relation<Edge>::from_vec(std::vector<Edge>(down)));
    flat_p.insert(df::Relation<Edge>::from_vec(std::vector<Edge>(flat)));
    dl.solve();
    const std::vector<Edge> out = sg.extract();
    return std::set<Edge>(out.begin(), out.end());
}

// cyclic triangle query: tri(X,Y,Z) :- e(X,Y), e(Y,Z), e(X,Z).
std::set<Triple> reference_tri(const std::vector<Edge> &edges) {
    std::set<Edge> present(edges.begin(), edges.end());
    std::set<Triple> out;
    for (const Edge &ab : edges)
        for (const Edge &bc : edges)
            if (bc[0] == ab[1] && present.count({ab[0], bc[1]}))
                out.insert({ab[0], ab[1], bc[1]});
    return out;
}

std::set<Triple> dartfrog_tri(const std::vector<Edge> &edges) {
    Datalog dl;
    Predicate<int32_t, 2> e(dl);
    Predicate<int32_t, 3> tri(dl);
    dl.add_rule(tri(Var<0>{}, Var<1>{}, Var<2>{}) %= e(Var<0>{}, Var<1>{}) &&
                                                     e(Var<1>{}, Var<2>{}) &&
                                                     e(Var<0>{}, Var<2>{}));
    e.insert(df::Relation<Edge>::from_vec(std::vector<Edge>(edges)));
    dl.solve();
    const std::vector<Triple> out = tri.extract();
    return std::set<Triple>(out.begin(), out.end());
}

TEST(DslPlanner, TransitiveClosureMatchesOracle) {
    std::mt19937 rng;
    for (int c = 0; c < 100; ++c) {
        std::uniform_int_distribution<int32_t> node(0, 7);
        std::uniform_int_distribution<int> cnt(0, 16);
        std::vector<Edge> edges;
        for (int i = 0, m = cnt(rng); i < m; ++i)
            edges.push_back({node(rng), node(rng)});
        edges = su<2>(std::move(edges));
        ASSERT_EQ(dartfrog_sg(edges), reference_tc(edges)) << "case " << c;
    }
}

TEST(DslPlanner, SameGenerationMatchesOracle) {
    std::mt19937 rng;
    for (int c = 0; c < 100; ++c) {
        std::uniform_int_distribution<int32_t> node(0, 6);
        std::uniform_int_distribution<int> cnt(0, 12);
        auto mk = [&] {
            std::vector<Edge> r;
            for (int i = 0, m = cnt(rng); i < m; ++i)
                r.push_back({node(rng), node(rng)});
            return su<2>(std::move(r));
        };
        const std::vector<Edge> up = mk(), down = mk(), flat = mk();
        ASSERT_EQ(dartfrog_sg(up, down, flat), reference_sg(up, down, flat))
            << "case " << c;
    }
}

TEST(DslPlanner, TriangleMatchesOracle) {
    std::mt19937 rng;
    for (int c = 0; c < 100; ++c) {
        std::uniform_int_distribution<int32_t> node(0, 9);
        std::uniform_int_distribution<int> cnt(0, 24);
        std::vector<Edge> edges;
        for (int i = 0, m = cnt(rng); i < m; ++i)
            edges.push_back({node(rng), node(rng)});
        edges = su<2>(std::move(edges));
        ASSERT_EQ(dartfrog_tri(edges), reference_tri(edges)) << "case " << c;
    }
}

} // namespace
