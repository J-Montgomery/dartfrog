#pragma once

#include <concepts>

#include <span>
#include <vector>

#include "relation.hpp"

namespace df {

template <std::totally_ordered Tuple> class Variable;
template <std::totally_ordered Tuple> struct Relation;

template <typename I> struct input_value_type;

template <typename K, typename V>
struct input_value_type<Variable<std::pair<K, V>>> {
    using type = V;
};

template <typename K, typename V>
struct input_value_type<Relation<std::pair<K, V>>> {
    using type = V;
};

template <typename I>
using input_value_type_t =
    typename input_value_type<std::remove_cvref_t<I>>::type;

template <typename I, typename Tuple>
concept JoinInput = requires(I input) {
    { input.recent() } -> std::convertible_to<std::span<const Tuple>>;
    {
        input.for_each_stable_set([](std::span<const Tuple>) {})
    };
};

template <typename T>
concept PairLike = requires {
    typename T::first_type;
    typename T::second_type;
} && requires(const T &t) {
    { t.first } -> std::convertible_to<const typename T::first_type &>;
    { t.second } -> std::convertible_to<const typename T::second_type &>;
};

template <typename Span1, typename Span2, typename Callback>
constexpr void join_helper(Span1 slice1, Span2 slice2, Callback &&result_cb) {
    using K = typename decltype(slice1)::value_type::first_type;
    while (!slice1.empty() && !slice2.empty()) {
        auto k1 = slice1[0].first;
        auto k2 = slice2[0].first;

        if (k1 < k2) {
            slice1 = seek(slice1, [&](const auto &x) { return x.first < k2; });
        } else if (k2 < k1) {
            slice2 = seek(slice2, [&](const auto &x) { return x.first < k1; });
        } else {
            const K &match_key = k1;

            auto rest1 = seek(
                slice1, [&](const auto &x) { return x.first == match_key; });
            size_t count1 = rest1.data() - slice1.data();

            auto rest2 = seek(
                slice2, [&](const auto &x) { return x.first == match_key; });
            size_t count2 = rest2.data() - slice2.data();

            for (size_t i = 0; i < count1; ++i) {
                for (size_t j = 0; j < count2; ++j) {
                    result_cb(match_key, slice1[i].second, slice2[j].second);
                }
            }

            slice1 = slice1.subspan(count1);
            slice2 = slice2.subspan(count2);
        }
    }
}

template <typename Input1, typename Input2, typename Callback>
constexpr void join_delta(const Input1 &input1, const Input2 &input2,
                          Callback &&result_cb) {
    using Tuple1 = typename Input1::value_type;
    using Tuple2 = typename Input2::value_type;

    auto recent1 = input1.recent();
    auto recent2 = input2.recent();

    input2.for_each_stable_set([&](std::span<const Tuple2> batch2) {
        join_helper(recent1, batch2, result_cb);
    });

    input1.for_each_stable_set([&](std::span<const Tuple1> batch1) {
        join_helper(batch1, recent2, result_cb);
    });

    join_helper(recent1, recent2, result_cb);
}

template <typename Input1, typename Input2, typename Output, typename Logic>
    requires JoinInput<Input2, typename Input2::value_type> &&
             PairLike<typename Input1::value_type>
constexpr void join_into(const Input1 &input1, const Input2 &input2,
                         Output &output, Logic &&logic) {

    using KVTuple = typename Input1::value_type;
    using K = typename KVTuple::first_type;
    using V1 = typename KVTuple::second_type;
    using V2 = typename Input2::value_type::second_type;
    using Result = decltype(std::declval<Logic>()(
        std::declval<K>(), std::declval<V1>(), std::declval<V2>()));

    std::vector<Result> results;
    auto push_result = [&](const K &k, const V1 &v1, const V2 &v2) {
        results.push_back(logic(k, v1, v2));
    };

    join_delta(input1, input2, push_result);

    output.insert(Relation<Result>::from_vec(std::move(results)));
}

template <typename Input1, typename Input2, typename Output, typename Logic>
    requires PairLike<typename Input1::value_type> &&
             JoinInput<Input2, typename Input2::value_type>
constexpr void join_and_filter_into(const Input1 &input1, const Input2 &input2,
                                    Output &output, Logic &&logic) {
    using KVTuple = typename Input1::value_type;
    using K = typename KVTuple::first_type;
    using V1 = typename KVTuple::second_type;
    using V2 = typename Input2::value_type::second_type;
    using OptResult = decltype(std::declval<Logic>()(
        std::declval<K>(), std::declval<V1>(), std::declval<V2>()));
    using Result = typename OptResult::value_type;

    std::vector<Result> results;
    join_delta(input1, input2, [&](const K &k, const V1 &v1, const V2 &v2) {
        if (auto opt = logic(k, v1, v2)) {
            results.push_back(std::move(*opt));
        }
    });

    output.insert(Relation<Result>::from_vec(std::move(results)));
}

template <typename InputRange, typename ExcludeKey, typename Logic>
constexpr auto antijoin(const InputRange &input1,
                        const Relation<ExcludeKey> &input2, Logic &&logic) {
    using ElementType =
        typename std::remove_cvref_t<decltype(*std::begin(input1))>;
    using KeyType = typename ElementType::first_type;
    using ValType = typename ElementType::second_type;
    using Result = decltype(std::declval<Logic>()(std::declval<KeyType>(),
                                                  std::declval<ValType>()));

    std::vector<Result> results;
    std::span<const ExcludeKey> tuples2 = input2.elements;

    for (const auto &elem : input1) {
        const auto &key = elem.first;
        const auto &val = elem.second;
        tuples2 = seek(tuples2, [&](const ExcludeKey &k) { return k < key; });
        if (tuples2.empty() || tuples2[0] != key) {
            results.push_back(logic(key, val));
        }
    }

    return Relation<Result>{std::move(results)};
}

template <typename Tuple, typename Collection, typename Logic>
    requires(std::totally_ordered<Tuple>) &&
            (std::totally_ordered<
                typename std::remove_cvref<Collection>::type::value_type>)
constexpr auto leapjoin(std::span<const Tuple> source, Collection &collection,
                        Logic &&logic) {
    using Result = decltype(std::declval<Logic>()(
        std::declval<Tuple>(),
        std::declval<typename std::remove_cvref_t<Collection>::value_type>()));
    using PointerType =
        typename std::remove_cvref_t<decltype(collection)>::value_type;
    using Val = std::remove_cv_t<std::remove_pointer_t<PointerType>>;

    std::vector<Result> result;
    std::vector<const Val *> values;

    for (const auto &tuple : source) {
        size_t min_index = std::numeric_limits<size_t>::max();
        size_t min_count = std::numeric_limits<size_t>::max();

        collection.for_each_count(tuple, [&](size_t index, size_t count) {
            if (min_count > count) {
                min_count = count;
                min_index = index;
            }
        });

        if (min_count > 0) {
            collection.propose(tuple, min_index, values);
            collection.intersect(tuple, min_index, values);

            for (const auto *val_ptr : values) {
                result.push_back(logic(tuple, *val_ptr));
            }

            values.clear();
        }
    }

    return Relation<Result>{std::move(result)};
}

} // namespace df
