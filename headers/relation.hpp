#pragma once

#include <algorithm>
#include <concepts>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace df {

template <std::totally_ordered T>
constexpr std::vector<T> merge_unique(std::vector<T> &&a, std::vector<T> &&b) {
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

template <typename T, typename Cmp>
constexpr std::span<T> seek(std::span<T> slice, Cmp &&cmp) {
    if (slice.empty() || !cmp(slice[0])) {
        return slice;
    }

    size_t len = slice.size();
    size_t lower = 0;
    size_t step = 1;

    while (lower + step < len && cmp(slice[lower + step])) {
        lower += step;
        step <<= 1;
    }

    size_t upper = std::min(len, lower + step + 1);

    auto first = slice.begin() + lower;
    auto last = slice.begin() + upper;

    auto it = std::partition_point(first, last, std::forward<Cmp>(cmp));
    return slice.subspan(std::distance(slice.begin(), it));
}

template <typename T, typename Key, typename Proj>
constexpr std::span<T> key_range(std::span<T> s, const Key &key, Proj proj) {
    s = seek(s, [&](const auto &x) { return proj(x) < key; });
    auto end = seek(s, [&](const auto &x) { return proj(x) <= key; });
    return s.subspan(0, s.size() - end.size());
}

template <std::totally_ordered Tuple> struct Relation {
    std::vector<Tuple> elements;
    constexpr Relation() = default;

    constexpr explicit Relation(std::vector<Tuple> &&elems)
        : elements(std::move(elems)) {}

    constexpr Relation merge(Relation &&other) && {
        return Relation{
            merge_unique(std::move(this->elements), std::move(other.elements))};
    }
    constexpr auto begin() const { return elements.begin(); }
    constexpr auto end() const { return elements.end(); }
    constexpr auto begin() { return elements.begin(); }
    constexpr auto end() { return elements.end(); }

    static constexpr Relation from_vec(std::vector<Tuple> &&elems) {
        std::sort(elems.begin(), elems.end());
        auto last = std::unique(elems.begin(), elems.end());
        elems.erase(last, elems.end());
        return Relation{std::move(elems)};
    }

    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, Tuple>
    static constexpr Relation from_iter(R &&range) {
        return from_vec(std::vector<Tuple>(
            std::make_move_iterator(std::ranges::begin(range)),
            std::make_move_iterator(std::ranges::end(range))));
    }

    template <typename T2, typename Logic>
    static constexpr Relation from_map(const Relation<T2> &input,
                                       Logic &&logic) {
        std::vector<Tuple> result;
        result.reserve(input.elements.size());
        for (const auto &item : input.elements) {
            result.push_back(logic(item));
        }
        return from_vec(std::move(result));
    }

    constexpr std::optional<size_t> binary_search(const Tuple &target) const {
        auto it =
            std::partition_point(elements.begin(), elements.end(),
                                 [&](const Tuple &x) { return x < target; });
        if (it != elements.end() && *it == target) {
            return std::distance(elements.begin(), it);
        }
        return std::nullopt;
    }

    constexpr size_t size() const { return elements.size(); }
    constexpr bool empty() const { return elements.empty(); }
};

} // namespace df
