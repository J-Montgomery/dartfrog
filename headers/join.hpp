#pragma once

#include <vector>
#include <functional>
#include <ranges>
#include <span>
#include <concepts>

template <std::totally_ordered Tuple> class Variable;
template <std::totally_ordered Tuple> struct Relation;

namespace join {

template <typename I, typename Tuple>
concept JoinInput = requires(I input) {
    { input.recent() } -> std::convertible_to<std::span<const Tuple>>;
    { input.for_each_stable_set(std::declval<std::function<void(std::span<const Tuple>)>>()) };
};


template <typename K, typename V1, typename V2, typename Callback,
JoinInput<std::pair<K, V2>> I2>
void join_delta(const Variable<std::pair<K, V1>>& input1,
                const I2& input2,
                Callback&& result_cb) {}

template <typename K, typename V1, typename V2, typename Res, typename Logic,
JoinInput<std::pair<K, V2>> I2>
void join_into(const Variable<std::pair<K, V1>>& input1,
            const I2& input2,
            Variable<Res>& output,
            Logic&& logic) {}


template <typename K, typename V1, typename V2, typename Res, typename Logic,
JoinInput<std::pair<K, V2>> I2>
void join_and_filter_into(const Variable<std::pair<K, V1>>& input1,
                          const I2& input2,
                          Variable<Res>& output,
                          Logic&& logic) {}

template <typename K, typename V, typename Res, typename Logic>
Relation<Res> antijoin(const Relation<std::pair<K, V>>& input1,
                       const Relation<K>& input2,
                       Logic&& logic) {}
} // namespace join
