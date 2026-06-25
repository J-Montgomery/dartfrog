#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <datatoad.hpp>

using namespace dt;

// ExtendWith tests
TEST(ExtendWithTest, CountReturnsMatchingRows) {
    auto rel =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 20}, {2, 30}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });

    EXPECT_EQ(ew.count(1), 2);
    EXPECT_EQ(ew.count(2), 1);
    EXPECT_EQ(ew.count(3), 0);
}

TEST(ExtendWithTest, ProposeCollectsValues) {
    auto rel =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 20}, {2, 30}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    ew.count(1);

    std::vector<const int *> values;
    ew.propose(1, values);
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(*values[0], 10);
    EXPECT_EQ(*values[1], 20);
}

TEST(ExtendWithTest, IntersectFiltersValues) {
    auto rel =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 30}, {1, 50}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    ew.count(1);

    int v1 = 10, v2 = 30, v3 = 40;
    std::vector<const int *> values = {&v1, &v2, &v3};
    ew.intersect(1, values);

    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(*values[0], 10);
    EXPECT_EQ(*values[1], 30);
}

TEST(ExtendWithTest, CacheIsReusedForSameKey) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 20}});
    RelationLeaper<int, int> rl{&rel};

    auto ew = rl.extend_with<int>([](int x) { return x; });
    size_t c1 = ew.count(1);
    size_t c2 = ew.count(1);
    EXPECT_EQ(c1, 2);
    EXPECT_EQ(c2, 2);
}

// ExtendAnti tests

TEST(ExtendAntiTest, IntersectRemovesMatchingValues) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}, {1, 30}});
    RelationLeaper<int, int> rl{&rel};

    auto ea = rl.extend_anti<int>([](int x) { return x; });

    int v1 = 10, v2 = 20, v3 = 30;
    std::vector<const int *> values = {&v1, &v2, &v3};
    ea.intersect(1, values);

    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(*values[0], 20);
}

TEST(ExtendAntiTest, IntersectKeepsAllWhenNoMatch) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto ea = rl.extend_anti<int>([](int x) { return x; });

    int v1 = 20, v2 = 30;
    std::vector<const int *> values = {&v1, &v2};
    ea.intersect(1, values);

    ASSERT_EQ(values.size(), 2);
}

TEST(ExtendAntiTest, IntersectEmptyValuesIsNoop) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto ea = rl.extend_anti<int>([](int x) { return x; });

    std::vector<const int *> values;
    ea.intersect(1, values);
    EXPECT_TRUE(values.empty());
}
