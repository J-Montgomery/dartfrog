#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <datalog.hpp>

using namespace df;
using namespace df::datalog;

int main() {
    std::ifstream ins("insert.txt");
    if (!ins) {
        std::cerr << "Failed to open insert.txt" << std::endl;
        return 1;
    }

    std::vector<std::array<int, 4>> insert_vec;
    std::set<std::pair<int, int>> unique_ids;
    int a, b, c, d;
    while (ins >> a >> b >> c >> d) {
        insert_vec.push_back({a, b, c, d});
        unique_ids.insert({a, b});
        unique_ids.insert({c, d});
    }

    std::ifstream rem("remove.txt");
    std::vector<std::array<int, 2>> remove_vec;
    if (rem) {
        while (rem >> a >> b) {
            remove_vec.push_back({a, b});
            unique_ids.insert({a, b});
        }
    }

    std::vector<std::array<int, 4>> id_gt_vec;
    for (const auto& id1 : unique_ids) {
        for (const auto& id2 : unique_ids) {
            if (id1.first > id2.first || (id1.first == id2.first && id1.second > id2.second)) {
                id_gt_vec.push_back({id1.first, id1.second, id2.first, id2.second});
            }
        }
    }

    Datalog dl1;

    Predicate<int, 4> insert_rel(dl1), id_gt(dl1), laterChild(dl1);
    Predicate<int, 4> sibling(dl1), laterSibling(dl1), laterSibling2(dl1);
    Predicate<int, 2> hasChild(dl1), hasNextSibling(dl1);

    insert_rel.insert(Relation<std::array<int, 4>>::from_vec(std::move(insert_vec)));
    id_gt.insert(Relation<std::array<int, 4>>::from_vec(std::move(id_gt_vec)));

    // hasChild(Parent) :- insert(_, Parent).
    dl1.add_rule(hasChild(Var<0>{}, Var<1>{}) %= 
        insert_rel(Var<2>{}, Var<3>{}, Var<0>{}, Var<1>{}));

    // laterChild(Parent, Child2) :- insert(Child1, Parent), insert(Child2, Parent), Child1 > Child2.
    dl1.add_rule(laterChild(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        insert_rel(Var<4>{}, Var<5>{}, Var<0>{}, Var<1>{}) &&
        insert_rel(Var<2>{}, Var<3>{}, Var<0>{}, Var<1>{}) &&
        id_gt(Var<4>{}, Var<5>{}, Var<2>{}, Var<3>{}));

    // sibling(Child1, Child2) :- insert(Child1, Parent), insert(Child2, Parent).
    dl1.add_rule(sibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        insert_rel(Var<0>{}, Var<1>{}, Var<4>{}, Var<5>{}) &&
        insert_rel(Var<2>{}, Var<3>{}, Var<4>{}, Var<5>{}));

    // laterSibling(Sib1, Sib2) :- sibling(Sib1, Sib2), Sib1 > Sib2.
    dl1.add_rule(laterSibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        sibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) &&
        id_gt(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    // laterSibling2(Sib1, Sib3) :- sibling(Sib1, Sib2), sibling(Sib1, Sib3), Sib1 > Sib2, Sib2 > Sib3.
    dl1.add_rule(laterSibling2(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        sibling(Var<0>{}, Var<1>{}, Var<4>{}, Var<5>{}) &&
        sibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) &&
        id_gt(Var<0>{}, Var<1>{}, Var<4>{}, Var<5>{}) &&
        id_gt(Var<4>{}, Var<5>{}, Var<2>{}, Var<3>{}));

    // hasNextSibling(Sib1) :- laterSibling(Sib1, _).
    dl1.add_rule(hasNextSibling(Var<0>{}, Var<1>{}) %=
        laterSibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    dl1.solve();


    auto ext_insert        = insert_rel.extract();
    auto ext_laterChild    = laterChild.extract();
    auto ext_laterSibling  = laterSibling.extract();
    auto ext_laterSibling2 = laterSibling2.extract();
    auto ext_hasNextSibling= hasNextSibling.extract();
    auto ext_hasChild      = hasChild.extract();

    std::set<std::array<int, 4>> set_laterChild(ext_laterChild.begin(), ext_laterChild.end());
    std::set<std::array<int, 4>> set_laterSibling2(ext_laterSibling2.begin(), ext_laterSibling2.end());
    std::set<std::array<int, 2>> set_hasNextSibling(ext_hasNextSibling.begin(), ext_hasNextSibling.end());
    std::set<std::array<int, 2>> set_hasChild(ext_hasChild.begin(), ext_hasChild.end());

    std::vector<std::array<int, 4>> firstChild_vec, nextSibling_vec, lastChildInParent_vec;
    std::vector<std::array<int, 2>> leafNode_vec;

    for (const auto& i : ext_insert) {
        std::array<int, 4> lc_tuple = {i[2], i[3], i[0], i[1]}; // Parent, Child
        if (set_laterChild.find(lc_tuple) == set_laterChild.end()) {
            firstChild_vec.push_back(lc_tuple);
        }
        std::array<int, 2> start_node = {i[0], i[1]};
        if (set_hasNextSibling.find(start_node) == set_hasNextSibling.end()) {
            lastChildInParent_vec.push_back({i[0], i[1], i[2], i[3]}); // Start, Parent
        }
    }

    // nextSibling = laterSibling \ laterSibling2
    for (const auto& ls : ext_laterSibling) {
        if (set_laterSibling2.find(ls) == set_laterSibling2.end()) {
            nextSibling_vec.push_back(ls);
        }
    }

    // leafNode = unique nodes \ hasChild
    for (const auto& id : unique_ids) {
        if (set_hasChild.find({id.first, id.second}) == set_hasChild.end()) {
            leafNode_vec.push_back({id.first, id.second});
        }
    }

    std::set<std::array<int, 2>> set_remove(remove_vec.begin(), remove_vec.end());
    std::vector<std::array<int, 3>> currentValue_vec;
    std::set<std::array<int, 2>> set_hasValue;

    for (const auto& i : ext_insert) {
        std::array<int, 5> a = {i[0], i[1], i[0], i[1], 1};
        std::array<int, 2> id = {a[0], a[1]};
        if (set_remove.find(id) == set_remove.end()) {
            currentValue_vec.push_back({a[2], a[3], a[4]});
            set_hasValue.insert({a[2], a[3]});
        }
    }

    std::vector<std::array<int, 2>> notHasValue_vec;
    for (const auto& id : unique_ids) {
        if (set_hasValue.find({id.first, id.second}) == set_hasValue.end()) {
            notHasValue_vec.push_back({id.first, id.second});
        }
    }
    std::vector<std::array<int, 2>> hasValue_vec(set_hasValue.begin(), set_hasValue.end());

    Datalog dl2;

    Predicate<int, 4> firstChild(dl2), nextSibling(dl2), nextSiblingAnc(dl2), nextElem(dl2);
    Predicate<int, 4> lastChildInParent(dl2), skipBlank(dl2), nextVisible(dl2);
    Predicate<int, 3> currentValue(dl2), result_rel(dl2);
    Predicate<int, 2> leafNode(dl2), hasValue(dl2), notHasValue(dl2);

    firstChild.insert(Relation<std::array<int, 4>>::from_vec(std::move(firstChild_vec)));
    nextSibling.insert(Relation<std::array<int, 4>>::from_vec(std::move(nextSibling_vec)));
    lastChildInParent.insert(Relation<std::array<int, 4>>::from_vec(std::move(lastChildInParent_vec)));
    leafNode.insert(Relation<std::array<int, 2>>::from_vec(std::move(leafNode_vec)));
    currentValue.insert(Relation<std::array<int, 3>>::from_vec(std::move(currentValue_vec)));
    hasValue.insert(Relation<std::array<int, 2>>::from_vec(std::move(hasValue_vec)));
    notHasValue.insert(Relation<std::array<int, 2>>::from_vec(std::move(notHasValue_vec)));

    // nextSiblingAnc(Start, Next) :- nextSibling(Start, Next).
    dl2.add_rule(nextSiblingAnc(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %= 
        nextSibling(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    // nextSiblingAnc(Start, Next) :- lastChildInParent(Start, Parent), nextSiblingAnc(Parent, Next).
    dl2.add_rule(nextSiblingAnc(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        lastChildInParent(Var<0>{}, Var<1>{}, Var<4>{}, Var<5>{}) &&
        nextSiblingAnc(Var<4>{}, Var<5>{}, Var<2>{}, Var<3>{}));

    // nextElem(Prev, Next) :- firstChild(Prev, Next).
    dl2.add_rule(nextElem(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        firstChild(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    // nextElem(Prev, Next) :- leafNode(Prev), nextSiblingAnc(Prev, Next).
    dl2.add_rule(nextElem(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        leafNode(Var<0>{}, Var<1>{}) &&
        nextSiblingAnc(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    // skipBlank(From, To) :- nextElem(From, To).
    dl2.add_rule(skipBlank(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        nextElem(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}));

    // skipBlank(From, To) :- nextElem(From, Via), notHasValue(Via), skipBlank(Via, To).
    dl2.add_rule(skipBlank(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        nextElem(Var<0>{}, Var<1>{}, Var<4>{}, Var<5>{}) &&
        notHasValue(Var<4>{}, Var<5>{}) &&
        skipBlank(Var<4>{}, Var<5>{}, Var<2>{}, Var<3>{}));

    // nextVisible(Prev, Next) :- hasValue(Prev), skipBlank(Prev, Next), hasValue(Next).
    dl2.add_rule(nextVisible(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) %=
        hasValue(Var<0>{}, Var<1>{}) &&
        skipBlank(Var<0>{}, Var<1>{}, Var<2>{}, Var<3>{}) &&
        hasValue(Var<2>{}, Var<3>{}));

    // result(ctr1, ctr2, value) :- nextVisible([ctr1, _], [ctr2, node2]), currentValue([ctr2, node2], value).
    dl2.add_rule(result_rel(Var<0>{}, Var<1>{}, Var<2>{}) %=
        nextVisible(Var<0>{}, Var<3>{}, Var<1>{}, Var<4>{}) &&
        currentValue(Var<1>{}, Var<4>{}, Var<2>{}));

    dl2.solve();

    auto res_tuples = result_rel.extract();
    std::ofstream out("result_dsl.csv");
    for (const auto& t : res_tuples) {
        std::string str_val = (t[2] == 1) ? "hi" : "unknown";
        out << t[0] << "," << t[1] << "," << str_val << "\n";
    }

    return 0;
}