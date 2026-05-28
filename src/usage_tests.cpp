#include <gtest/gtest.h>

#include "dartfrog.hpp"

TEST(UsageTest, TransitiveClosureOneStep) {
    // edge(1,2), edge(2,3) => path(1,2), path(2,3), path(1,3)
    Iteration iter;
    auto edge = iter.variable<std::pair<int, int>>("edge");
    auto path = iter.variable<std::pair<int, int>>("path");

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
    Iteration iter;
    auto parent = iter.variable<std::pair<std::string, std::string>>("parent");
    auto grandparent =
        iter.variable<std::pair<std::string, std::string>>("grandparent");

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
    Iteration iter;
    auto student = iter.variable<std::pair<int, Unit>>("student");
    auto graduated = iter.variable<int>("graduated");
    auto active = iter.variable<int>("active");

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
    Iteration iter;
    auto parent = iter.variable<std::pair<std::string, std::string>>("parent");
    auto grandparent =
        iter.variable<std::pair<std::string, std::string>>("grandparent");

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
    Iteration iter;
    auto input = iter.variable<int>("input");
    auto output = iter.variable<int>("output");

    input->insert(Relation<int>::from_vec({1, 2, 3}));

    while (iter.changed()) {
        map::map_into(*input, *output, [](int x) { return x * 10; });
    }

    auto result = std::move(*output).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{10, 20, 30}));
}

TEST(UsageTest, MultipleRoundsOfFixedPoint) {
    Iteration iter;
    auto edge = iter.variable<std::pair<int, int>>("edge");
    auto reachable = iter.variable<std::pair<int, Unit>>("reachable");

    edge->insert(
        Relation<std::pair<int, int>>::from_vec({{0, 1}, {1, 2}, {2, 3}}));
    reachable->insert(Relation<std::pair<int, Unit>>::from_vec({{0, {}}}));

    int round = 0;
    while (iter.changed() && round < 10) {
        round++;
        reachable->from_join(*reachable, *edge, *reachable,
                             [](int x, std::monostate _, int y) {
                                 return std::pair{y, std::monostate{}};
                             });
    }

    auto result = std::move(*reachable).complete();
    EXPECT_EQ(result.elements, (std::vector<std::pair<int, Unit>>{
                                   {0, {}}, {1, {}}, {2, {}}, {3, {}}}));
}
