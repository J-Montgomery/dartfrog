#pragma once

#include <concepts>
#include <functional>
#include <ranges>
#include <span>
#include <variant>
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

template <typename Span1, typename Span2, class ResultCallback>
constexpr void join_helper(Span1 slice1, Span2 slice2,
                           ResultCallback &&result_cb) {
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

template <class Input1, class Input2, class OutputT, class Logic>
    requires JoinInput<Input2, typename Input2::value_type> &&
             PairLike<typename Input1::value_type>
constexpr void join_into(const Input1 &input1, const Input2 &input2,
                         OutputT &output, Logic &&logic) {

    using KVTuple = typename Input1::value_type;
    using K = typename KVTuple::first_type;
    using V1 = typename KVTuple::second_type;
    using V2 = typename Input2::value_type::second_type;
    using Result =
        std::invoke_result_t<Logic, const K &, const V1 &, const V2 &>;

    std::vector<Result> results;
    auto push_result = [&](const K &k, const V1 &v1, const V2 &v2) {
        results.push_back(logic(k, v1, v2));
    };

    join_delta(input1, input2, push_result);

    output.insert(Relation<Result>::from_vec(std::move(results)));
}

template <typename Input1, typename Input2, typename OutputT, typename Logic>
    requires PairLike<typename Input1::value_type> &&
             JoinInput<Input2, typename Input2::value_type>
constexpr void join_and_filter_into(const Input1 &input1, const Input2 &input2,
                                    OutputT &output, Logic &&logic) {
    using KVTuple = typename Input1::value_type;
    using K = typename KVTuple::first_type;
    using V1 = typename KVTuple::second_type;
    using V2 = typename Input2::value_type::second_type;
    using OptResult =
        std::invoke_result_t<Logic, const K &, const V1 &, const V2 &>;
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
    using Result =
        std::invoke_result_t<Logic, const KeyType &, const ValType &>;

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

    return Relation<Result>::from_vec(std::move(results));
}

template <class Tuple, class Collection, class Logic>
    requires(std::totally_ordered<Tuple>) &&
            (std::totally_ordered<
                typename std::remove_cvref<Collection>::type::value_type>)
constexpr auto leapjoin(std::span<const Tuple> source, Collection &collection,
                        Logic &&logic) {
    using Result = std::invoke_result_t<
        Logic, const Tuple &,
        const typename std::remove_cvref_t<Collection>::value_type &>;
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

        if (!(min_count < std::numeric_limits<size_t>::max())) {
            throw std::runtime_error("leapjoin: Unbounded relations");
        }

        if (min_count > 0) {
            collection.propose(tuple, min_index, values);
            collection.intersect(tuple, min_index, values);

            for (const auto *val_ptr : values) {
                result.push_back(logic(tuple, *val_ptr));
            }

            values.clear();
        }
    }

    return Relation<Result>::from_vec(std::move(result));
}

using Unit = std::monostate;
inline constexpr Unit UNIT_INSTANCE{};

namespace filters {
template <typename Tuple, typename Func>
    requires std::predicate<Func, const Tuple &>
struct PrefixFilter {
    Func predicate;

    constexpr size_t count(const Tuple &prefix) {
        return predicate(prefix) ? std::numeric_limits<size_t>::max() : 0;
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    constexpr void propose(const Tuple &, std::vector<const Val2 *> &) {
        throw std::runtime_error(
            "PrefixFilter::propose(): variable apparently unbound");
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    constexpr void intersect(const Tuple &, std::vector<const Val2 *> &) {}

    constexpr void for_each_count(const Tuple &tuple, auto &&op) {
        size_t c = count(tuple);
        op(0, c == 0 ? 0 : 1);
    }
};

template <typename Tuple, typename Func>
constexpr auto prefix_filter(Func pred) {
    return PrefixFilter<Tuple, Func>{std::move(pred)};
}

template <typename Tuple> struct Passthrough {
    constexpr size_t count(const Tuple &) { return 1; }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &values) {
        values.push_back(&UNIT_INSTANCE);
    }

    constexpr void intersect(const Tuple &, std::vector<const Unit *> &) {}
};

template <typename Tuple> constexpr auto passthrough() {
    return Passthrough<Tuple>{};
}
template <typename Tuple, typename Val, typename Func>
    requires std::predicate<Func, const Tuple &, const Val &>
struct ValueFilter {
    Func predicate;

    constexpr size_t count(const Tuple &) {
        return std::numeric_limits<size_t>::max();
    }

    constexpr void propose(const Tuple &, std::vector<const Val *> &) {
        throw std::runtime_error(
            "ValueFilter::propose(): variable apparently unbound");
    }

