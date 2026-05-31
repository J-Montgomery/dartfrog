#include <gtest/gtest.h>

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "query.hpp"

TEST(QueryTest, SelectAllNoOps) {
    Relation<int> source = Relation<int>::from_vec({3, 1, 2});
    Relation<int> result = query(std::move(source)).execute();

    EXPECT_EQ(result.elements, (std::vector<int>{1, 2, 3}));
}

TEST(QueryTest, WhereFilter) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3, 4, 5});
    Relation<int> result = query(std::move(source))
                               .where([](const int &x) { return x > 3; })
                               .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{4, 5}));
}