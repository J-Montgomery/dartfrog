#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

using namespace df;

TEST(VariableTest, InitiallyStable) {
    Variable<int> v;
    EXPECT_TRUE(v.is_stable());
    EXPECT_TRUE(v.recent().empty());
    EXPECT_EQ(v.num_stable(), 0);
}

TEST(VariableTest, InsertAndChanged) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2, 3}));
    EXPECT_FALSE(v.is_stable());

    bool changed = v.changed();
    EXPECT_TRUE(changed);
    EXPECT_FALSE(v.recent().empty());
    EXPECT_EQ(v.recent().size(), 3);
}

TEST(VariableTest, ChangedReturnsFalseWhenNoNewData) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2}));
    v.changed();

    bool changed = v.changed();
    EXPECT_FALSE(changed);
    EXPECT_TRUE(v.is_stable());
}

TEST(VariableTest, MultipleInsertBatches) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2}));
    v.insert(Relation<int>::from_vec({3, 4}));
    v.changed();

    EXPECT_EQ(v.recent().size(), 4);
}

TEST(VariableTest, DistinctDeduplicatesAgainstStable) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2, 3}));
    v.changed();

    v.insert(Relation<int>::from_vec({2, 3, 4}));
    v.changed();

    EXPECT_EQ(v.recent().size(), 1);
    EXPECT_EQ(v.recent()[0], 4);
}

TEST(VariableTest, IndistinctDoesNotDeduplicate) {
    auto v = std::make_shared<Variable<int>>();
    v->distinct = false;
    v->insert(Relation<int>::from_vec({1, 2, 3}));
    v->changed();

    v->insert(Relation<int>::from_vec({2, 3, 4}));
    v->changed();

    EXPECT_EQ(v->recent().size(), 3);
}

TEST(VariableTest, CompleteWhenStable) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({3, 1, 2}));
    v.changed();
    v.changed();

    auto result = std::move(v).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{1, 2, 3}));
}

TEST(VariableTest, CompleteWhenNotStableThrows) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2}));
    EXPECT_THROW(std::move(v).complete(), std::logic_error);
}

TEST(VariableTest, ForEachStableSetIteratesBatches) {
    Variable<int> v;
    std::vector<int> big(10);
    std::iota(big.begin(), big.end(), 0);
    v.insert(Relation<int>::from_vec(std::move(big)));
    v.changed();
    v.insert(Relation<int>::from_vec({100}));
    v.changed();
    v.changed();

    size_t batches = 0;
    size_t count = 0;
    v.for_each_stable_set([&](std::span<const int> batch) {
        batches++;
        count += batch.size();
    });
    EXPECT_EQ(batches, 2);
    EXPECT_EQ(count, 11);
}

TEST(VariableTest, NumStableAccumulatesAcrossBatches) {
    Variable<int> v;
    v.insert(Relation<int>::from_vec({1, 2}));
    v.changed();
    v.insert(Relation<int>::from_vec({3, 4, 5}));
    v.changed();
    v.changed();

    EXPECT_EQ(v.num_stable(), 5);
}