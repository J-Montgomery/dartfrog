#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <datalog.hpp>
#include <gtest/gtest.h>

using namespace dt::datalog;
namespace {

template <class T>
dt::Relation<std::array<T, 2>> rel(std::vector<std::array<T, 2>> v) {
    return dt::Relation<std::array<T, 2>>::from_vec(std::move(v));
}

template <class T> std::vector<T> sorted(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<std::array<int, 2>>
transitive_closure(const std::vector<std::array<int, 2>> &edges) {
    std::set<std::array<int, 2>> tc(edges.begin(), edges.end());
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<std::array<int, 2>> snap(tc.begin(), tc.end());
        for (const auto &[a, b] : snap)
            for (const auto &[c, d] : snap)
                if (b == c)
                    changed |= tc.insert({a, d}).second;
    }
    return {tc.begin(), tc.end()};
}
} // namespace

TEST(DatalogTests, BasicEdges) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;

    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);

    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Edge_rev(y, x) %= Edge(x, y));
    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.add_rule(Path(x, z) %= Edge_rev(y, x) && Path(y, z));

    dl.solve();
    std::vector<std::array<int, 2>> final_paths = Path.extract();

    for (const auto &p : final_paths) {
        std::cout << "(" << p[0] << ", " << p[1] << ")" << std::endl;
    }

    std::cout << std::endl;

    std::vector<std::array<int, 2>> expected_paths = {{1, 2}, {2, 3}, {3, 4},
                                                      {1, 3}, {2, 4}, {1, 4}};

    std::cout << "=== expected paths (" << expected_paths.size()
              << ") ===" << std::endl;
    for (const auto &p : expected_paths) {
        std::cout << "(" << p[0] << ", " << p[1] << ")" << std::endl;
    }

    std::sort(final_paths.begin(), final_paths.end());
    std::sort(expected_paths.begin(), expected_paths.end());

    ASSERT_EQ(final_paths.size(), expected_paths.size())
        << "Incorrect number of paths found.";
    EXPECT_EQ(final_paths, expected_paths)
        << "Path tuples do not match expected transitive closure.";
}

TEST(DslSingleTerm, DirectCopyIsIdentity) {
    auto x = Var<0>();
    auto y = Var<1>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Copy(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Copy(x, y) %= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(DslSingleTerm, SwapReversesTuples) {
    auto x = Var<0>();
    auto y = Var<1>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Rev(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Rev(y, x) %= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Rev.extract()),
              (std::vector<std::array<int, 2>>{{2, 1}, {3, 2}, {4, 3}}));
}

TEST(DslJoin, SelfJoinTwoHopForwardHead) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> TwoHop(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(TwoHop(x, z) %= Edge(x, y) && Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHop.extract()),
              (std::vector<std::array<int, 2>>{{1, 3}, {2, 4}}));
}

TEST(DslJoin, SelfJoinTwoHopReversedHead) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> TwoHopRev(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(TwoHopRev(z, x) %= Edge(x, y) && Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHopRev.extract()),
              (std::vector<std::array<int, 2>>{{3, 1}, {4, 2}}));
}

TEST(DslJoin, JoinAcrossTwoDistinctPredicates) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> A(dl);
    Predicate<int, 2> B(dl);
    Predicate<int, 2> AB(dl);
    A.insert(rel<int>({{1, 10}, {2, 20}}));
    B.insert(rel<int>({{10, 100}, {20, 200}, {30, 300}}));

    dl.add_rule(AB(x, z) %= A(x, y) && B(y, z));
    dl.solve();

    EXPECT_EQ(sorted(AB.extract()),
              (std::vector<std::array<int, 2>>{{1, 100}, {2, 200}}));
}

TEST(DslRecursion, TransitiveClosureLineGraph) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl), Path(dl);

    std::vector<std::array<int, 2>> edges = {
        {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}};
    Edge.insert(rel<int>(edges));

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Path(x, y) && Edge(y, z));
    dl.solve();

    auto result = sorted(Path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 15u);
}

