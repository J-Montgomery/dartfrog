#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(LeapjoinTest, SingleExtendWith) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 20}, {2, 30}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    LeaperCollection<int, int, decltype(ew)> collection{std::tuple{ew}};

    std::vector<int> source = {1, 2, 3};
    auto result = leapjoin<int, int, std::pair<int, int>>(source, collection,
        [](int prefix, int val) { return std::pair{prefix, val}; });

    EXPECT_EQ(result.elements, (std::vector<std::pair<int, int>>{
        {1, 10}, {1, 20}, {2, 30}}));
}

TEST(LeapjoinTest, EmptySource) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    LeaperCollection<int, int, decltype(ew)> collection{std::tuple{ew}};

    std::vector<int> source = {};
    auto result = leapjoin<int, int, std::pair<int, int>>(source, collection,
        [](int prefix, int val) { return std::pair{prefix, val}; });

    EXPECT_TRUE(result.elements.empty());
}

TEST(LeapjoinTest, NoMatchesProducesEmpty) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    LeaperCollection<int, int, decltype(ew)> collection{std::tuple{ew}};

    std::vector<int> source = {99};
    auto result = leapjoin<int, int, std::pair<int, int>>(source, collection,
        [](int prefix, int val) { return std::pair{prefix, val}; });

    EXPECT_TRUE(result.elements.empty());
}

TEST(LeapjoinTest, WithPrefixFilter) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}, {3, 30}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    auto pf = filters::prefix_filter<int>(
        [](int x) { return x <= 2; });

    LeaperCollection<int, int, decltype(pf), decltype(ew)> collection{
        std::tuple{pf, ew}};

    std::vector<int> source = {1, 2, 3};
    auto result = leapjoin<int, int, std::pair<int, int>>(source, collection,
        [](int prefix, int val) { return std::pair{prefix, val}; });

    EXPECT_EQ(result.elements, (std::vector<std::pair<int, int>>{
        {1, 10}, {2, 20}}));
}

TEST(LeapjoinTest, WithPassthrough) {
    auto pt = filters::passthrough<int>();
    LeaperCollection<int, Unit, decltype(pt)> collection{std::tuple{pt}};

    std::vector<int> source = {1, 2, 3};
    auto result = leapjoin<int, Unit, int>(source, collection,
        [](int prefix, const Unit &) { return prefix * 10; });

    EXPECT_EQ(result.elements, (std::vector<int>{10, 20, 30}));
}
