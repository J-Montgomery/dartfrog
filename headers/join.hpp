#pragma once

#include <concepts>
#include <functional>
#include <ranges>
#include <span>
#include <vector>

template <std::totally_ordered Tuple> class Variable;
template <std::totally_ordered Tuple> struct Relation;

namespace join {

template <typename I, typename Tuple>
concept JoinInput = requires(I input) {
    { input.recent() } -> std::convertible_to<std::span<const Tuple>>;
    {
        input.for_each_stable_set(
            std::declval<std::function<void(std::span<const Tuple>)>>())
    };
};

template <typename T, typename Cmp>
std::span<const T> gallop(std::span<const T> slice, Cmp &&cmp) {
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

template <typename K, typename V1, typename V2, typename ResultCallback>
void join_helper(std::span<const std::pair<K, V1>> slice1,
                 std::span<const std::pair<K, V2>> slice2,
                 ResultCallback &&result_cb) {
    while (!slice1.empty() && !slice2.empty()) {
        const K &k1 = slice1[0].first;
        const K &k2 = slice2[0].first;

        if (k1 < k2) {
            slice1 =
                gallop(slice1, [&](const auto &x) { return x.first < k2; });
        } else if (k2 < k2) {
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

template <typename K, typename V1, typename V2, typename Callback,
          JoinInput<std::pair<K, V2>> I2>
void join_delta(const Variable<std::pair<K, V1>> &input1, const I2 &input2,
                Callback &&result_cb) {
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

template <typename K, typename V1, typename V2, typename Res, typename Logic,
          JoinInput<std::pair<K, V2>> I2>
void join_into(const Variable<std::pair<K, V1>> &input1, const I2 &input2,
               Variable<Res> &output, Logic &&logic) {
    std::vector<Res> results;
    join_delta(input1, input2, [&](const K &k, const V1 &v1, const V2 &v2) {
        results.push_back(logic(k, v1, v2));
    });

    output.insert(Relation<Res>::from_vec(std::move(results)));
}

template <typename K, typename V1, typename V2, typename Res, typename Logic,
          JoinInput<std::pair<K, V2>> I2>
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

template <typename K, typename V, typename Res, typename Logic>
Relation<Res> antijoin(const Relation<std::pair<K, V>> &input1,
                       const Relation<K> &input2, Logic &&logic) {}
} // namespace join
