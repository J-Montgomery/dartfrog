#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <datalog.hpp>
#include <gtest/gtest.h>

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
    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);

    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_VARS(x, y, z);
    RULE(dl, Edge_rev(y, x) <= Edge(x, y));
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    RULE(dl, Path(x, z) <= Edge_rev(y, x), Path(y, z));

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

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Copy(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_VARS(x, y);
    RULE(dl, Copy(x, y) <= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(DslSingleTerm, SwapReversesTuples) {
    Datalog dl;
    DL_VARS(x, y);
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Rev(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    RULE(dl, Rev(y, x) <= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Rev.extract()),
              (std::vector<std::array<int, 2>>{{2, 1}, {3, 2}, {4, 3}}));
}

TEST(DslJoin, SelfJoinTwoHopForwardHead) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> TwoHop(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_VARS(x, y, z);
    RULE(dl, TwoHop(x, z) <= Edge(x, y), Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHop.extract()),
              (std::vector<std::array<int, 2>>{{1, 3}, {2, 4}}));
}

TEST(DslJoin, SelfJoinTwoHopReversedHead) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> TwoHopRev(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    DL_VARS(x, y, z);
    RULE(dl, TwoHopRev(z, x) <= Edge(x, y), Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHopRev.extract()),
              (std::vector<std::array<int, 2>>{{3, 1}, {4, 2}}));
}

TEST(DslJoin, JoinAcrossTwoDistinctPredicates) {
    Datalog dl;
    Predicate<int, 2> A(dl);
    Predicate<int, 2> B(dl);
    Predicate<int, 2> AB(dl);
    A.insert(rel<int>({{1, 10}, {2, 20}}));
    B.insert(rel<int>({{10, 100}, {20, 200}, {30, 300}}));

    DL_VARS(x, y, z);
    RULE(dl, AB(x, z) <= A(x, y), B(y, z));
    dl.solve();

    EXPECT_EQ(sorted(AB.extract()),
              (std::vector<std::array<int, 2>>{{1, 100}, {2, 200}}));
}

TEST(DslRecursion, TransitiveClosureLineGraph) {
    Datalog dl;
    Predicate<int, 2> Edge(dl), Path(dl);

    std::vector<std::array<int, 2>> edges = {
        {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}};
    Edge.insert(rel<int>(edges));

    DL_VARS(x, y, z);
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Path(x, y), Edge(y, z));
    dl.solve();

    auto result = sorted(Path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 15u);
}

TEST(DslRecursion, TerminatesOnCycle) {
    Datalog dl;
    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 1}};
    Edge.insert(rel<int>(edges));

    DL_VARS(x, y, z);
    RULE(dl, Edge_rev(y, x) <= Edge(x, y));
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    RULE(dl, Path(x, z) <= Edge_rev(y, x), Path(y, z));
    dl.solve();

    auto result = sorted(Path.extract());
    EXPECT_EQ(result, transitive_closure(edges));
    EXPECT_EQ(result.size(), 9u);
}

TEST(DslRecursion, SelfLoop) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 1}, {1, 2}};
    Edge.insert(rel<int>(edges));

    DL_VARS(x, y, z);
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslRecursion, DisconnectedComponentsDoNotMerge) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    std::vector<std::array<int, 2>> edges = {{1, 2}, {3, 4}};
    Edge.insert(rel<int>(edges));

    DL_VARS(x, y, z);
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslTypes, StringNodes) {
    Datalog dl;
    Predicate<std::string, 2> Edge(dl), Edge_rev(dl), Path(dl);
    Edge.insert(rel<std::string>({{"a", "b"}, {"b", "c"}, {"c", "d"}}));

    DL_VARS(x, y, z);
    RULE(dl, Edge_rev(y, x) <= Edge(x, y));
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    RULE(dl, Path(x, z) <= Edge_rev(y, x), Path(y, z));
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
    Datalog dl;
    Predicate<int, 2> Label(dl);
    Predicate<int, 2> Copy(dl);
    Label.insert(rel<int>({{1, 10}, {2, 20}}));

    DL_VARS(x, y);
    RULE(dl, Copy(x, y) <= Label(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::array<int, 2>>{{1, 10}, {2, 20}}));
}

TEST(DslInvariants, InsertDeduplicates) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Copy(dl);
    Edge.insert(rel<int>({{1, 2}, {1, 2}, {2, 3}, {2, 3}, {2, 3}}));

    DL_VARS(x, y, z);
    RULE(dl, Copy(x, y) <= Edge(x, y));
    dl.solve();

    EXPECT_EQ(Copy.extract().size(), 2u);
}

TEST(DslInvariants, ExtractIsSortedAndUnique) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);
    Edge.insert(rel<int>({{3, 4}, {1, 2}, {2, 3}}));

    DL_VARS(x, y, z);
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    dl.solve();

    auto out = Path.extract();
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()))
        << "Relation must yield tuples in sorted order";
    EXPECT_EQ(std::adjacent_find(out.begin(), out.end()), out.end())
        << "Relation must yield unique tuples";
}