    constexpr void intersect(const Tuple &prefix,
                             std::vector<const Val *> &values) {
        std::erase_if(values,
                      [&](const Val *val) { return !predicate(prefix, *val); });
    }
};

template <typename Tuple, typename Val, typename Func>
auto value_filter(Func pred) {
    return ValueFilter<Tuple, Val, Func>{std::move(pred)};
}
} // namespace filters

template <typename Key, typename Val, typename Tuple, typename Func>
class ExtendWith {
    const Relation<std::pair<Key, Val>> *relation;
    size_t start = 0, end = 0;
    Func key_func;
    std::optional<Key> old_key;

  public:
    using value_type = Val;
    constexpr ExtendWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        Key key = key_func(prefix);
        if (!old_key || *old_key != key) {
            std::span all{relation->elements};
            auto range = df::key_range(all, key,
                                       [](const auto &kv) { return kv.first; });
            start = range.data() - all.data();
            end = start + range.size();
            old_key = std::move(key);
        }
        return end - start;
    }

    constexpr void propose(const Tuple &, std::vector<const Val *> &values) {
        for (size_t i = start; i < end; ++i)
            values.push_back(&relation->elements[i].second);
    }

    constexpr void intersect(const Tuple &, std::vector<const Val *> &values) {
        std::span slice{relation->elements.begin() + start,
                        relation->elements.begin() + end};

        auto write_it = values.begin();
        for (const Val *v : values) {
            slice =
                df::seek(slice, [&](const auto &kv) { return kv.second < *v; });
            if (!slice.empty() && slice[0].second == *v) {
                *write_it = v;
                ++write_it;
            }
        }
        values.erase(write_it, values.end());
    }
};

template <typename Key, typename Val, typename Tuple, typename Func>
class ExtendAnti {
    const Relation<std::pair<Key, Val>> *relation;
    Func key_func;
    struct Cache {
        Key key;
        size_t start;
        size_t end;
    };
    mutable std::optional<Cache> old_key;

  public:
    using value_type = Val;
    constexpr ExtendAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &) {
        return std::numeric_limits<size_t>::max();
    }
    constexpr void propose(const Tuple &, std::vector<const Val *> &) {
        throw std::runtime_error(
            "ExtendAnti::propose(): variable apparently unbound.");
    }

    constexpr void intersect(const Tuple &prefix,
                             std::vector<const Val *> &values) const {
        if (values.empty())
            return;

        Key key = key_func(prefix);
        if (!old_key || old_key->key != key) {
            std::span all{relation->elements};
            auto range = df::key_range(all, key,
                                       [](const auto &kv) { return kv.first; });
            size_t s = range.data() - all.data();
            old_key = Cache{key, s, s + range.size()};
        }

        std::span slice{relation->elements.begin() + old_key->start,
                        relation->elements.begin() + old_key->end};
        if (slice.empty())
            return;

        std::erase_if(values, [slice](const Val *v) mutable {
            slice =
                df::seek(slice, [&](const auto &kv) { return kv.second < *v; });
            return !slice.empty() && slice[0].second == *v;
        });
    }
};

template <typename Key, typename Val, typename Tuple, typename Func>
class FilterWith {
    const Relation<std::pair<Key, Val>> *relation;
    Func key_func;
    std::optional<std::pair<std::pair<Key, Val>, bool>> old_kv;

  public:
    constexpr FilterWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);
        if (old_kv && old_kv->first == kv)
            return old_kv->second ? std::numeric_limits<size_t>::max() : 0;
        bool present = relation->binary_search(kv).has_value();
        old_kv = {kv, present};
        return present ? std::numeric_limits<size_t>::max() : 0;
    }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &v) {
        v.push_back(&UNIT_INSTANCE);
    }
    constexpr void intersect(const Tuple &, std::vector<const Unit *> &) {}
};

template <typename Key, typename Val, typename Tuple, typename Func>
class FilterAnti {
    const Relation<std::pair<Key, Val>> *relation;
    Func key_func;
    std::optional<std::pair<std::pair<Key, Val>, bool>> old_kv;

  public:
    constexpr FilterAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);

        if (old_kv && old_kv->first == kv) {
            return old_kv->second ? 0 : std::numeric_limits<size_t>::max();
        }

        bool present = relation->binary_search(kv).has_value();
        old_kv = {kv, present};

        return present ? 0 : std::numeric_limits<size_t>::max();
    }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &v) {
        v.push_back(&UNIT_INSTANCE);
    }

    constexpr void intersect(const Tuple &, std::vector<const Unit *> &) {}
};

} // namespace df
