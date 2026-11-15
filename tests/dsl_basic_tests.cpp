#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <datalog.hpp>

using namespace df::datalog;
namespace {

template <class T>
df::Relation<std::array<T, 2>> rel(std::vector<std::array<T, 2>> v) {
    return df::Relation<std::array<T, 2>>::from_vec(std::move(v));
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
    Datalog dl;
    Predicate<int, 2> edge(dl), edge_rev(dl), path(dl);

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y, z), edge_rev(y, x) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge_rev(y, x), path(y, z));

    dl.solve();
    std::vector<std::array<int, 2>> final_paths = path.extract();

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

    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> copy(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y), copy(x, y) <= edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(DslSingleTerm, SwapReversesTuples) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> rev(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y), rev(y, x) <= edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(rev.extract()),
              (std::vector<std::array<int, 2>>{{2, 1}, {3, 2}, {4, 3}}));
}

TEST(DslJoin, SelfJoinTwoHopForwardHead) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> two_hop(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y, z), two_hop(x, z) <= edge(x, y), edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(two_hop.extract()),
              (std::vector<std::array<int, 2>>{{1, 3}, {2, 4}}));
}

TEST(DslJoin, SelfJoinTwoHopReversedHead) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> two_hop_rev(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y, z), two_hop_rev(z, x) <= edge(x, y), edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(two_hop_rev.extract()),
              (std::vector<std::array<int, 2>>{{3, 1}, {4, 2}}));
}

TEST(DslJoin, JoinAcrossTwoDistinctPredicates) {
    Datalog dl;
    Predicate<int, 2> a(dl);
    Predicate<int, 2> b(dl);
    Predicate<int, 2> ab(dl);
    a.insert(rel<int>({{1, 10}, {2, 20}}));
    b.insert(rel<int>({{10, 100}, {20, 200}, {30, 300}}));

    DL_RULE(dl, (x, y, z), ab(x, z) <= a(x, y), b(y, z));
    dl.solve();

    EXPECT_EQ(sorted(ab.extract()),
              (std::vector<std::array<int, 2>>{{1, 100}, {2, 200}}));
}

TEST(DslRecursion, TransitiveClosureLineGraph) {
    Datalog dl;
    Predicate<int, 2> edge(dl), path(dl);

    std::vector<std::array<int, 2>> edges = {
        {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}};
    edge.insert(rel<int>(edges));

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= path(x, y), edge(y, z));
    dl.solve();

    auto result = sorted(path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 15u);
}

TEST(DslRecursion, TerminatesOnCycle) {
    Datalog dl;
    Predicate<int, 2> edge(dl), edge_rev(dl), path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 1}};
    edge.insert(rel<int>(edges));

    DL_RULE(dl, (x, y, z), edge_rev(y, x) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge_rev(y, x), path(y, z));
    dl.solve();

    auto result = sorted(path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 9u);
}

TEST(DslRecursion, SelfLoop) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 1}, {1, 2}};
    edge.insert(rel<int>(edges));

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(path.extract()), transitive_closure(edges));
}

TEST(DslRecursion, DisconnectedComponentsDoNotMerge) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {3, 4}};
    edge.insert(rel<int>(edges));

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(path.extract()), transitive_closure(edges));
}

TEST(DslTypes, StringNodes) {
    Datalog dl;
    Predicate<std::string, 2> edge(dl), edge_rev(dl), path(dl);
    edge.insert(rel<std::string>({{"a", "b"}, {"b", "c"}, {"c", "d"}}));

    DL_RULE(dl, (x, y, z), edge_rev(y, x) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge_rev(y, x), path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(path.extract()),
              (std::vector<std::array<std::string, 2>>{{"a", "b"},
                                                       {"a", "c"},
                                                       {"a", "d"},
                                                       {"b", "c"},
                                                       {"b", "d"},
                                                       {"c", "d"}}));
}