TEST(DslEdgeCases, EmptyRelationStaysEmpty) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);

    DL_VARS(x, y, z);
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    dl.solve();

    EXPECT_TRUE(Path.extract().empty());
}

TEST(DslEdgeCases, SingleEdge) {
    DL_VARS(x, y, z);

    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 2> Path(dl);
    Edge.insert(rel<int>({{5, 6}}));

    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()),
              (std::vector<std::array<int, 2>>{{5, 6}}));
}

TEST(DslEdgeCases, SolveIsIdempotent) {
    DL_VARS(x, y, z);

    Datalog dl;
    Predicate<int, 2> Edge(dl), Edge_rev(dl), Path(dl);
    std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}, {3, 4}};
    Edge.insert(rel<int>(edges));

    RULE(dl, Edge_rev(y, x) <= Edge(x, y));
    RULE(dl, Path(x, y) <= Edge(x, y));
    RULE(dl, Path(x, z) <= Edge(x, y), Path(y, z));
    RULE(dl, Path(x, z) <= Edge_rev(y, x), Path(y, z));

    dl.solve();
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslWcoj, Triangle) {
    DL_VARS(x, y, z);
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 3> Tri(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 1}, {2, 4}, {4, 5}, {5, 2}}));
    RULE(dl, Tri(x, y, z) <= Edge(x, y), Edge(y, z), Edge(z, x));
    dl.solve();

    EXPECT_EQ(
        sorted(Tri.extract()),
        (std::vector<std::array<int, 3>>{
            {1, 2, 3}, {2, 3, 1}, {2, 4, 5}, {3, 1, 2}, {4, 5, 2}, {5, 2, 4}}));
}

TEST(DslWcoj, ThreeHopChain) {
    DL_VARS(x, y, z);
    auto w = Var<3>();
    Datalog dl;
    Predicate<int, 2> Edge(dl), Hop3(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}, {4, 5}}));
    RULE(dl, Hop3(x, w) <= Edge(x, y), Edge(y, z), Edge(z, w));
    dl.solve();
    EXPECT_EQ(sorted(Hop3.extract()),
              (std::vector<std::array<int, 2>>{{1, 4}, {2, 5}}));
}

TEST(DslWcoj, TwoSharedKeysIsIntersection) {
    DL_VARS(x, y);
    Datalog dl;
    Predicate<int, 2> A(dl), B(dl), C(dl);
    A.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    B.insert(rel<int>({{2, 3}, {3, 4}, {9, 9}}));
    RULE(dl, C(x, y) <= A(x, y), B(x, y));
    dl.solve();
    EXPECT_EQ(sorted(C.extract()),
              (std::vector<std::array<int, 2>>{{2, 3}, {3, 4}}));
}

TEST(DslWcoj, NegationEdb) {
    DL_VARS(x, y);
    Datalog dl;
    Predicate<int, 2> Edge(dl), Blocked(dl), Open(dl);
    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    Blocked.insert(rel<int>({{2, 3}}));
    RULE(dl, Open(x, y) <= Edge(x, y), !Blocked(x, y));
    dl.solve();
    EXPECT_EQ(sorted(Open.extract()),
              (std::vector<std::array<int, 2>>{{1, 2}, {3, 4}}));
}

TEST(DslNAry, TernaryPredicateAsSource) {
    DL_VARS(x, y, z);

    Datalog dl;

    Predicate<int, 3> triple(dl);
    Predicate<int, 2> edge(dl), result(dl);

    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));

    RULE(dl, triple(x, y, z) <= edge(x, y), edge(y, z));
    RULE(dl, result(x, z) <= triple(x, y, z));

    dl.solve();

    EXPECT_EQ(sorted(result.extract()),
              (std::vector<std::array<int, 2>>{{{1, 3}, {2, 4}}}));
}

TEST(DslUndirected, TCOnUndirectedGraph) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    DL_VARS(x, y, z);

    dl.make_symmetric(edge);
    RULE(dl, tc(x, y) <= edge(x, y));
    RULE(dl, tc(x, z) <= tc(x, y), edge(y, z));

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
    DL_VARS(x, y);

    RULE(dl, blocked(x, y) <= edge(x, y), heavy(x, y));
    RULE(dl, open(x, y) <= edge(x, y), !blocked(x, y));

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
    DL_VARS(x, y, z);
    RULE(dl, tc(x, y) <= edge(x, y));
    RULE(dl, tc(x, z) <= tc(x, y), edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 4}}));
    dl.solve();

    auto seed = Const<int>({2});

    Datalog query_dl;
    Predicate<int, 2> result(query_dl);
    RULE(query_dl, result(x, y) <= seed(x), tc(x, y));
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
    DL_VARS(x, y);
    RULE(dl, reachable(x, y) <= seed(x), graph(x, y));
    dl.solve();

    EXPECT_EQ(sorted(reachable.extract()), (std::vector<T2>{{1, 2}, {1, 3}}));
}

