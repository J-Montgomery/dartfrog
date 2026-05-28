#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <vector>

template <typename T, typename Tuple, typename Val>
concept Leaper = requires(T l, const Tuple &t, std::vector<const Val *> &v) {
    { l.count(t) } -> std::same_as<size_t>;
    { l.propose(t, v) } -> std::same_as<void>;
    { l.intersect(t, v) } -> std::same_as<void>;
};

template <typename T, typename Func>
    requires std::invocable<Func, const T &> && (sizeof(T) > 0)
size_t binary_search(const std::vector<T> &vec, Func &&cmp) {
    auto it = std::partition_point(vec.begin(), vec.end(), cmp);
    return std::distance(vec.begin(), it);
}

template <typename Tuple, typename Val, typename... LeaperTs>
    requires(Leaper<LeaperTs, Tuple, Val> && ...)
struct LeaperCollection {
    std::tuple<LeaperTs...> leapers;

    void for_each_count(const Tuple &tuple,
                        std::invocable<size_t, size_t> auto &&op) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (op(Is, std::get<Is>(leapers).count(tuple)), ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }

    void propose(const Tuple &tuple, size_t min_index,
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

    void intersect(const Tuple &tuple, size_t min_index,
                   std::vector<const Val *> &values) {
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((Is != min_index ? std::get<Is>(leapers).intersect(tuple, values)
                              : void()),
             ...);
        }(std::index_sequence_for<LeaperTs...>{});
    }
};

template <typename Tuple, typename Val, typename Result, typename Collection,
          typename Logic>
auto leapjoin(const std::vector<Tuple> &source, Collection &collection,
              Logic &&logic) -> Relation<Result> {

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

    return Relation<Result>::from_vec(result);
}

using Unit = std::monostate;
const Unit UNIT_INSTANCE = std::monostate{};

namespace filters {
template <typename Tuple, typename Func>
    requires std::predicate<Func, const Tuple &>
struct PrefixFilter {
    Func predicate;

    static auto from(Func pred) {
        return PrefixFilter<Tuple, Func>{std::move(pred)};
    }

    size_t count(const Tuple &prefix) {
        return predicate(prefix) ? std::numeric_limits<size_t>::max() : 0;
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void propose(const Tuple &, std::vector<const Val2 *> &) {
        throw std::runtime_error(
            "PrefixFilter::propose(): variable apparently unbound");
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void intersect(const Tuple &, std::vector<const Val2 *> &) {}

    template <typename Val2>
    void for_each_count(const Tuple &tuple, auto &&op) {
        size_t c = count(tuple);
        if constexpr (std::is_same_v<Val2, Unit>) {
            op(0, c == 0 ? 0 : 1);
        } else {
            op(0, c);
        }
    }

    void propose(const Tuple &, std::vector<const Unit *> &values) {
        values.push_back(&UNIT_INSTANCE);
    }

    void intersect(const Tuple &, std::vector<const Unit *> &values) {
        if (values.size() != 1)
            throw std::runtime_error("Logic error");
    }
};
template <typename Tuple> struct Passthrough {
    size_t count(const Tuple &) { return 1; }

    void propose(const Tuple &, std::vector<const Unit *> &values) {
        values.push_back(&UNIT_INSTANCE);
    }

    void intersect(const Tuple &, std::vector<const Unit *> &) {}
};

template <typename Tuple> auto passthrough() { return Passthrough<Tuple>{}; }
template <typename Tuple, typename Val, typename Func>
    requires std::predicate<Func, const Tuple &, const Val &>
struct ValueFilter {
    Func predicate;

    static auto from(Func pred) {
        return ValueFilter<Tuple, Val, Func>{std::move(pred)};
    }

    size_t count(const Tuple &) { return std::numeric_limits<size_t>::max(); }

    void propose(const Tuple &, std::vector<const Val *> &) {
        throw std::runtime_error(
            "ValueFilter::propose(): variable apparently unbound");
    }

    void intersect(const Tuple &prefix, std::vector<const Val *> &values) {
        std::erase_if(values,
                      [&](const Val *val) { return !predicate(prefix, *val); });
    }

    void for_each_count(const Tuple &t, auto &&op) { op(0, count(t)); }
    void propose(const Tuple &t, size_t, std::vector<const Val *> &v) {
        propose(t, v);
    }
    void intersect(const Tuple &t, size_t, std::vector<const Val *> &v) {
        intersect(t, v);
    }
};
} // namespace filters

namespace extend_with {
template <typename Key, typename Val, typename Tuple, typename Func>
class ExtendWith {
    const Relation<std::pair<Key, Val>> *relation;
    size_t start = 0, end = 0;
    Func key_func;
    std::optional<Key> old_key;

  public:
    ExtendWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    size_t count(const Tuple &prefix) {
        Key key = key_func(prefix);
        if (!old_key || *old_key != key) {
            start = binary_search(relation->elements,
                                  [&](const auto &x) { return x.first < key; });
            std::span s1{relation->elements.begin() + start,
                         relation->elements.end()};
            auto s2 = gallop(s1, [&](const auto &x) { return x.first <= key; });
            end = relation->len() - s2.size();
            old_key = std::move(key);
        }
        return end - start;
    }

