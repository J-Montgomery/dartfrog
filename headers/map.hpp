#pragma once

#include <algorithm>
#include <ranges>
#include <vector>

#include "relation.hpp"
#include "variable.hpp"

namespace map {
template <typename T1, typename T2, typename Logic>
void map_into(const Variable<T1> &input, Variable<T2> &output, Logic &&logic) {
    std::vector<T2> results;
    results.reserve(input.recent().size());
    for (const auto &item : input.recent()) {
        results.push_back(logic(item));
    }

    output.insert(Relation<T2>::from_vec(std::move(results)));
}
} // namespace map