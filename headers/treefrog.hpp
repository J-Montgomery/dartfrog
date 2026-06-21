#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace df {

template <typename T, typename Tuple, typename Val>
concept Leaper = requires(T l, const Tuple &t, std::vector<const Val *> &v) {
    { l.count(t) } -> std::same_as<size_t>;
    { l.propose(t, v) } -> std::same_as<void>;
    { l.intersect(t, v) } -> std::same_as<void>;
};

template <typename T, typename Func>
    requires std::invocable<Func, const T &> && (sizeof(T) > 0)
constexpr size_t binary_search(const std::vector<T> &vec, Func &&cmp) {
    auto it = std::partition_point(vec.begin(), vec.end(), cmp);
    return std::distance(vec.begin(), it);
}

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
const Unit UNIT_INSTANCE = std::monostate{};

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

    // These are needed to Satisfy the LeaperCollection concept, but min_index
    // is always 0 for PrefixFilters
    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Unit *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Unit *> &v) {
        intersect(t, v);
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

    constexpr void for_each_count(const Tuple &t, auto &&op) {
        op(0, count(t));
    }
    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Val *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Val *> &v) {
        intersect(t, v);
    }
};

template <typename Tuple, typename Val, typename Func>
auto value_filter(Func pred) {
    return ValueFilter<Tuple, Val, Func>{std::move(pred)};
    ;
}
} // namespace filters

namespace extend_with {
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
            start = binary_search(relation->elements,
                                  [&](const auto &x) { return x.first < key; });
            std::span s1{relation->elements.begin() + start,
                         relation->elements.end()};
            auto s2 =
                join::gallop(s1, [&](const auto &x) { return x.first <= key; });
            end = relation->size() - s2.size();
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
            slice = join::gallop(
                slice, [&](const auto &kv) { return kv.second < *v; });
            if (!slice.empty() && slice[0].second == *v) {
                *write_it = v;
                ++write_it;
            }
        }
        values.erase(write_it, values.end());
    }

    constexpr void for_each_count(const Tuple &t, auto &&op) {
        op(0, count(t));
    }
    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Val *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Val *> &v) {
        intersect(t, v);
    }
};
} // namespace extend_with

namespace extend_anti {
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
            size_t s = binary_search(relation->elements, [&](const auto &x) {
                return x.first < key;
            });
            std::span s1{relation->elements.begin() + s,
                         relation->elements.end()};
            auto s2 =
                join::gallop(s1, [&](const auto &x) { return x.first <= key; });
            old_key = Cache{key, s, relation->elements.size() - s2.size()};
        }

        std::span slice{relation->elements.begin() + old_key->start,
                        relation->elements.begin() + old_key->end};
        if (slice.empty())
            return;

        std::erase_if(values, [slice](const Val *v) mutable {
            slice = join::gallop(
                slice, [&](const auto &kv) { return kv.second < *v; });
            return !slice.empty() && slice[0].second == *v;
        });
    }

    constexpr void for_each_count(const Tuple &t, auto &&op) {
        op(0, count(t));
    }
    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Val *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Val *> &v) {
        intersect(t, v);
    }
};
} // namespace extend_anti

namespace filter_with {
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

    constexpr void for_each_count(const Tuple &t, auto &&op) {
        size_t c = count(t);
        op(0, c == 0 ? 0 : 1);
    }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &v) {
        v.push_back(&UNIT_INSTANCE);
    }
    constexpr void intersect(const Tuple &, std::vector<const Unit *> &) {}

    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Unit *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Unit *> &v) {
        intersect(t, v);
    }
};
} // namespace filter_with

namespace filter_anti {
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

    constexpr void for_each_count(const Tuple &t, auto &&op) {
        size_t c = count(t);
        op(0, c == 0 ? 0 : 1);
    }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &v) {
        v.push_back(&UNIT_INSTANCE);
    }

    constexpr void intersect(const Tuple &, std::vector<const Unit *> &) {}

    constexpr void propose(const Tuple &t, size_t,
                           std::vector<const Unit *> &v) {
        propose(t, v);
    }
    constexpr void intersect(const Tuple &t, size_t,
                             std::vector<const Unit *> &v) {
        intersect(t, v);
    }
};
} // namespace filter_anti

template <typename Key, typename Val> struct RelationLeaper {
    const Relation<std::pair<Key, Val>> *self;

    template <typename Tuple, typename Func>
    constexpr auto extend_with(Func &&f) const {
        return extend_with::ExtendWith<Key, Val, Tuple, std::remove_cvref_t<Func>>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto extend_anti(Func &&f) const {
        return extend_anti::ExtendAnti<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto filter_with(Func &&f) const {
        return filter_with::FilterWith<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func>
    constexpr auto filter_anti(Func &&f) const {
        return filter_anti::FilterAnti<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }
};

template <typename SrcTuple, typename RelKey, typename RelVal, typename KeyFunc, typename Logic>
void leap(std::span<const SrcTuple> src_span,
             const df::Relation<std::pair<RelKey, RelVal>>& rel,
             KeyFunc&& kf, Logic&& logic, auto& output) {

    RelationLeaper<RelKey, RelVal> leaper_wrapper{&rel};
    auto ext = leaper_wrapper.template extend_with<SrcTuple>(kf);
    LeaperCollection<SrcTuple, typename decltype(ext)::value_type, decltype(ext)>
        coll{std::make_tuple(ext)};

    auto result_rel = leapjoin(src_span, coll, logic);
    output.insert(result_rel);
}

template <typename Var1, typename Var2, typename KFunc1, typename KFunc2, typename Logic>
void leapjoin_delta(const Var1& var1, const Var2& var2,
                    KFunc1&& kf1, KFunc2&& kf2,
                    Logic&& logic, auto& output) {
    auto src_span1 = var1.recent();
    if (!src_span1.empty()) {
        for (const auto& rel2 : var2.stable) {
            leap(src_span1, rel2, kf1, logic, output);
        }
        if (!var2.recent_data.empty()) {
            leap(src_span1, var2.recent_data, kf1, logic, output);
        }
    }

    auto src_span2 = var2.recent();
    if (!src_span2.empty()) {
        for (const auto& rel1 : var1.stable) {
            auto reverse_logic = [&](const auto& t2, const auto& val1) {
                return logic(std::pair{t2.first, val1}, t2.second);
            };
            leap(src_span2, rel1, kf2, reverse_logic, output);
        }
    }
}
} // namespace df
