#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

using namespace df;

// Prefix Filter tests
TEST(PrefixFilterTest, PredicateTrueReturnsMax) {
    auto pf = filters::prefix_filter<int>([](int x) { return x > 5; });
    EXPECT_EQ(pf.count(10), std::numeric_limits<size_t>::max());
}

TEST(PrefixFilterTest, PredicateFalseReturnsZero) {
    auto pf = filters::prefix_filter<int>([](int x) { return x > 5; });
    EXPECT_EQ(pf.count(3), 0);
}

TEST(PrefixFilterTest, ForEachCountReportsCorrectly) {
    auto pf = filters::prefix_filter<int>([](int x) { return x > 5; });

    size_t idx = 999, cnt = 999;
    pf.for_each_count(10, [&](size_t i, size_t c) {
        idx = i;
        cnt = c;
    });
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(cnt, 1);

    pf.for_each_count(3, [&](size_t i, size_t c) {
        idx = i;
        cnt = c;
    });
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(cnt, 0);
}

// Value Filter Tests

TEST(ValueFilterTest, IntersectRemovesNonMatching) {
    auto vf = filters::value_filter<int, int>(
        [](int prefix, int val) { return val > prefix; });

    int a = 3, b = 7, c = 10;
    std::vector<const int *> values = {&a, &b, &c};
    vf.intersect(5, values);

    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(*values[0], 7);
    EXPECT_EQ(*values[1], 10);
}

TEST(ValueFilterTest, IntersectRemovesAll) {
    auto vf = filters::value_filter<int, int>(
        [](int prefix, int val) { return val > 100; });

    int a = 3, b = 7;
    std::vector<const int *> values = {&a, &b};
    vf.intersect(0, values);

    EXPECT_TRUE(values.empty());
}

TEST(ValueFilterTest, CountReturnsMax) {
    auto vf = filters::value_filter<int, int>([](int, int) { return true; });

    EXPECT_EQ(vf.count(0), std::numeric_limits<size_t>::max());
}

// Passthrough Filter Tests

TEST(PassthroughTest, CountIsAlwaysOne) {
    auto pt = filters::passthrough<int>();
    EXPECT_EQ(pt.count(42), 1);
}

TEST(PassthroughTest, ProposePushesUnit) {
    auto pt = filters::passthrough<int>();
    std::vector<const Unit *> values;
    pt.propose(0, values);
    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(values[0], &UNIT_INSTANCE);
}

TEST(PassthroughTest, IntersectIsNoop) {
    auto pt = filters::passthrough<int>();
    std::vector<const Unit *> values = {&UNIT_INSTANCE};
    pt.intersect(0, values);
    EXPECT_EQ(values.size(), 1);
}

// FilterWith Tests

TEST(FilterWithTest, CountReturnsMaxWhenPresent) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}});
    RelationLeaper<int, int> rl{&rel};

    auto fw = rl.filter_with<int>([](int x) { return std::pair{x, x * 10}; });

    EXPECT_EQ(fw.count(1), std::numeric_limits<size_t>::max());
}

TEST(FilterWithTest, CountReturnsZeroWhenAbsent) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto fw = rl.filter_with<int>([](int x) { return std::pair{x, x * 100}; });

    EXPECT_EQ(fw.count(1), 0);
}

// FilterAnti Tests

TEST(FilterAntiTest, CountReturnsZeroWhenPresent) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto fa = rl.filter_anti<int>([](int x) { return std::pair{x, x * 10}; });

    EXPECT_EQ(fa.count(1), 0);
}

TEST(FilterAntiTest, CountReturnsMaxWhenAbsent) {
    auto rel = Relation<std::pair<int, int>>::from_vec({{1, 10}});
    RelationLeaper<int, int> rl{&rel};

    auto fa = rl.filter_anti<int>([](int x) { return std::pair{x, x * 100}; });

    EXPECT_EQ(fa.count(1), std::numeric_limits<size_t>::max());
}