TEST(DslRecursion, TerminatesOnCycle) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 1}};
    Edge.insert(rel<int>(edges));

    dl.add_rule(Edge_rev(y, x) %= Edge(x, y));
    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.add_rule(Path(x, z) %= Edge_rev(y, x) && Path(y, z));
    dl.solve();

    auto result = sorted(Path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 9u);
}

TEST(DslRecursion, SelfLoop) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 1}, {1, 2}};
    Edge.insert(rel<int>(edges));

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslRecursion, DisconnectedComponentsDoNotMerge) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {3, 4}};
    Edge.insert(rel<int>(edges));

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslTypes, StringNodes) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<std::string, 2> Edge(dl), Edge_rev(dl), Path(dl);
    Edge.insert(rel<std::string>({{"a", "b"}, {"b", "c"}, {"c", "d"}}));

    dl.add_rule(Edge_rev(y, x) %= Edge(x, y));
    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.add_rule(Path(x, z) %= Edge_rev(y, x) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()),
              (std::vector<std::array<std::string, 2>>{{"a", "b"},
                                                       {"a", "c"},
                                                       {"a", "d"},
                                                       {"b", "c"},
                                                       {"b", "d"},
                                                       {"c", "d"}}));
}

TEST(DslTypes, HeterogeneousColumns) {
    auto x = Var<0>();
    auto y = Var<1>();

    Datalog dl;
    Predicate<int, 2> Label(dl);
    Predicate<int, 2> Copy(dl);
    Label.insert(rel<int>({{1, 10}, {2, 20}}));

    dl.add_rule(Copy(x, y) %= Label(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 10}, {2, 20}}));
}

TEST(DslInvariants, InsertDeduplicates) {
    auto x = Var<0>();
    auto y = Var<1>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Copy(dl);
    Edge.insert(rel<int>({{1, 2}, {1, 2}, {2, 3}, {2, 3}, {2, 3}}));

    dl.add_rule(Copy(x, y) %= Edge(x, y));
    dl.solve();

    EXPECT_EQ(Copy.extract().size(), 2u);
}

TEST(DslInvariants, ExtractIsSortedAndUnique) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);
    Edge.insert(rel<int>({{3, 4}, {1, 2}, {2, 3}}));

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.solve();

    auto out = Path.extract();
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()))
        << "Relation must yield tuples in sorted order";
    EXPECT_EQ(std::adjacent_find(out.begin(), out.end()), out.end())
        << "Relation must yield unique tuples";
}

TEST(DslEdgeCases, EmptyRelationStaysEmpty) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_TRUE(Path.extract().empty());
}

TEST(DslEdgeCases, SingleEdge) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);
    Edge.insert(rel<int>({{5, 6}}));

    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()),
              (std::vector<std::array<int, 2>>{{5, 6}}));
}

TEST(DslEdgeCases, SolveIsIdempotent) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;
    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);
    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 4}};
    Edge.insert(rel<int>(edges));

    dl.add_rule(Edge_rev(y, x) %= Edge(x, y));
    dl.add_rule(Path(x, y) %= Edge(x, y));
    dl.add_rule(Path(x, z) %= Edge(x, y) && Path(y, z));
    dl.add_rule(Path(x, z) %= Edge_rev(y, x) && Path(y, z));

    dl.solve();
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslWcoj, Triangle) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 3> Tri(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 1}, {2, 4}, {4, 5}, {5, 2}}));
    dl.add_rule(Tri(x, y, z) %= Edge(x, y) && Edge(y, z) && Edge(z, x));
    dl.solve();

    EXPECT_EQ(
        sorted(Tri.extract()),
        (std::vector<std::array<int, 3>>{
            {1, 2, 3}, {2, 3, 1}, {2, 4, 5}, {3, 1, 2}, {4, 5, 2}, {5, 2, 4}}));
}

