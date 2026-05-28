#pragma once

#include <algorithm>
#include <concepts>
#include <iterator>
#include <ranges>
#include <vector>

template <std::totally_ordered T>
std::vector<T> merge_unique(std::vector<T> &&a, std::vector<T> &&b) {
    if (a.empty())
        return std::move(b);
    if (b.empty())
        return std::move(a);

    // If a and b don't overlap,
    // do it the easy way
    if (a.back() < b.front()) {
        a.insert(a.end(), std::make_move_iterator(b.begin()),
                 std::make_move_iterator(b.end()));
        return std::move(a);
    }

    if (b.back() < a.front()) {
        b.insert(b.end(), std::make_move_iterator(a.begin()),
                 std::make_move_iterator(a.end()));
        return std::move(b);
    }

    std::vector<T> out;
    out.reserve(a.size() + b.size());

    std::ranges::set_union(
        std::make_move_iterator(a.begin()), std::make_move_iterator(a.end()),
        std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()),
        std::back_inserter(out));

    return out;
}

template <std::totally_ordered Tuple> struct Relation {
    std::vector<Tuple> elements;
    Relation() = default;

    explicit Relation(std::vector<Tuple> &&elems)
        : elements(std::move(elems)) {}

    Relation merge(Relation &&other) && {
        return Relation{
            merge_unique(std::move(this->elements), std::move(other.elements))};
    }

    static Relation from_vec(std::vector<Tuple> &&elems) {
        std::sort(elems.begin(), elems.end());
        auto last = std::unique(elems.begin(), elems.end());
        elems.erase(last, elems.end());
        return Relation{std::move(elems)};
    }

    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, Tuple>
    static Relation from_iter(R &&range) {
        return from_vec(std::vector<Tuple>(
            std::make_move_iterator(std::ranges::begin(range)),
            std::make_move_iterator(std::ranges::end(range))));
    }

    template <typename T2, typename Logic>
    static Relation from_map(const Relation<T2> &input, Logic &&logic) {
        std::vector<Tuple> result;
        result.reserve(input.elements.size());
        for (const auto &item : input.elements) {
            result.push_back(logic(item));
        }
        return from_vec(std::move(result));
    }

    std::optional<size_t> binary_search(const Tuple &target) const {
        auto it =
            std::partition_point(elements.begin(), elements.end(),
                                 [&](const Tuple &x) { return x < target; });
        if (it != elements.end() && *it == target) {
            return std::distance(elements.begin(), it);
        }
        return std::nullopt;
    }
};