TEST(DslTypes, HeterogeneousColumns) {
    Datalog dl;
    Predicate<int, 2> label(dl);
    Predicate<int, 2> copy(dl);
    label.insert(rel<int>({{1, 10}, {2, 20}}));

    DL_RULE(dl, (x, y), copy(x, y) <= label(x, y));
    dl.solve();

    EXPECT_EQ(sorted(copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 10}, {2, 20}}));
}

TEST(DslInvariants, InsertDeduplicates) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> copy(dl);
    edge.insert(rel<int>({{1, 2}, {1, 2}, {2, 3}, {2, 3}, {2, 3}}));

    DL_RULE(dl, (x, y, z), copy(x, y) <= edge(x, y));
    dl.solve();

    EXPECT_EQ(copy.extract().size(), 2u);
}

TEST(DslInvariants, ExtractIsSortedAndUnique) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> path(dl);
    edge.insert(rel<int>({{3, 4}, {1, 2}, {2, 3}}));

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    dl.solve();

    auto out = path.extract();
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()))
        << "Relation must yield tuples in sorted order";
    EXPECT_EQ(std::adjacent_find(out.begin(), out.end()), out.end())
        << "Relation must yield unique tuples";
}

TEST(DslEdgeCases, EmptyRelationStaysEmpty) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> path(dl);

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    dl.solve();

    EXPECT_TRUE(path.extract().empty());
}

TEST(DslEdgeCases, SingleEdge) {

    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> path(dl);
    edge.insert(rel<int>({{5, 6}}));

    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(path.extract()),
              (std::vector<std::array<int, 2>>{{5, 6}}));
}

TEST(DslEdgeCases, SolveIsIdempotent) {

    Datalog dl;
    Predicate<int, 2> edge(dl), edge_rev(dl), path(dl);
    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 4}};
    edge.insert(rel<int>(edges));

    DL_RULE(dl, (x, y, z), edge_rev(y, x) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge(x, y), path(y, z));
    DL_RULE(dl, (x, y, z), path(x, z) <= edge_rev(y, x), path(y, z));

    dl.solve();
    dl.solve();

    EXPECT_EQ(sorted(path.extract()), transitive_closure(edges));
}

TEST(DslWcoj, Triangle) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 3> tri(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 1}, {2, 4}, {4, 5}, {5, 2}}));
    DL_RULE(dl, (x, y, z), tri(x, y, z) <= edge(x, y), edge(y, z), edge(z, x));
    dl.solve();

    EXPECT_EQ(
        sorted(tri.extract()),
        (std::vector<std::array<int, 3>>{
            {1, 2, 3}, {2, 3, 1}, {2, 4, 5}, {3, 1, 2}, {4, 5, 2}, {5, 2, 4}}));
}

TEST(DslWcoj, ThreeHopChain) {
    auto w = Var<3>();
    Datalog dl;
    Predicate<int, 2> edge(dl), hop3(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}, {4, 5}}));
    DL_RULE(dl, (x, y, z), hop3(x, w) <= edge(x, y), edge(y, z), edge(z, w));
    dl.solve();
    EXPECT_EQ(sorted(hop3.extract()),
              (std::vector<std::array<int, 2>>{{1, 4}, {2, 5}}));
}

TEST(DslWcoj, TwoSharedKeysIsIntersection) {
    Datalog dl;
    Predicate<int, 2> a(dl), b(dl), c(dl);
    a.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    b.insert(rel<int>({{2, 3}, {3, 4}, {9, 9}}));
    DL_RULE(dl, (x, y), c(x, y) <= a(x, y), b(x, y));
    dl.solve();
    EXPECT_EQ(sorted(c.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}, {3, 4}}));
}

TEST(DslWcoj, NegationEdb) {
    Datalog dl;
    Predicate<int, 2> edge(dl), blocked(dl), open(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    blocked.insert(rel<int>({{2, 3}}));
    DL_RULE(dl, (x, y), open(x, y) <= edge(x, y), !blocked(x, y));
    dl.solve();
    EXPECT_EQ(sorted(open.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {3, 4}}));
}

