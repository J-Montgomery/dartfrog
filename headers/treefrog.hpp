#pragma once

#include <algorithm>
#include <vector>
#include <tuple>
#include <concepts>
#include <type_traits>
#include <numeric>
#include <cstdint>


template<typename T, typename Tuple, typename Val>
concept Leaper = requires(T l, const Tuple& t, std::vector<const Val*>& v) {
    { l.count(t) } -> std::same_as<size_t>;
    { l.propose(t, v) } -> std::same_as<void>;
    { l.intersect(t, v) } -> std::same_as<void>;
};

template<typename Tuple, typename Val, typename... LeaperTs>
requires (Leaper<LeaperTs, Tuple, Val>&& ...)
struct LeaperCollection {
    std::tuple<LeaperTs...> leapers;

    void for_each_count(const Tuple& tuple, auto&& op) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (op(Is, std::get<Is>(leapers).count(tuple)), ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }

    void propose(const Tuple& tuple, size_t min_index, std::vector<const Val*>& values) {
        bool found = [&]<size_t... Is>(std::index_sequence<Is...>) {
            bool handled = false;
            ((Is == min_index ? (std::get<Is>(leapers).propose(tuple, values), handled = true) : false) || ...);
            return handled;
        }(std::index_sequence_for<LeaperTs...>{});

        if (!found) throw std::runtime_error("No match found for min_index");
    }

    void intersect(const Tuple& tuple, size_t min_index, std::vector<const Val*>& values) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((Is != min_index ? std::get<Is>(leapers).intersect(tuple, values) : void()), ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }
};

template<typename Tuple, typename Val, typename Result, typename Collection, typename Logic>
auto leapjoin(
    const std::vector<Tuple>& source,
    Collection& collection,
    Logic&& logic
) -> std::vector<Result> {

    std::vector<Result> result;
    std::vector<const Val*> values;

    for (const auto& tuple : source) {
        size_t min_index = std::numeric_limits<size_t>::max();
        size_t min_count = std::numeric_limits<size_t>::max();

        collection.for_each_count(tuple, [&](size_t index, size_t count) {
            if (min_count > count) {
                min_count = count;
                min_index = index;
            }
        });

        if (min_count != std::numeric_limits<size_t>::max() && min_count > 0) {
            collection.propose(tuple, min_index, values);
            collection.intersect(tuple, min_index, values);

            for (const auto* val_ptr : values) {
                result.push_back(logic(tuple, *val_ptr));
            }

            values.clear();
        }
    }

    return result;
}

using Unit = std::monostate;
const Unit UNIT_INSTANCE = std::monostate{};
