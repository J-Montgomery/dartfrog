#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(GallopTest, EmptySlice) {
    std::vector<int> v = {};
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 5; });
    EXPECT_TRUE(result.empty());
}

TEST(GallopTest, FirstElementDoesNotMatch) {
    std::vector<int> v = {10, 20, 30};
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 5; });
    EXPECT_EQ(result.data(), s.data());
    EXPECT_EQ(result.size(), 3);
}

TEST(GallopTest, AllElementsMatch) {
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 100; });
    EXPECT_EQ(result.size(), 0);
}

TEST(GallopTest, SomeElementsMatch) {
    std::vector<int> v = {1, 2, 3, 10, 20, 30};
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 10; });
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 10);
}

TEST(GallopTest, SingleMatchingElement) {
    std::vector<int> v = {1, 10, 20};
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 10; });
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 10);
}

TEST(GallopTest, ExponentialGallopTriggered) {
    std::vector<int> v;
    for (int i = 0; i < 1000; ++i) v.push_back(i);
    std::span<const int> s(v);
    auto result = join::gallop(s, [](const int &x) { return x < 500; });
    EXPECT_EQ(result.size(), 500);
    EXPECT_EQ(result[0], 500);
}
