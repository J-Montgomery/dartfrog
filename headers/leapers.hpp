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

using Unit = std::monostate;
inline constexpr Unit UNIT_INSTANCE{};

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

namespace filters {
template <typename Tuple, typename Func>
    requires std::predicate<Func, const Tuple &>
struct PrefixFilter {
    Func predicate;

    constexpr size_t count(const Tuple &prefix) {
        return predicate(prefix) ? std::numeric_limits<size_t>::max() : 0;
    }

    template <typename Val2>
    constexpr void propose(const Tuple &, std::vector<const Val2 *> &) {
        assert(!"PrefixFilter::propose(): variable apparently unbound");
    }

    template <typename Val2>
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
        assert("ValueFilter::propose(): variable apparently unbound");
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
        assert("ExtendAnti::propose(): variable apparently unbound.");
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

    template <typename Val2>
    constexpr void propose(const Tuple &, std::vector<const Val2 *> &) {
        assert(!"FilterWith::propose(): variable apparently unbound");
    }

    template <typename Val2>
    constexpr void intersect(const Tuple &, std::vector<const Val2 *> &) {}
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
