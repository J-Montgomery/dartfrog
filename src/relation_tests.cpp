#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(RelationTest, FromVecHandlesDuplicatesAndSorting) {
    std::vector<int> input = {5, 1, 3, 3, 1, 5};
    auto rel = Relation<int>::from_vec(std::move(input));

    std::vector<int> expected = {1, 3, 5};
    EXPECT_EQ(rel.elements, expected);
}

TEST(RelationTest, MergeTwoRelations) {
    auto rel1 = Relation<int>::from_vec({1, 2, 3});
    auto rel2 = Relation<int>::from_vec({3, 4, 5});

    auto result = std::move(rel1).merge(std::move(rel2));

    std::vector<int> expected = {1, 2, 3, 4, 5};
    EXPECT_EQ(result.elements, expected);
}

TEST(RelationTest, FromMapTransformation) {
    auto rel_ints = Relation<int>::from_vec({1, 2, 3});

    auto rel_strs = Relation<std::string>::from_map(
        rel_ints, [](int i) { return "val" + std::to_string(i); });

    std::vector<std::string> expected = {"val1", "val2", "val3"};
    EXPECT_EQ(rel_strs.elements, expected);
}

TEST(RelationTest, FromIterMoveSemantics) {
    std::vector<std::string> sources = {"apple", "banana"};

    auto rel = Relation<std::string>::from_iter(sources);

    EXPECT_EQ(rel.elements.size(), 2);
    EXPECT_EQ(rel.elements[0], "apple");
    EXPECT_TRUE(sources[0].empty());
}