TEST(DslNAry, TernaryPredicateAsSource) {

    Datalog dl;

    Predicate<int, 3> triple(dl);
    Predicate<int, 2> edge(dl), result(dl);

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y, z), triple(x, y, z) <= edge(x, y), edge(y, z));
    DL_RULE(dl, (x, y, z), result(x, z) <= triple(x, y, z));

    dl.solve();

    EXPECT_EQ(sorted(result.extract()),
              (std::vector<std::array<int, 2>>{{{1, 3}, {2, 4}}}));
}

TEST(DslUndirected, TCOnUndirectedGraph) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);

    dl.make_symmetric(edge);
    DL_RULE(dl, (x, y, z), tc(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), tc(x, z) <= tc(x, y), edge(y, z));

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

    DL_RULE(dl, (x, y), blocked(x, y) <= edge(x, y), heavy(x, y));
    DL_RULE(dl, (x, y), open(x, y) <= edge(x, y), !blocked(x, y));

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
    DL_RULE(dl, (x, y, z), tc(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), tc(x, z) <= tc(x, y), edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    dl.solve();

    auto seed = Const<int>({2});

    Datalog query_dl;
    Predicate<int, 2> result(query_dl);
    DL_RULE(query_dl, (x, y, z), result(x, y) <= seed(x), tc(x, y));
    query_dl.solve();

    EXPECT_EQ(sorted(result.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}, {2, 4}}));
}

TEST(DslQuery, DetachedPredicateFromExternalData) {
    using T2 = std::array<int, 2>;
    Predicate<int, 2> graph;
    graph.insert(
        df::Relation<T2>::from_vec({{1, 2}, {1, 3}, {2, 3}, {2, 4}, {3, 4}}));
    graph.commit();

    Datalog dl;
    auto seed = Const<int>({1});
    Predicate<int, 2> reachable(dl);
    DL_RULE(dl, (x, y), reachable(x, y) <= seed(x), graph(x, y));
    dl.solve();

    EXPECT_EQ(sorted(reachable.extract()), (std::vector<T2>{{1, 2}, {1, 3}}));
}

TEST(DslQuery, SolveIsIdempotentOnNoNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    DL_RULE(dl, (x, y, z), tc(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), tc(x, z) <= tc(x, y), edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();
    dl.solve();
    EXPECT_EQ(tc.peek(),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 3}, {2, 3}}));
}

TEST(DslQuery, CommittedFactsVisibleThroughReindex) {
    Datalog dl;
    Predicate<int, 2> aa(dl), bb(dl);
    Predicate<int, 3> c(dl);
    DL_RULE(dl, (x, y, z), c(x, y, z) <= aa(x, y), bb(y, z));

    aa.insert(rel<int>({{2, 1}, {3, 2}}));
    bb.insert(rel<int>({{1, 10}}));
    aa.commit();
    bb.commit();
    dl.solve();

    EXPECT_EQ(sorted(c.extract()),
              (std::vector<std::array<int, 3>>{{2, 1, 10}}));
}

TEST(DslQuery, FactsCommittedBetweenSolvesVisibleThroughReindex) {
    Datalog dl;
    Predicate<int, 2> aa(dl), bb(dl);
    Predicate<int, 3> c(dl);
    DL_RULE(dl, (x, y, z), c(x, y, z) <= aa(x, y), bb(y, z));

    aa.insert(rel<int>({{2, 1}, {3, 2}}));
    bb.insert(rel<int>({{1, 10}}));
    aa.commit();
    bb.commit();
    dl.solve();

    aa.insert(rel<int>({{4, 3}}));
    bb.insert(rel<int>({{3, 30}}));
    aa.commit();
    bb.commit();
    dl.solve();

    EXPECT_EQ(sorted(c.extract()),
              (std::vector<std::array<int, 3>>{{2, 1, 10}, {4, 3, 30}}));
}