TEST(DslWcoj, ThreeHopChain) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();
    auto w = Var<3>();
    Datalog dl;
    Predicate<int, 2> Edge(dl), Hop3(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}, {4, 5}}));
    dl.add_rule(Hop3(x, w) %= Edge(x, y) && Edge(y, z) && Edge(z, w));
    dl.solve();
    EXPECT_EQ(sorted(Hop3.extract()),
              (std::vector<std::array<int, 2>>{{1, 4}, {2, 5}}));
}

TEST(DslWcoj, TwoSharedKeysIsIntersection) {
    auto x = Var<0>();
    auto y = Var<1>();
    Datalog dl;
    Predicate<int, 2> A(dl), B(dl), C(dl);
    A.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    B.insert(rel<int>({{2, 3}, {3, 4}, {9, 9}}));
    dl.add_rule(C(x, y) %= A(x, y) && B(x, y));
    dl.solve();
    EXPECT_EQ(sorted(C.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}, {3, 4}}));
}

TEST(DslWcoj, NegationEdb) {
    auto x = Var<0>();
    auto y = Var<1>();
    Datalog dl;
    Predicate<int, 2> Edge(dl), Blocked(dl), Open(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    Blocked.insert(rel<int>({{2, 3}}));
    dl.add_rule(Open(x, y) %= Edge(x, y) && !Blocked(x, y));
    dl.solve();
    EXPECT_EQ(sorted(Open.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {3, 4}}));
}

TEST(DslNAry, TernaryPredicateAsSource) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto z = Var<2>();

    Datalog dl;

    Predicate<int, 3> triple(dl);
    Predicate<int, 2> edge(dl), result(dl);

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(triple(x, y, z) %= edge(x, y) && edge(y, z));
    dl.add_rule(result(x, z) %= triple(x, y, z));

    dl.solve();

    EXPECT_EQ(sorted(result.extract()),
              (std::vector<std::array<int, 2>>{{{1, 3}, {2, 4}}}));
}

TEST(DslUndirected, TCOnUndirectedGraph) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    Var<0> x;
    Var<1> y;
    Var<2> z;

    dl.make_symmetric(edge);
    dl.add_rule(tc(x, y) %= edge(x, y));
    dl.add_rule(tc(x, z) %= tc(x, y) && edge(y, z));

    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();

    auto result = sorted(tc.extract());
    EXPECT_EQ(result, (std::vector<std::array<int, 2>>{{1, 1},
                                                       {1, 2},
                                                       {1, 3},
                                                       {2, 1},
                                                       {2, 2},
                                                       {2, 3},
                                                       {3, 1},
                                                       {3, 2},
                                                       {3, 3}}));
}

TEST(DslStratification, NegationOverDerivedPredicate) {
    Datalog dl;
    Predicate<int, 2> edge(dl), heavy(dl), blocked(dl), open(dl);
    Var<0> x;
    Var<1> y;

    dl.add_rule(blocked(x, y) %= edge(x, y) && heavy(x, y));
    dl.add_rule(open(x, y) %= edge(x, y) && !blocked(x, y));

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    heavy.insert(rel<int>({{2, 3}}));
    dl.solve();

    EXPECT_EQ(sorted(blocked.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}}));
    EXPECT_EQ(sorted(open.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {3, 4}}));
}

TEST(DslQuery, QueryOverSolvedIDB) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    Var<0> x;
    Var<1> y;
    Var<2> z;
    dl.add_rule(tc(x, y) %= edge(x, y));
    dl.add_rule(tc(x, z) %= tc(x, y) && edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    dl.solve();

    auto seed = Const<int>({2});

    Datalog query_dl;
    Predicate<int, 2> result(query_dl);
    query_dl.add_rule(result(x, y) %= seed(x) && tc(x, y));
    query_dl.solve();

    EXPECT_EQ(sorted(result.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}, {2, 4}}));
}

