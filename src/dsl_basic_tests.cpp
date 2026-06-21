#include <gtest/gtest.h>
#include <dsl/predicate.hpp>

TEST(DatalogTests, BasicEdges) {
    auto x = Var<"x">();
    auto y = Var<"y">();
    auto z = Var<"z">();

    Datalog dl;

    Predicate<int, int> Edge(dl);
    Predicate<int, int> Path(dl);

    Edge.insert(df::Relation<std::pair<int, int>>::from_vec({{1, 2}, {2, 3}, {3, 4}}));

    dl.add_rule( Path(x, y) <<= Edge(x, y) );
    dl.add_rule( Path(x, z) <<= Edge(x, y) && Path(y, z) );

    dl.solve();
    std::vector<std::pair<int, int>> final_paths = Path.extract();

    for (const auto& p : final_paths) {
        std::cout << "(" << p.first << ", " << p.second << ")" << std::endl;
    }

    std::cout << std::endl;

    std::vector<std::pair<int, int>> expected_paths = {
        {1, 2}, {2, 3}, {3, 4},
        {1, 3}, {2, 4},
        {1, 4}
    };

    std::cout << "=== expected paths (" << expected_paths.size() << ") ===" << std::endl;
    for (const auto& p : expected_paths) {
        std::cout << "(" << p.first << ", " << p.second << ")" << std::endl;
    }


    std::sort(final_paths.begin(), final_paths.end());
    std::sort(expected_paths.begin(), expected_paths.end());

    ASSERT_EQ(final_paths.size(), expected_paths.size()) << "Incorrect number of paths found.";
    EXPECT_EQ(final_paths, expected_paths) << "Path tuples do not match expected transitive closure.";
}