TEST(DslQuery, SolveSecondCallPicksUpNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    DL_RULE(dl, (x, y, z), tc(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), tc(x, z) <= tc(x, y), edge(y, z));
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
    DL_RULE(dl, (x, y, z), tc(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y, z), tc(x, z) <= tc(x, y), edge(y, z));

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

    Datalog dl;

    Predicate<int, 2> succ(dl);
    Predicate<int, 1> num(dl);
    Predicate<int, 3> add(dl);
    Predicate<int, 3> mul(dl);

    auto zero = Const<int>({0});

    // The domain of numbers we'll compute over
    succ.insert(rel<int>({{0, 1}, {1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y, y_next, res, res_next), num(x) <= zero(x));
    DL_RULE(dl, (x, y, y_next, res, res_next), num(x) <= succ(x, y));
    DL_RULE(dl, (x, y, y_next, res, res_next), num(y) <= succ(x, y));

    DL_RULE(dl, (x, y, y_next, res, res_next), add(x, y, x) <= num(x), zero(y));
    DL_RULE(dl, (x, y, y_next, res, res_next), add(x, y_next, res_next) <= add(x, y, res), succ(y, y_next),
         succ(res, res_next));

    DL_RULE(dl, (x, y, y_next, res, res_next), mul(x, y, y) <= num(x), zero(y));
    DL_RULE(dl, (x, y, y_next, res, res_next), mul(x, y_next, res_next) <= mul(x, y, res), succ(y, y_next),
         add(x, res, res_next));

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

TEST(DslProvenance, PeanoArithmeticProvenance) {
    Datalog dl;
    using T3 = std::array<int, 3>;

    Predicate<int, 3> succ(dl);
    Predicate<int, 4> add_prov(dl);
    Predicate<int, 2> step_dep(dl);
    Predicate<int, 2> depends_on(dl);
    Predicate<int, 1> num(dl);

    auto zero = Const<int>({0});

        Var<0> n;
        DL_RULE(dl, (n), num(n) <= zero(n));

        DL_RULE(dl, (x, sx, id), num(sx) <= succ(x, sx, id));

        DL_RULE(dl, (zval, y), add_prov(zval, y, y, zval) <= zero(zval), num(y));

        DL_RULE(dl, (sx, y, sz, x, z, id1, id2, id3), add_prov(sx, y, sz, sx) <= add_prov(x, y, z, id1),
             succ(x, sx, id2), succ(z, sz, id3));

        DL_RULE(dl, (sx, id1, x, y, z, id2, sz, id3), step_dep(sx, id1) <= add_prov(x, y, z, id1), succ(x, sx, id2),
             succ(z, sz, id3));

        DL_RULE(dl, (sx, id2, x, y, z, id1, sz, id3), step_dep(sx, id2) <= add_prov(x, y, z, id1), succ(x, sx, id2),
             succ(z, sz, id3));

        DL_RULE(dl, (sx, id3, x, y, z, id1, id2, sz), step_dep(sx, id3) <= add_prov(x, y, z, id1), succ(x, sx, id2),
             succ(z, sz, id3));

        DL_RULE(dl, (child, parent), depends_on(child, parent) <= step_dep(child, parent));

        DL_RULE(dl, (child, ancestor, parent), depends_on(child, ancestor) <= step_dep(child, parent),
             depends_on(parent, ancestor));

    succ.insert(df::Relation<T3>::from_vec(
        {{0, 1, 101}, {1, 2, 102}, {2, 3, 103}, {3, 4, 104}, {4, 5, 105}}));
    succ.commit();

    dl.solve();

    auto add_results = add_prov.extract();
    bool found_sum = false;
    int prov_id_2_plus_3 = -1;

    for (const auto &row : add_results) {
        if (row[0] == 2 && row[1] == 3 && row[2] == 5) {
            found_sum = true;
            prov_id_2_plus_3 = row[3];
            break;
        }
    }
    EXPECT_TRUE(found_sum);

    auto lineage_results = depends_on.extract();
    std::set<int> contributing_facts;

    for (const auto &row : lineage_results) {
        if (row[0] == prov_id_2_plus_3) {
            contributing_facts.insert(row[1]);
        }
    }

    EXPECT_TRUE(contributing_facts.count(102) > 0);
}

TEST(DslAggregates, SumSalesByGroup) {
    Datalog dl;
    Predicate<int, 2> sales(dl), total_sales(dl);

    DL_RULE(dl, (group, value, total), total_sales(group, total) <= sales(group, value),
         group_by<total, value, group>([](std::span<const int> values) {
             return std::accumulate(values.begin(), values.end(), 0);
         }));

    sales.insert(rel<int>({
        {1, 10},
        {1, 20},
        {1, 5},
        {2, 7},
        {2, 3},
        {3, 42},
    }));

    dl.solve();

    EXPECT_EQ(total_sales.extract(), (std::vector<std::array<int, 2>>{
                                         {1, 35},
                                         {2, 10},
                                         {3, 42},
                                     }));
}

TEST(DslAggregates, FiltersValuesBeforeSumming) {
    Datalog dl;
    Predicate<int, 2> sales(dl), total_sales(dl);

    // clang-format off
    DL_RULE(dl, (group, value, total),
        total_sales(group, total) <=
            sales(group, value),
            where<value>([](int value) {
                return value >= 10;
            }),
            group_by<total, value, group>([](std::span<const int> values) {
                return std::accumulate(values.begin(), values.end(), 0);
            }
    ));
    // clang-format on

    sales.insert(rel<int>({
        {1, 5},
        {1, 10},
        {1, 20},
        {2, 3},
        {2, 15},
    }));

    dl.solve();

    EXPECT_EQ(total_sales.extract(), (std::vector<std::array<int, 2>>{
                                         {1, 30},
                                         {2, 15},
                                     }));
}

TEST(DslAggregates, RecomputesInLaterRound) {
    Datalog dl;
    Predicate<int, 1> seed(dl), s(dl);
    Predicate<int, 2> v(dl);
    Predicate<int, 2> big(dl);

    Var<2> t;
    DL_RULE(dl, (x, y, t), s(x) <= seed(x));
    DL_RULE(dl, (x, y, t), big(x, t) <= s(x), v(x, y),
         group_by<t, y, x>([](std::span<const int> values) {
             int sum = 0;
             for (int value : values)
                 sum += value;
             return sum;
         }));

    seed.insert(df::Relation<std::array<int, 1>>::from_vec({{1}}));
    v.insert(rel<int>({{1, 10}}));
    dl.solve();

    EXPECT_EQ(big.extract(), (std::vector<std::array<int, 2>>{{1, 10}}));
}

TEST(DslAggregates, AggregateValuesAccumulate) {
    Datalog dl;
    Predicate<int, 2> base(dl), edge(dl), tc(dl);
    Predicate<int, 2> big(dl);

    Var<2> t;
    DL_RULE(dl, (x, y, z), tc(x, y) <= base(x, y));
    DL_RULE(dl, (x, y, z), tc(x, y) <= tc(x, z), edge(z, y));
    DL_RULE(dl, (x, y, z), big(x, t) <= tc(x, y),
         group_by<t, y, x>([](std::span<const int> values) {
             int sum = 0;
             for (int value : values)
                 sum += value;
             return sum;
         }));

    base.insert(rel<int>({{1, 2}}));
    edge.insert(rel<int>({{2, 3}, {3, 4}}));
    dl.solve();

    // Test that each of the values produced by the aggregate accumulate
    // across rounds
    EXPECT_EQ(sorted(big.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 5}, {1, 9}}));
    EXPECT_EQ(sorted(tc.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 3}, {1, 4}}));
}

