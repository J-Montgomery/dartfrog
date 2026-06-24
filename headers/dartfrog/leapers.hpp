#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dartfrog/relation.hpp"

namespace df {

template <typename T, typename Tuple, typename Val>
concept Leaper = requires(T l, const Tuple &t, std::vector<const Val *> &v) {
    { l.count(t) } -> std::same_as<size_t>;
    { l.propose(t, v) } -> std::same_as<void>;
    { l.intersect(t, v) } -> std::same_as<void>;
};

struct Unit {
    constexpr auto operator<=>(const Unit &) const noexcept = default;
};

template <typename Tuple, typename Val, typename... Leapers>
    requires(Leaper<Leapers, Tuple, Val> && ...)
struct LeaperCollection {
    using value_type = Val;
    std::tuple<Leapers...> leapers;

    constexpr void for_each_count(const Tuple &tuple,
                                  std::invocable<size_t, size_t> auto &&op) {
        for_indices<sizeof...(Leapers)>(
            [&]<size_t I>() { op(I, std::get<I>(leapers).count(tuple)); });
    }

    constexpr void propose(const Tuple &tuple, size_t min_index,
                           std::vector<const Val *> &values) {
        bool found = false;
        for_indices<sizeof...(Leapers)>([&]<size_t I>() {
            if (I == min_index) {
                std::get<I>(leapers).propose(tuple, values);
                found = true;
            }
        });
        if (!found)
            throw std::logic_error("No match found for min_index");
    }

    constexpr void intersect(const Tuple &tuple, size_t min_index,
                             std::vector<const Val *> &values) {
        for_indices<sizeof...(Leapers)>([&]<size_t I>() {
            if (I != min_index)
                std::get<I>(leapers).intersect(tuple, values);
        });
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

    template <typename OtherVal>
    constexpr void propose(const Tuple &, std::vector<const OtherVal *> &) {
        throw std::logic_error(
            "PrefixFilter::propose(): variable apparently unbound");
    }

    template <typename OtherVal>
    constexpr void intersect(const Tuple &, std::vector<const OtherVal *> &) {}

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
    [[no_unique_address]] Unit unit;

    constexpr size_t count(const Tuple &) { return 1; }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &values) {
        values.push_back(&unit);
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
        throw std::logic_error(
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
    std::optional<Key> cached_key;

  public:
    using value_type = Val;
    constexpr ExtendWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        Key key = key_func(prefix);
        if (!cached_key || *cached_key != key) {
            std::span all{relation->elements};
            auto range = df::key_range(all, key,
                                       [](const auto &kv) { return kv.first; });
            start = range.data() - all.data();
            end = start + range.size();
            cached_key = std::move(key);
        }
        return end - start;
    }

    constexpr void propose(const Tuple &, std::vector<const Val *> &values) {
        for (size_t i = start; i < end; i++)
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
    mutable std::optional<Cache> cached_range;

  public:
    using value_type = Val;
    constexpr ExtendAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &) {
        return std::numeric_limits<size_t>::max();
    }
    constexpr void propose(const Tuple &, std::vector<const Val *> &) {
        throw std::logic_error(
            "ExtendAnti::propose(): variable apparently unbound.");
    }

    constexpr void intersect(const Tuple &prefix,
                             std::vector<const Val *> &values) const {
        if (values.empty())
            return;

        Key key = key_func(prefix);
        if (!cached_range || cached_range->key != key) {
            std::span all{relation->elements};
            auto range = df::key_range(all, key,
                                       [](const auto &kv) { return kv.first; });
            size_t range_start = range.data() - all.data();
            cached_range = Cache{key, range_start, range_start + range.size()};
        }

        std::span slice{relation->elements.begin() + cached_range->start,
                        relation->elements.begin() + cached_range->end};
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
    std::optional<std::pair<std::pair<Key, Val>, bool>> cached_key_value;

  public:
    constexpr FilterWith(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);
        if (cached_key_value && cached_key_value->first == kv)
            return cached_key_value->second ? std::numeric_limits<size_t>::max()
                                            : 0;
        bool present = relation->binary_search(kv).has_value();
        cached_key_value = {kv, present};
        return present ? std::numeric_limits<size_t>::max() : 0;
    }

    template <typename OtherVal>
    constexpr void propose(const Tuple &, std::vector<const OtherVal *> &) {
        throw std::logic_error(
            "FilterWith::propose(): variable apparently unbound");
    }

    template <typename OtherVal>
    constexpr void intersect(const Tuple &, std::vector<const OtherVal *> &) {}
};

template <typename Key, typename Val, typename Tuple, typename Func>
class FilterAnti {
    const Relation<std::pair<Key, Val>> *relation;
    Func key_func;
    std::optional<std::pair<std::pair<Key, Val>, bool>> cached_key_value;
    [[no_unique_address]] Unit unit;

