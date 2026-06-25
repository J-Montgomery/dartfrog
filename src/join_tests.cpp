#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <datatoad.hpp>

using namespace dt;

TEST(JoinHelperTest, BothEmpty) {
    std::vector<std::pair<int, int>> a, b;
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    EXPECT_TRUE(results.empty());
}

TEST(JoinHelperTest, OneEmpty) {
    std::vector<std::pair<int, int>> a = {{1, 10}, {2, 20}};
    std::vector<std::pair<int, int>> b = {};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    EXPECT_TRUE(results.empty());
}

TEST(JoinHelperTest, NonOverlappingKeysAllLess) {
    std::vector<std::pair<int, int>> a = {{1, 10}, {2, 20}};
    std::vector<std::pair<int, int>> b = {{3, 30}, {4, 40}};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    EXPECT_TRUE(results.empty());
}

TEST(JoinHelperTest, NonOverlappingKeysAllGreater) {
    std::vector<std::pair<int, int>> a = {{3, 30}, {4, 40}};
    std::vector<std::pair<int, int>> b = {{1, 10}, {2, 20}};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    EXPECT_TRUE(results.empty());
}

TEST(JoinHelperTest, SingleMatchingKey) {
    std::vector<std::pair<int, int>> a = {{1, 10}, {2, 20}};
    std::vector<std::pair<int, int>> b = {{2, 200}, {3, 300}};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], (std::tuple{2, 20, 200}));
}

TEST(JoinHelperTest, MultipleMatchingKeys) {
    std::vector<std::pair<int, int>> a = {{1, 10}, {2, 20}, {3, 30}};
    std::vector<std::pair<int, int>> b = {{2, 200}, {3, 300}, {4, 400}};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], (std::tuple{2, 20, 200}));
    EXPECT_EQ(results[1], (std::tuple{3, 30, 300}));
}

TEST(JoinHelperTest, CartesianProductForDuplicateKeys) {
    std::vector<std::pair<int, std::string>> a = {{1, "a1"}, {1, "a2"}};
    std::vector<std::pair<int, std::string>> b = {
        {1, "b1"}, {1, "b2"}, {1, "b3"}};
    std::vector<std::tuple<int, std::string, std::string>> results;
    dt::join_helper(std::span(a), std::span(b),
                    [&](int k, const std::string &v1, const std::string &v2) {
                        results.emplace_back(k, v1, v2);
                    });
    ASSERT_EQ(results.size(), 6);
    EXPECT_EQ(results[0], (std::tuple{1, "a1", "b1"}));
    EXPECT_EQ(results[1], (std::tuple{1, "a1", "b2"}));
    EXPECT_EQ(results[2], (std::tuple{1, "a1", "b3"}));
    EXPECT_EQ(results[3], (std::tuple{1, "a2", "b1"}));
    EXPECT_EQ(results[4], (std::tuple{1, "a2", "b2"}));
    EXPECT_EQ(results[5], (std::tuple{1, "a2", "b3"}));
}

TEST(JoinHelperTest, MixedOverlappingAndNonOverlapping) {
    std::vector<std::pair<int, int>> a = {{1, 10}, {3, 30}, {5, 50}};
    std::vector<std::pair<int, int>> b = {{2, 200}, {3, 300}, {6, 600}};
    std::vector<std::tuple<int, int, int>> results;
    dt::join_helper(std::span(a), std::span(b), [&](int k, int v1, int v2) {
        results.emplace_back(k, v1, v2);
    });
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], (std::tuple{3, 30, 300}));
}

// antijoin tests
TEST(AntijoinTest, EmptyInput1) {
    Relation<std::pair<int, int>> input1;
    Relation<int> input2 = Relation<int>::from_vec({1, 2, 3});
    auto result = dt::antijoin(input1, input2,
                               [](int k, int v) { return std::pair{k, v}; });
    EXPECT_TRUE(result.elements.empty());
}

TEST(AntijoinTest, EmptyInput2) {
    auto input1 = Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}});
    Relation<int> input2;
    auto result = dt::antijoin(input1, input2,
                               [](int k, int v) { return std::pair{k, v}; });
    EXPECT_EQ(result.elements,
              (std::vector<std::pair<int, int>>{{1, 10}, {2, 20}}));
}

TEST(AntijoinTest, NoKeysInCommon) {
    auto input1 = Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}});
    auto input2 = Relation<int>::from_vec({3, 4, 5});
    auto result = dt::antijoin(input1, input2,
                               [](int k, int v) { return std::pair{k, v}; });
    EXPECT_EQ(result.elements,
              (std::vector<std::pair<int, int>>{{1, 10}, {2, 20}}));
}

TEST(AntijoinTest, SomeKeysInCommon) {
    auto input1 =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}, {3, 30}});
    auto input2 = Relation<int>::from_vec({2});
    auto result = dt::antijoin(input1, input2,
                               [](int k, int v) { return std::pair{k, v}; });
    EXPECT_EQ(result.elements,
              (std::vector<std::pair<int, int>>{{1, 10}, {3, 30}}));
}

TEST(AntijoinTest, AllKeysInCommon) {
    auto input1 = Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}});
    auto input2 = Relation<int>::from_vec({1, 2});
    auto result = dt::antijoin(input1, input2,
                               [](int k, int v) { return std::pair{k, v}; });
    EXPECT_TRUE(result.elements.empty());
}

TEST(AntijoinTest, TransformsValues) {
    auto input1 =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}, {3, 30}});
    auto input2 = Relation<int>::from_vec({2});
    auto result =
        dt::antijoin(input1, input2, [](int k, int v) { return v * 100; });
    EXPECT_EQ(result.elements, (std::vector<int>{1000, 3000}));
}