TEST(DslQuery, DetachedPredicateFromExternalData) {
    using T2 = std::array<int, 2>;
    Predicate<int, 2> graph;
    graph.insert(
        dt::Relation<T2>::from_vec({{1, 2}, {1, 3}, {2, 3}, {2, 4}, {3, 4}}));
    graph.commit();

    Datalog query_dl;
    auto seed = Const<int>({1});
    Predicate<int, 2> reachable(query_dl);
    Var<0> x;
    Var<1> y;
    query_dl.add_rule(reachable(x, y) %= seed(x) && graph(x, y));
    query_dl.solve();

    EXPECT_EQ(sorted(reachable.extract()), (std::vector<T2>{{1, 2}, {1, 3}}));
}

TEST(DslQuery, SolveIsIdempotentOnNoNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    Var<0> x;
    Var<1> y;
    Var<2> z;
    dl.add_rule(tc(x, y) %= edge(x, y));
    dl.add_rule(tc(x, z) %= tc(x, y) && edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();
    dl.solve();
    EXPECT_EQ(tc.peek(),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 3}, {2, 3}}));
}

TEST(DslQuery, SolveSecondCallPicksUpNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    Var<0> x;
    Var<1> y;
    Var<2> z;
    dl.add_rule(tc(x, y) %= edge(x, y));
    dl.add_rule(tc(x, z) %= tc(x, y) && edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();
    edge.insert(rel<int>({{3, 4}}));
    dl.solve();

    EXPECT_EQ(tc.extract(),
              (std::vector<std::array<int, 2>>{
                  {1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}}));
}

TEST(DslIncremental, UpdateAddsNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    Var<0> x;
    Var<1> y;
    Var<2> z;
    dl.add_rule(tc(x, y) %= edge(x, y));
    dl.add_rule(tc(x, z) %= tc(x, y) && edge(y, z));

    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();

    EXPECT_EQ(tc.peek(),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 3}, {2, 3}}));

    edge.insert(rel<int>({{3, 4}}));
    dl.solve();
    EXPECT_EQ(tc.extract(),
              (std::vector<std::array<int, 2>>{
                  {1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}}));
}

TEST(DslIncremental, PeanoArithmetic) {
    auto x = Var<0>();
    auto y = Var<1>();
    auto y_next = Var<2>();
    auto res = Var<3>();
    auto res_next = Var<4>();

    Datalog dl;

    Predicate<int, 2> succ(dl);
    Predicate<int, 1> num(dl);
    Predicate<int, 3> add(dl);
    Predicate<int, 3> mul(dl);

    auto zero = Const<int>({0});

    // The domain of numbers we'll compute over
    succ.insert(rel<int>({{0, 1}, {1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(num(x) %= zero(x));
    dl.add_rule(num(x) %= succ(x, y));
    dl.add_rule(num(y) %= succ(x, y));

    dl.add_rule(add(x, y, x) %= num(x) && zero(y));
    dl.add_rule(add(x, y_next, res_next) %=
                add(x, y, res) && succ(y, y_next) && succ(res, res_next));

    dl.add_rule(mul(x, y, y) %= num(x) && zero(y));
    dl.add_rule(mul(x, y_next, res_next) %=
                mul(x, y, res) && succ(y, y_next) && add(x, res, res_next));

    dl.solve();

    auto has_fact = [](const auto &vec, const std::array<int, 3> &val) {
        return std::find(vec.begin(), vec.end(), val) != vec.end();
    };

    auto add_out = add.extract();
    EXPECT_TRUE(has_fact(add_out, {1, 2, 3})); // 1 + 2 = 3
    EXPECT_TRUE(has_fact(add_out, {2, 1, 3})); // 2 + 1 = 3

    auto mul_out = mul.extract();
    EXPECT_TRUE(has_fact(mul_out, {2, 1, 2})); // 2 * 1 = 2

    succ.insert(rel<int>({{3, 4}}));
    dl.solve();

    add_out = add.extract();
    EXPECT_TRUE(has_fact(add_out, {2, 2, 4})); // 2 + 2 = 4

    mul_out = mul.extract();
    EXPECT_TRUE(has_fact(mul_out, {2, 2, 4})); // 2 * 2 = 4
}