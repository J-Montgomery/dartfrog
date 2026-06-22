#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <dsl/predicate.hpp>

namespace {

template <class T1, class T2>
df::Relation<std::pair<T1, T2>> rel(std::vector<std::pair<T1, T2>> v) {
    return df::Relation<std::pair<T1, T2>>::from_vec(std::move(v));
}

template <class T>
std::vector<T> sorted(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<std::pair<int, int>>
transitive_closure(const std::vector<std::pair<int, int>> &edges) {
    std::set<std::pair<int, int>> tc(edges.begin(), edges.end());
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<std::pair<int, int>> snap(tc.begin(), tc.end());
        for (const auto &[a, b] : snap)
            for (const auto &[c, d] : snap)
                if (b == c && tc.insert({a, d}).second)
                    changed = true;
    }
    return {tc.begin(), tc.end()};
}
}

TEST(DatalogTests, BasicEdges) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;

    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    Edge.insert(
        df::Relation<std::pair<int, int>>::from_vec({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));

    dl.solve();
    std::vector<std::pair<int, int>> final_paths = Path.extract();

    for (const auto &p : final_paths) {
        std::cout << "(" << p.first << ", " << p.second << ")" << std::endl;
    }

    std::cout << std::endl;

    std::vector<std::pair<int, int>> expected_paths = {{1, 2}, {2, 3}, {3, 4},
                                                       {1, 3}, {2, 4}, {1, 4}};

    std::cout << "=== expected paths (" << expected_paths.size()
              << ") ===" << std::endl;
    for (const auto &p : expected_paths) {
        std::cout << "(" << p.first << ", " << p.second << ")" << std::endl;
    }

    std::sort(final_paths.begin(), final_paths.end());
    std::sort(expected_paths.begin(), expected_paths.end());

    ASSERT_EQ(final_paths.size(), expected_paths.size())
        << "Incorrect number of paths found.";
    EXPECT_EQ(final_paths, expected_paths)
        << "Path tuples do not match expected transitive closure.";
}

TEST(DslSingleTerm, DirectCopyIsIdentity) {
    auto x = Var<"x">();
    auto y = Var<"y">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Copy(dl);
    Edge.insert(rel<int, int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Copy(x, y) <<= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::pair<int, int>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(DslSingleTerm, SwapReversesTuples) {
    auto x = Var<"x">();
    auto y = Var<"y">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Rev(dl);
    Edge.insert(rel<int, int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(Rev(y, x) <<= Edge(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Rev.extract()),
              (std::vector<std::pair<int, int>>{{2, 1}, {3, 2}, {4, 3}}));
}

TEST(DslJoin, SelfJoinTwoHopForwardHead) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> TwoHop(dl);
    Edge.insert(rel<int, int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(TwoHop(x, z) <<= Edge(x, y) && Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHop.extract()),
              (std::vector<std::pair<int, int>>{{1, 3}, {2, 4}}));
}

TEST(DslJoin, SelfJoinTwoHopReversedHead) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> TwoHopRev(dl);
    Edge.insert(rel<int, int>({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule(TwoHopRev(z, x) <<= Edge(x, y) && Edge(y, z));
    dl.solve();

    EXPECT_EQ(sorted(TwoHopRev.extract()),
              (std::vector<std::pair<int, int>>{{3, 1}, {4, 2}}));
}

TEST(DslJoin, JoinAcrossTwoDistinctPredicates) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> A(dl);
    Predicate<int, int> B(dl);
    Predicate<int, int> AB(dl);
    A.insert(rel<int, int>({{1, 10}, {2, 20}}));
    B.insert(rel<int, int>({{10, 100}, {20, 200}, {30, 300}}));

    dl.add_rule(AB(x, z) <<= A(x, y) && B(y, z));
    dl.solve();

    EXPECT_EQ(sorted(AB.extract()),
              (std::vector<std::pair<int, int>>{{1, 100}, {2, 200}}));
}

TEST(DslRecursion, TransitiveClosureLineGraph) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    std::vector<std::pair<int, int>> edges = {{1, 2}, {2, 3}, {3, 4},
                                              {4, 5}, {5, 6}};
    Edge.insert(rel<int, int>(edges));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslRecursion, TerminatesOnCycle) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    std::vector<std::pair<int, int>> edges = {{1, 2}, {2, 3}, {3, 1}};
    Edge.insert(rel<int, int>(edges));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
    EXPECT_EQ(Path.extract().size(), 0u);
}

TEST(DslRecursion, SelfLoop) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    std::vector<std::pair<int, int>> edges = {{1, 1}, {1, 2}};
    Edge.insert(rel<int, int>(edges));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslRecursion, DisconnectedComponentsDoNotMerge) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    std::vector<std::pair<int, int>> edges = {{1, 2}, {3, 4}};
    Edge.insert(rel<int, int>(edges));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}

TEST(DslTypes, StringNodes) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<std::string, std::string> Edge(dl);
    Predicate<std::string, std::string> Path(dl);
    Edge.insert(rel<std::string, std::string>(
        {{"a", "b"}, {"b", "c"}, {"c", "d"}}));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()),
              (std::vector<std::pair<std::string, std::string>>{
                  {"a", "b"}, {"a", "c"}, {"a", "d"},
                  {"b", "c"}, {"b", "d"}, {"c", "d"}}));
}

TEST(DslTypes, HeterogeneousColumns) {
    auto x = Var<"x">();
    auto y = Var<"y">();

    Datalog dl;
    Predicate<int, std::string> Label(dl);
    Predicate<int, std::string> Copy(dl);
    Label.insert(rel<int, std::string>({{1, "one"}, {2, "two"}}));

    dl.add_rule(Copy(x, y) <<= Label(x, y));
    dl.solve();

    EXPECT_EQ(sorted(Copy.extract()),
              (std::vector<std::pair<int, std::string>>{{1, "one"}, {2, "two"}}));
}

TEST(DslInvariants, InsertDeduplicates) {
    auto x = Var<"x">();
    auto y = Var<"y">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Copy(dl);
    Edge.insert(rel<int, int>({{1, 2}, {1, 2}, {2, 3}, {2, 3}, {2, 3}}));

    dl.add_rule(Copy(x, y) <<= Edge(x, y));
    dl.solve();

    EXPECT_EQ(Copy.extract().size(), 2u);
}

TEST(DslInvariants, ExtractIsSortedAndUnique) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);
    Edge.insert(rel<int, int>({{3, 4}, {1, 2}, {2, 3}}));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    auto out = Path.extract();
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()))
        << "Relation must yield tuples in sorted order";
    EXPECT_EQ(std::adjacent_find(out.begin(), out.end()), out.end())
        << "Relation must yield unique tuples";
}

TEST(DslEdgeCases, EmptyRelationStaysEmpty) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_TRUE(Path.extract().empty());
}

TEST(DslEdgeCases, SingleEdge) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);
    Edge.insert(rel<int, int>({{5, 6}}));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()),
              (std::vector<std::pair<int, int>>{{5, 6}}));
}

TEST(DslEdgeCases, SolveIsIdempotent) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;
    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);
    std::vector<std::pair<int, int>> edges = {{1, 2}, {2, 3}, {3, 4}};
    Edge.insert(rel<int, int>(edges));

    dl.add_rule(Path(x, y) <<= Edge(x, y));
    dl.add_rule(Path(x, z) <<= Edge(x, y) && Path(y, z));

    dl.solve();
    dl.solve();

    EXPECT_EQ(sorted(Path.extract()), transitive_closure(edges));
}