TEST(DslQuery, SolveIsIdempotentOnNoNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    DL_VARS(x, y, z);
    RULE(dl, tc(x, y) <= edge(x, y));
    RULE(dl, tc(x, z) <= tc(x, y), edge(y, z));
    edge.insert(rel<int>({{1, 2}, {2, 3}}));
    dl.solve();
    dl.solve();
    EXPECT_EQ(tc.peek(),
              (std::vector<std::array<int, 2>>{{1, 2}, {1, 3}, {2, 3}}));
}

TEST(DslQuery, SolveSecondCallPicksUpNewFacts) {
    Datalog dl;
    Predicate<int, 2> edge(dl), tc(dl);
    DL_VARS(x, y, z);
    RULE(dl, tc(x, y) <= edge(x, y));
    RULE(dl, tc(x, z) <= tc(x, y), edge(y, z));
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
    DL_VARS(x, y, z);
    RULE(dl, tc(x, y) <= edge(x, y));
    RULE(dl, tc(x, z) <= tc(x, y), edge(y, z));

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
    DL_VARS(x, y, y_next, res, res_next);

    Datalog dl;

    Predicate<int, 2> succ(dl);
    Predicate<int, 1> num(dl);
    Predicate<int, 3> add(dl);
    Predicate<int, 3> mul(dl);

    auto zero = Const<int>({0});

    // The domain of numbers we'll compute over
    succ.insert(rel<int>({{0, 1}, {1, 2}, {2, 3}, {3, 4}}));

    RULE(dl, num(x) <= zero(x));
    RULE(dl, num(x) <= succ(x, y));
    RULE(dl, num(y) <= succ(x, y));

    RULE(dl, add(x, y, x) <= num(x), zero(y));
    RULE(dl, add(x, y_next, res_next) <= add(x, y, res), succ(y, y_next),
         succ(res, res_next));

    RULE(dl, mul(x, y, y) <= num(x), zero(y));
    RULE(dl, mul(x, y_next, res_next) <= mul(x, y, res), succ(y, y_next),
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

    Predicate<int, 3> Succ(dl);
    Predicate<int, 4> AddProv(dl);
    Predicate<int, 2> StepDep(dl);
    Predicate<int, 2> DependsOn(dl);
    Predicate<int, 1> Num(dl);

    auto zero = Const<int>({0});
    {
        Var<0> n;
        RULE(dl, Num(n) <= zero(n));
    }
    {
        DL_VARS(x, sx, id);
        RULE(dl, Num(sx) <= Succ(x, sx, id));
    }
    {
        DL_VARS(zval, y);
        RULE(dl, AddProv(zval, y, y, zval) <= zero(zval), Num(y));
    }
    {
        DL_VARS(sx, y, sz, x, z, id1, id2, id3);
        RULE(dl, AddProv(sx, y, sz, sx) <= AddProv(x, y, z, id1),
             Succ(x, sx, id2), Succ(z, sz, id3));
    }
    {
        DL_VARS(sx, id1, x, y, z, id2, sz, id3);
        RULE(dl, StepDep(sx, id1) <= AddProv(x, y, z, id1), Succ(x, sx, id2),
             Succ(z, sz, id3));
    }
    {
        DL_VARS(sx, id2, x, y, z, id1, sz, id3);
        RULE(dl, StepDep(sx, id2) <= AddProv(x, y, z, id1), Succ(x, sx, id2),
             Succ(z, sz, id3));
    }
    {
        DL_VARS(sx, id3, x, y, z, id1, id2, sz);
        RULE(dl, StepDep(sx, id3) <= AddProv(x, y, z, id1), Succ(x, sx, id2),
             Succ(z, sz, id3));
    }

    {
        DL_VARS(child, parent);
        RULE(dl, DependsOn(child, parent) <= StepDep(child, parent));
    }
    {
        DL_VARS(child, ancestor, parent);
        RULE(dl, DependsOn(child, ancestor) <= StepDep(child, parent),
             DependsOn(parent, ancestor));
    }

    Succ.insert(df::Relation<T3>::from_vec(
        {{0, 1, 101}, {1, 2, 102}, {2, 3, 103}, {3, 4, 104}, {4, 5, 105}}));
    Succ.commit();

    dl.solve();

    auto add_results = AddProv.extract();
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

    auto lineage_results = DependsOn.extract();
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

    DL_VARS(group, value, total);

    RULE(dl, total_sales(group, total) <= sales(group, value),
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

    DL_VARS(group, value, total);

    // clang-format off
    RULE(dl,
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

TEST(DslRuleSyntax, TriangleTest) {
    Datalog dl;
    Predicate<int, 2> Edge(dl);
    Predicate<int, 3> Tri(dl);

    Edge.insert(rel<int>({{1, 2}, {2, 3}, {3, 1}, {2, 4}, {4, 5}, {5, 2}}));

    DL_VARS(x, y, z);
    RULE(dl, Tri(x, y, z) <= Edge(x, y), Edge(y, z), Edge(z, x));
    dl.solve();

    EXPECT_EQ(
        sorted(Tri.extract()),
        (std::vector<std::array<int, 3>>{
            {1, 2, 3}, {2, 3, 1}, {2, 4, 5}, {3, 1, 2}, {4, 5, 2}, {5, 2, 4}}));
}