  public:
    constexpr FilterAnti(const Relation<std::pair<Key, Val>> *rel, Func f)
        : relation(rel), key_func(std::move(f)) {}

    constexpr size_t count(const Tuple &prefix) {
        auto kv = key_func(prefix);

        if (cached_key_value && cached_key_value->first == kv) {
            return cached_key_value->second
                       ? 0
                       : std::numeric_limits<size_t>::max();
        }

        bool present = relation->binary_search(kv).has_value();
        cached_key_value = {kv, present};

        return present ? 0 : std::numeric_limits<size_t>::max();
    }

    constexpr void propose(const Tuple &, std::vector<const Unit *> &v) {
        v.push_back(&unit);
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

template <typename V, size_t K, size_t PrefixLen> struct PrefixExtractor {
    std::array<int, PrefixLen> positions;
    std::array<V, PrefixLen> operator()(const std::array<V, K> &jp) const {
        return project<PrefixLen>(jp, positions);
    }
};

template <typename V, size_t N, size_t ProposeCol, typename ExtractorT>
class TupleLeaper {
    static constexpr size_t MAX_BATCHES = 32;

    const std::vector<Relation<std::array<V, N>>> *batches;
    std::optional<std::array<V, ProposeCol>> cached_prefix;

    // [start, end)
    using batch_range = std::pair<size_t, size_t>;
    std::array<batch_range, MAX_BATCHES> ranges;
    size_t num_batches = 0;
    size_t total_count = 0;
    ExtractorT extractor;

    void update_ranges(const std::array<V, ProposeCol> &prefix) {
        num_batches = batches->size();
        total_count = 0;
        for (size_t batch_idx = 0; batch_idx < num_batches; batch_idx++) {
            std::span all{(*batches)[batch_idx].elements};
            auto range = key_range(all, prefix, [](const std::array<V, N> &t) {
                return take_prefix<ProposeCol>(t);
            });
            ranges[batch_idx].first = range.data() - all.data();
            ranges[batch_idx].second = ranges[batch_idx].first + range.size();
            total_count += range.size();
        }
        cached_prefix = prefix;
    }

  public:
    using value_type = V;

    TupleLeaper(const std::vector<Relation<std::array<V, N>>> *batch_relations,
                ExtractorT prefix_extractor)
        : batches(batch_relations), extractor(std::move(prefix_extractor)) {}

    template <size_t JoinLen>
    size_t count(const std::array<V, JoinLen> &join_prefix) {
        auto prefix = extractor(join_prefix);
        if (!cached_prefix || *cached_prefix != prefix)
            update_ranges(prefix);
        return total_count;
    }

    template <size_t JoinLen>
    void propose(const std::array<V, JoinLen> &,
                 std::vector<const V *> &values) {
        size_t before = values.size();
        for (size_t batch_idx = 0; batch_idx < num_batches; batch_idx++)
            for (size_t i = ranges[batch_idx].first;
                 i < ranges[batch_idx].second; i++)
                values.push_back(
                    &(*batches)[batch_idx].elements[i][ProposeCol]);

        // We can skip sorting if this is the first/only batch
        if (num_batches > 1)
            std::sort(values.begin() + before, values.end(),
                      [](const V *a, const V *b) { return *a < *b; });
    }

    template <size_t JoinLen>
    void intersect(const std::array<V, JoinLen> &,
                   std::vector<const V *> &values) {
        std::array<std::span<const std::array<V, N>>, MAX_BATCHES> slices;
        for (size_t batch_idx = 0; batch_idx < num_batches; batch_idx++)
            slices[batch_idx] = {(*batches)[batch_idx].elements.begin() +
                                     ranges[batch_idx].first,
                                 (*batches)[batch_idx].elements.begin() +
                                     ranges[batch_idx].second};
        auto write_it = values.begin();
        for (const V *v : values) {
            bool found = false;
            for (size_t batch_idx = 0; batch_idx < num_batches && !found;
                 ++batch_idx) {
                slices[batch_idx] = seek(slices[batch_idx], [v](const auto &t) {
                    return t[ProposeCol] < *v;
                });
                if (!slices[batch_idx].empty() &&
                    slices[batch_idx][0][ProposeCol] == *v)
                    found = true;
            }
            if (found)
                *write_it++ = v;
        }
        values.erase(write_it, values.end());
    }
};

} // namespace df