    void propose(const Tuple &, std::vector<const Val *> &values) {
        for (size_t i = start; i < end; ++i)
            values.push_back(&relation->elements[i].second);
    }

    void intersect(const Tuple &, std::vector<const Val *> &values) {
        std::span slice{relation->elements.begin() + start,
                        relation->elements.begin() + end};

        auto write_it = values.begin();
        for (const Val *v : values) {
            slice =
                gallop(slice, [&](const auto &kv) { return kv.second < *v; });
            if (!slice.empty() && slice[0].second == *v) {
                *write_it = v;
                ++write_it;
            }
        }
        values.erase(write_it, values.end());
    }

    void for_each_count(const Tuple &t, auto &&op) { op(0, count(t)); }
    void propose(const Tuple &t, size_t, std::vector<const Val *> &v) {
        propose(t, v);
    }
    void intersect(const Tuple &, size_t, std::vector<const Val *> &v) {
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
    ExtendAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    size_t count(const Tuple &) { return std::numeric_limits<size_t>::max(); }
    void propose(const Tuple &, std::vector<const Val *> &) {
        throw std::runtime_error(
            "ExtendAnti::propose(): variable apparently unbound.");
    }

    void intersect(const Tuple &prefix,
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
            auto s2 = gallop(s1, [&](const auto &x) { return x.first <= key; });
            old_key = Cache{key, s, relation->elements.size() - s2.size()};
        }

        std::span slice{relation->elements.begin() + old_key->start,
                        relation->elements.begin() + old_key->end};
        if (slice.empty())
            return;

        std::erase_if(values, [slice](const Val *v) mutable {
            slice =
                gallop(slice, [&](const auto &kv) { return kv.second < *v; });
            return !slice.empty() && slice[0].second == *v;
        });
    }

    void for_each_count(const Tuple &t, auto &&op) { op(0, count(t)); }
    void propose(const Tuple &t, size_t, std::vector<const Val *> &v) {
        propose(t, v);
    }
    void intersect(const Tuple &t, size_t, std::vector<const Val *> &v) {
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
    FilterWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);
        if (old_kv && old_kv->first == kv)
            return old_kv->second ? std::numeric_limits<size_t>::max() : 0;
        bool present = relation->binary_search(kv).has_value();
        old_kv = {kv, present};
        return present ? std::numeric_limits<size_t>::max() : 0;
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void propose(const Tuple &, std::vector<const Val2 *> &) {
        throw std::runtime_error(
            "FilterWith::propose(): variable apparently unbound.");
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void intersect(const Tuple &, std::vector<const Val2 *> &) {}

    void for_each_count(const Tuple &t, auto &&op) {
        size_t c = count(t);
        op(0, c == 0 ? 0 : (std::is_same_v<Val, Unit> ? 1 : c));
    }

    void propose(const Tuple &, size_t, std::vector<const Unit *> &v) {
        v.push_back(&UNIT_INSTANCE);
    }
    void intersect(const Tuple &, size_t, std::vector<const Unit *> &) {}
};
} // namespace filter_with

namespace filter_anti {
template <typename Key, typename Val, typename Tuple, typename Func>
class FilterAnti {
    const Relation<std::pair<Key, Val>> *relation;
    Func key_func;
    std::optional<std::pair<std::pair<Key, Val>, bool>> old_kv;

  public:
    FilterAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);

        if (old_kv && old_kv->first == kv) {
            return old_kv->second ? 0 : std::numeric_limits<size_t>::max();
        }

        bool present = relation->binary_search(kv).has_value();
        old_kv = {kv, present};

        return present ? 0 : std::numeric_limits<size_t>::max();
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void propose(const Tuple &, std::vector<const Val2 *> &) {
        throw std::runtime_error("FilterAnti::propose(): variable unbound.");
    }

    template <typename Val2>
        requires(!std::is_same_v<Val2, Unit>)
    void intersect(const Tuple &, std::vector<const Val2 *> &) {}

    void for_each_count(const Tuple &t, auto &&op) {
        size_t c = count(t);
        op(0, c == 0 ? 0 : (std::is_same_v<Val, Unit> ? 1 : c));
    }

    void propose(const Tuple &, std::vector<const Unit *> &values) {
        values.push_back(&UNIT_INSTANCE);
    }

    void intersect(const Tuple &, std::vector<const Unit *> &values) {
        if (values.size() != 1)
            throw std::logic_error("logic error");
    }
};
} // namespace filter_anti

template <typename Key, typename Val> struct RelationLeaper {
    const Relation<std::pair<Key, Val>> *self;

    template <typename Tuple, typename Func> auto extend_with(Func &&f) const {
        return extend_with::ExtendWith<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func> auto extend_anti(Func &&f) const {
        return extend_anti::ExtendAnti<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func> auto filter_with(Func &&f) const {
        return filter_with::FilterWith<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }

    template <typename Tuple, typename Func> auto filter_anti(Func &&f) const {
        return filter_anti::FilterAnti<Key, Val, Tuple, Func>(
            self, std::forward<Func>(f));
    }
};
