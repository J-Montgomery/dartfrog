#include <gtest/gtest.h>

#include "dartfrog.hpp"

using namespace df;

TEST(UsageTest, TransitiveClosureOneStep) {
    // edge(1,2), edge(2,3) => path(1,2), path(2,3), path(1,3)
    auto [iter1, edge] = Iteration{}.variable<std::pair<int, int>>();
    auto [iter, path] = std::move(iter1).variable<std::pair<int, int>>();

    // Seed edges
    edge->insert(Relation<std::pair<int, int>>::from_vec({{1, 2}, {2, 3}}));

    while (iter.changed()) {
        // path(x, z) :- edge(x, z)
        path->from_join(*edge, *edge, *path,
                        [](int x, int z, int _) { return std::pair{x, z}; });
    }

    auto result = std::move(*path).complete();
    EXPECT_EQ(result.elements,
              (std::vector<std::pair<int, int>>{{1, 2}, {2, 3}}));
}

TEST(UsageTest, JoinTwoRelations) {
    // parent(john, mary), parent(mary, ann)
    // grandparent(X, Z) :- parent(X, Y), parent(Y, Z)
    auto [iter1, parent] =
        Iteration{}.variable<std::pair<std::string, std::string>>();
    auto [iter, grandparent] =
        std::move(iter1).variable<std::pair<std::string, std::string>>();

    parent->insert(Relation<std::pair<std::string, std::string>>::from_vec(
        {{"john", "mary"}, {"mary", "ann"}, {"bob", "carol"}}));

    while (iter.changed()) {
        RelationLeaper<std::string, std::string> parent_rl{
            &parent->recent_data};

        auto ext = parent_rl.extend_with<std::pair<std::string, std::string>>(
            [](const std::pair<std::string, std::string> &p) {
                return p.second;
            });

        LeaperCollection<std::pair<std::string, std::string>, std::string,
                         decltype(ext)>
            leapers{std::tuple{std::move(ext)}};

        grandparent->from_leapjoin(
            *parent, leapers,
            [](const std::pair<std::string, std::string> &p,
               const std::string &z) { return std::pair{p.first, z}; });
    }

    auto result = std::move(*grandparent).complete();
    EXPECT_EQ(
        result.elements,
        (std::vector<std::pair<std::string, std::string>>{{"john", "ann"}}));
}

TEST(UsageTest, AntijoinExample) {
    auto [iter1, student] = Iteration{}.variable<std::pair<int, Unit>>();
    auto [iter2, graduated] = std::move(iter1).variable<int>();
    auto [iter, active] = std::move(iter2).variable<int>();

    student->insert(
        Relation<std::pair<int, Unit>>::from_vec({{1, {}}, {2, {}}, {3, {}}}));
    graduated->insert(Relation<int>::from_vec({2}));

    while (iter.changed()) {
        active->from_antijoin(*student, graduated->recent_data,
                              [](int x, Unit) { return x; });
    }

    auto result = std::move(*active).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{1, 3}));
}

TEST(UsageTest, LeapjoinExample) {
    // parent(john, mary), parent(mary, ann)
    // grandparent(X, Z) :- parent(X, Y), parent(Y, Z)
    auto [iter1, parent] =
        Iteration{}.variable<std::pair<std::string, std::string>>();
    auto [iter, grandparent] =
        std::move(iter1).variable<std::pair<std::string, std::string>>();

    parent->insert(Relation<std::pair<std::string, std::string>>::from_vec(
        {{"john", "mary"}, {"mary", "ann"}, {"bob", "carol"}}));

    while (iter.changed()) {
        RelationLeaper<std::string, std::string> parent_rl{
            &parent->recent_data};

        auto ew = parent_rl.extend_with<std::pair<std::string, std::string>>(
            [](const std::pair<std::string, std::string> &p) {
                return p.second;
            });

        LeaperCollection<std::pair<std::string, std::string>, std::string,
                         decltype(ew)>
            leapers{std::tuple{std::move(ew)}};

        grandparent->from_leapjoin(
            *parent, leapers,
            [](const std::pair<std::string, std::string> &p,
               const std::string &z) { return std::pair{p.first, z}; });
    }

    auto result = std::move(*grandparent).complete();
    EXPECT_EQ(
        result.elements,
        (std::vector<std::pair<std::string, std::string>>{{"john", "ann"}}));
}

TEST(UsageTest, MapInto) {
    auto [iter1, input] = Iteration{}.variable<int>();
    auto [iter, output] = std::move(iter1).variable<int>();

    input->insert(Relation<int>::from_vec({1, 2, 3}));

    while (iter.changed()) {
        map_into(*input, *output, [](int x) { return x * 10; });
    }

    auto result = std::move(*output).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{10, 20, 30}));
}

TEST(UsageTest, MultipleRoundsOfFixedPoint) {
    auto [iter1, edge] = Iteration{}.variable<std::pair<int, int>>();
    auto [iter, reachable] = std::move(iter1).variable<std::pair<int, Unit>>();

    edge->insert(
        Relation<std::pair<int, int>>::from_vec({{0, 1}, {1, 2}, {2, 3}}));
    reachable->insert(Relation<std::pair<int, Unit>>::from_vec({{0, {}}}));

    int round = 0;
    while (iter.changed() && round < 10) {
        round++;
        reachable->from_join(
            *reachable, *edge, *reachable,
            [](int x, df::Unit _, int y) { return std::pair{y, df::Unit{}}; });
    }

    auto result = std::move(*reachable).complete();
    EXPECT_EQ(result.elements, (std::vector<std::pair<int, Unit>>{
                                   {0, {}}, {1, {}}, {2, {}}, {3, {}}}));
}