TEST(DslAggregates, RowReductionArithmetic) {
    Datalog dl;
    Predicate<int, 3> a(dl);
    Predicate<int, 2> g(dl);

    DL_RULE(dl, (x, y, w, t), g(x, t) <= a(x, y, w),
         group_by_rows<t, x>([y, w](std::span<const std::array<int, 3>> rows) {
             int sum = 0;
             for (const auto &row : rows)
                 sum += row[y] / row[w];
             return sum;
         }));

    a.insert(df::Relation<std::array<int, 3>>::from_vec(
        {{1, 4, 2}, {1, 1, 1}, {2, 5, 5}}));
    dl.solve();

    EXPECT_EQ(sorted(g.extract()),
              (std::vector<std::array<int, 2>>{{1, 3}, {2, 1}}));
}

TEST(DslAggregates, PageRank) {
    Datalog dl;
    Predicate<double, 2> link(dl), outdeg(dl), succ(dl);
    Predicate<double, 3> rank(dl);

        DL_RULE(dl, (q, w, d), outdeg(q, d) <= link(q, w),
             group_by<d, w, q>([](std::span<const double> values) {
                 return static_cast<double>(values.size());
             }));

        DL_RULE(dl, (k2, k, q, p, rq, dq, r), rank(k2, p, r) <= rank(k, q, rq), succ(k2, k), link(q, p),
             outdeg(q, dq),
             group_by_rows<r, p, k2>(
                 [rq, dq](std::span<const std::array<double, 6>> rows) {
                     double sum = 0.0;
                     for (const auto &row : rows)
                         sum += row[rq] / row[dq];
                     return 1.0 / 6.0 + 0.5 * sum;
                 }));

    link.insert(df::Relation<std::array<double, 2>>::from_vec(
        {{1, 2}, {2, 1}, {2, 3}, {3, 2}}));
    succ.insert(
        df::Relation<std::array<double, 2>>::from_vec({{1, 0}, {2, 1}}));
    rank.insert(df::Relation<std::array<double, 3>>::from_vec(
        {{0, 1, 1.0 / 3}, {0, 2, 1.0 / 3}, {0, 3, 1.0 / 3}}));
    dl.solve();

    auto layer = [&](double k) {
        std::vector<std::array<double, 3>> rows;
        for (const auto &row : rank.peek())
            if (row[0] == k)
                rows.push_back(row);
        std::sort(rows.begin(), rows.end());
        return rows;
    };

    constexpr double epsilon = 1e-9;
    auto l1 = layer(1.0);
    ASSERT_EQ(l1.size(), 3);
    EXPECT_NEAR(l1[0][2], 0.25, epsilon);
    EXPECT_NEAR(l1[1][2], 0.5, epsilon);
    EXPECT_NEAR(l1[2][2], 0.25, epsilon);

    auto l2 = layer(2.0);
    ASSERT_EQ(l2.size(), 3);
    EXPECT_NEAR(l2[0][2], 7.0 / 24, epsilon);
    EXPECT_NEAR(l2[1][2], 5.0 / 12, epsilon);
    EXPECT_NEAR(l2[2][2], 7.0 / 24, epsilon);
}

TEST(DslRuleSyntax, TriangleTest) {
    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 3> tri(dl);

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 1}, {2, 4}, {4, 5}, {5, 2}}));

    DL_RULE(dl, (x, y, z), tri(x, y, z) <= edge(x, y), edge(y, z), edge(z, x));
    dl.solve();

    EXPECT_EQ(
        sorted(tri.extract()),
        (std::vector<std::array<int, 3>>{
            {1, 2, 3}, {2, 3, 1}, {2, 4, 5}, {3, 1, 2}, {4, 5, 2}, {5, 2, 4}}));
}

TEST(DslInlineVars, SanityCheck) {

    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> copy(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y), copy(x, y) <= edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(DslInlineVars, DeclsDontCollide) {

    Datalog dl;
    Predicate<int, 2> edge(dl);
    Predicate<int, 2> original(dl);
    Predicate<int, 2> reversed(dl);
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_RULE(dl, (x, y), original(x, y) <= edge(x, y));
    DL_RULE(dl, (x, y), reversed(y, x) <= edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(original.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {2, 3}, {3, 4}}));

    EXPECT_EQ(sorted(reversed.extract()),
              (std::vector<std::array<int, 2>>{{2, 1}, {3, 2}, {4, 3}}));
}

