#pragma once

#include <concepts>
#include <functional>
#include <ranges>
#include <span>
#include <vector>

template <std::totally_ordered Tuple> class Variable;
template <std::totally_ordered Tuple> struct Relation;

namespace join {

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
        input.for_each_stable_set(
            std::declval<std::function<void(std::span<const Tuple>)>>())
    };
};

template <typename T, typename Cmp>
std::span<T> gallop(std::span<T> slice, Cmp &&cmp) {
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

template <typename Span1, typename Span2, class ResultCallback>
void join_helper(Span1 slice1, Span2 slice2, ResultCallback &&result_cb) {
    using K = typename decltype(slice1)::value_type::first_type;
    using V1 = typename decltype(slice1)::value_type::second_type;
    while (!slice1.empty() && !slice2.empty()) {
        auto k1 = slice1[0].first;
        auto k2 = slice2[0].first;

        if (k1 < k2) {
            slice1 =
                gallop(slice1, [&](const auto &x) { return x.first < k2; });
        } else if (k2 < k1) {
            slice2 =
                gallop(slice2, [&](const auto &x) { return x.first < k1; });
        } else {
            const K &match_key = k1;

            size_t count1 = 0;
            while (count1 < slice1.size() && slice1[count1].first == match_key)
                count1++;

            size_t count2 = 0;
            while (count2 < slice2.size() && slice2[count2].first == match_key)
                count2++;

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

template <typename Variable1, typename Input2, typename Callback>
void join_delta(const Variable1 &input1, const Input2 &input2,
                Callback &&result_cb) {
    using K = typename Variable1::value_type::first_type;
    using V1 = typename Variable1::value_type::second_type;
    using V2 = typename Input2::value_type::second_type;

    auto recent1 = input1.recent();
    auto recent2 = input2.recent();

    input2.for_each_stable_set([&](std::span<const std::pair<K, V2>> batch2) {
        join_helper(recent1, batch2, result_cb);
    });

    input1.for_each_stable_set([&](std::span<const std::pair<K, V1>> batch1) {
        join_helper(batch1, recent2, result_cb);
    });

    join_helper(recent1, recent2, result_cb);
}

template <class Tuple1, class I2, class Res, class Logic>
    requires JoinInput<I2, std::pair<typename Tuple1::first_type,
                                     typename I2::value_type::second_type>>
void join_into(const Variable<Tuple1> &input1, const I2 &input2,
               Variable<Res> &output, Logic &&logic) {

    using K = typename Tuple1::first_type;
    using V1 = typename Tuple1::second_type;
    using V2 = typename I2::value_type::second_type;

    std::vector<Res> results;
    auto push_result = [&](const K &k, const V1 &v1, const V2 &v2) {
        results.push_back(logic(k, v1, v2));
    };

    join_delta(input1, input2, push_result);

    output.insert(Relation<Res>::from_vec(std::move(results)));
}

template <typename K, typename V1, typename V2 = V1, typename Res,
          typename Logic, JoinInput<std::pair<K, V2>> I2>
void join_and_filter_into(const Variable<std::pair<K, V1>> &input1,
                          const I2 &input2, Variable<Res> &output,
                          Logic &&logic) {
    std::vector<Res> results;
    join_delta(input1, input2, [&](const K &k, const V1 &v1, const V2 &v2) {
        if (auto opt = logic(k, v1, v2)) {
            results.push_back(std::move(*opt));
        }
    });

    output.insert(Relation<Res>::from_vec(std::move(results)));
}

template <typename InputRange, typename K, typename Logic>
auto antijoin(const InputRange &input1, const Relation<K> &input2,
              Logic &&logic) {
    using ElementType =
        typename std::remove_cvref_t<decltype(*std::begin(input1))>;
    using KeyType = typename ElementType::first_type;
    using ValType = typename ElementType::second_type;
    using Res = std::invoke_result_t<Logic, const KeyType &, const ValType &>;

    std::vector<Res> results;
    std::span<const K> tuples2 = input2.elements;

    for (const auto &[key, val] : input1) {
        tuples2 = gallop(tuples2, [&](const K &k) { return k < key; });
        if (tuples2.empty() || tuples2[0] != key) {
            results.push_back(logic(key, val));
        }
    }

    return Relation<Res>::from_vec(std::move(results));
}
} // namespace join
