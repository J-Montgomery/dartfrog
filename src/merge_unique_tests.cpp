#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <datatoad.hpp>

using namespace dt;

TEST(MergeUniqueTest, HandlesEmptyVectors) {
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{1, 2, 3}));
}

TEST(MergeUniqueTest, NonOverlappingAFirst) {
    std::vector<int> a = {1, 2};
    std::vector<int> b = {3, 4};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{1, 2, 3, 4}));
}

TEST(MergeUniqueTest, NonOverlappingBFirst) {
    std::vector<int> a = {10, 20};
    std::vector<int> b = {1, 2};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{1, 2, 10, 20}));
}

TEST(MergeUniqueTest, MergesWithOverlapAndUniqueness) {
    std::vector<int> a = {1, 3, 5};
    std::vector<int> b = {2, 3, 4, 6};

    auto result = merge_unique(std::move(a), std::move(b));
    std::vector<int> expected = {1, 2, 3, 4, 5, 6};
    EXPECT_EQ(result, expected);
}

TEST(MergeUniqueTest, MoveOnlyTypes) {
    std::vector<std::unique_ptr<int>> a;
    a.push_back(std::make_unique<int>(1));

    std::vector<std::unique_ptr<int>> b;
    b.push_back(std::make_unique<int>(2));

    auto result = merge_unique(std::move(a), std::move(b));
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(*result[0], 1);
    EXPECT_EQ(*result[1], 2);
}

TEST(MergeUniqueTest, BothEmpty) {
    std::vector<int> a = {};
    std::vector<int> b = {};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_TRUE(result.empty());
}

TEST(MergeUniqueTest, IdenticalVectors) {
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {1, 2, 3};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{1, 2, 3}));
}

TEST(MergeUniqueTest, InterleavedElements) {
    std::vector<int> a = {1, 3, 5, 7};
    std::vector<int> b = {2, 4, 6, 8};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(MergeUniqueTest, SingleElementOverlap) {
    std::vector<int> a = {5};
    std::vector<int> b = {5};

    auto result = merge_unique(std::move(a), std::move(b));
    EXPECT_EQ(result, (std::vector<int>{5}));
}