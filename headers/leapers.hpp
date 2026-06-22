#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "join.hpp"
#include "relation.hpp"

namespace df {

template <typename T, typename Tuple, typename Val>
concept Leaper = requires(T l, const Tuple &t, std::vector<const Val *> &v) {
    { l.count(t) } -> std::same_as<size_t>;
    { l.propose(t, v) } -> std::same_as<void>;
    { l.intersect(t, v) } -> std::same_as<void>;
};

template <typename Tuple, typename Val, typename... LeaperTs>
    requires(Leaper<LeaperTs, Tuple, Val> && ...)
struct LeaperCollection {
    using value_type = Val;
    std::tuple<LeaperTs...> leapers;

    constexpr void for_each_count(const Tuple &tuple,
                                  std::invocable<size_t, size_t> auto &&op) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (op(Is, std::get<Is>(leapers).count(tuple)), ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }

    constexpr void propose(const Tuple &tuple, size_t min_index,
                           std::vector<const Val *> &values) {
        bool found = [&]<size_t... Is>(std::index_sequence<Is...>) {
            bool handled = false;
            ((Is == min_index ? (std::get<Is>(leapers).propose(tuple, values),
                                 handled = true)
                              : false) ||
             ...);
            return handled;
        }(std::index_sequence_for<LeaperTs...>{});

        if (!found)
            throw std::runtime_error("No match found for min_index");
    }

    constexpr void intersect(const Tuple &tuple, size_t min_index,
                             std::vector<const Val *> &values) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((Is != min_index ? std::get<Is>(leapers).intersect(tuple, values)
                              : void()),
             ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }
};

template <typename Key, typename Val> struct RelationLeaper {
    const Relation<std::pair<Key, Val>> *self;

    template <typename Tuple, typename Func>
    constexpr auto extend_with(Func &&f) const {
        return ExtendWith<Key, Val, Tuple, std::remove_cvref_t<Func>>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto extend_anti(Func &&f) const {
        return ExtendAnti<Key, Val, Tuple, Func>(self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto filter_with(Func &&f) const {
        return FilterWith<Key, Val, Tuple, Func>(self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto filter_anti(Func &&f) const {
        return FilterAnti<Key, Val, Tuple, Func>(self, std::forward<Func>(f));
    }
};

} // namespace df
