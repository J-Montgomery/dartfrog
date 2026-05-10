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

template<typename T, typename Func>
requires std::invocable<Func, const T&> && (sizeof(T) > 0)
size_t binary_search(const std::vector<T>& vec, Func&& cmp) {
    static_assert(sizeof(T) > 0, "Binary search on zero-sized types is not supported.");

    size_t hi = vec.size();
    size_t lo = 0;

    const T* data = vec.data();

    while (lo < hi) {
        size_t mid = std::midpoint(hi, lo);
        const T& el = data[mid];

        if (cmp(el)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

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

namespace filters {
    template<typename Tuple, typename Func>
    requires std::predicate<Func, const Tuple&>
    struct PrefixFilter {
        Func predicate;

        static auto from(Func pred) {
            return PrefixFilter<Tuple, Func>{ std::move(pred) };
        }

        size_t count(const Tuple& prefix) {
            return predicate(prefix) ? std::numeric_limits<size_t>::max() : 0;
        }

        void propose(const Tuple&, std::vector<const Unit*>&) {
            throw std::runtime_error("PrefixFilter::propose(): variable apparently unbound");
        }

        void intersect(const Tuple&, std::vector<const Unit*>&) { }

        void for_each_count(const Tuple& tuple, auto&& op) {
            if (this->count(tuple) == 0) {
                op(0, 0);
            } else {
                op(0, 1);
            }
        }

        void propose(const Tuple&, size_t min_index, std::vector<const Unit*>& values) {
            if (min_index != 0) throw std::runtime_error("Index mismatch");
            values.push_back(&UNIT_INSTANCE);
        }

        void intersect(const Tuple&, size_t min_index, std::vector<const Unit*>& values) {
            if (min_index != 0 || values.size() != 1) throw std::runtime_error("Logic error");
        }
    };
    template<typename Tuple>
    struct Passthrough {
        size_t count(const Tuple&) { return 1; }

        void propose(const Tuple&, std::vector<const Unit*>& values) {
            values.push_back(&UNIT_INSTANCE);
        }

        void intersect(const Tuple&, std::vector<const Unit*>&) {}
    };

    template<typename Tuple>
    auto passthrough() { return Passthrough<Tuple>{}; }
    template<typename Tuple, typename Val, typename Func>
    requires std::predicate<Func, const Tuple&, const Val&>
    struct ValueFilter {
        Func predicate;

        static auto from(Func pred) {
            return ValueFilter<Tuple, Val, Func>{ std::move(pred) };
        }

        size_t count(const Tuple&) {
            return std::numeric_limits<size_t>::max();
        }

        void propose(const Tuple&, std::vector<const Val*>&) {
            throw std::runtime_error("ValueFilter::propose(): variable apparently unbound");
        }

        void intersect(const Tuple& prefix, std::vector<const Val*>& values) {
            std::erase_if(values, [&](const Val* val) {
                return !predicate(prefix, *val);
            });
        }
    };

} // namespace filters

namespace extend_with {
    template<typename Key, typename Val, typename Tuple, typename Func>
    class ExtendWith {
        const Relation<std::pair<Key, Val>>* relation;
        size_t start = 0, end = 0;
        Func key_func;
        std::optional<Key> old_key;

    public:
        ExtendWith(const Relation<std::pair<Key, Val>>* rel, Func f) : relation(rel), key_func(std::move(f)) {}

        size_t count(const Tuple& prefix) {
            Key key = key_func(prefix);
            if (!old_key || *old_key != key) {
                auto it = std::lower_bound(relation->elements.begin(), relation->elements.end(), key,
                    [](const auto& x, const auto& k) { return x.first < k; });
                start = std::distance(relation->elements.begin(), it);
                std::span slice1{relation->elements.begin() + start, relation->elements.end()};
                auto slice2 = gallop(slice1, [&](const auto& x) { return x.first <= key; });
                end = relation->len() - slice2.size();
                old_key = std::move(key);
            }
            return end - start;
        }

        void propose(const Tuple&, std::vector<const Val*>& values) {
            for (size_t i = start; i < end; ++i) values.push_back(&relation->elements[i].second);
        }

        void intersect(const Tuple&, std::vector<const Val*>& values) {
            std::span slice{relation->elements.begin() + start, relation->elements.begin() + end};
            std::erase_if(values, [&](const Val* v) {
                slice = gallop(slice, [&](const auto& kv) { return kv.second < *v; });
                return slice.empty() || !(slice[0].second == *v);
            });
        }

        void for_each_count(const Tuple& t, auto&& op) { op(0, count(t)); }
        void propose(const Tuple& t, size_t idx, std::vector<const Val*>& v) { propose(t, v); }
        void intersect(const Tuple&, size_t, std::vector<const Val*>&) {}
    };
}

namespace extend_anti {
    template<typename Key, typename Val, typename Tuple, typename Func>
    class ExtendAnti {
        const Relation<std::pair<Key, Val>>* relation;
        Func key_func;
        struct Cache { Key key; size_t start; size_t end; };
        std::optional<Cache> old_key;

    public:
        ExtendAnti(const Relation<std::pair<Key, Val>>* rel, Func f) : relation(rel), key_func(std::move(f)) {}

        size_t count(const Tuple&) { return std::numeric_limits<size_t>::max(); }

        void intersect(const Tuple& prefix, std::vector<const Val*>& values) {
            Key key = key_func(prefix);
            if (!old_key || old_key->key != key) {
                auto it = std::lower_bound(relation->elements.begin(), relation->elements.end(), key,
                    [](const auto& x, const auto& k) { return x.first < k; });
                size_t s = std::distance(relation->elements.begin(), it);
                std::span s1{relation->elements.begin() + s, relation->elements.end()};
                auto s2 = gallop(s1, [&](const auto& x) { return x.first <= key; });
                old_key = {key, s, relation->len() - s2.size()};
            }

            std::span slice{relation->elements.begin() + old_key->start, relation->elements.begin() + old_key->end};
            if (!slice.empty()) {
                std::erase_if(values, [&](const Val* v) {
                    slice = gallop(slice, [&](const auto& kv) { return kv.second < *v; });
                    return !slice.empty() && slice[0].second == *v;
                });
            }
        }
    };
}

namespace filter_with {
    template<typename Key, typename Val, typename Tuple, typename Func>
    class FilterWith {
        const Relation<std::pair<Key, Val>>* relation;
        Func key_func;
        std::optional<std::pair<std::pair<Key, Val>, bool>> old_kv;

    public:
        FilterWith(const Relation<std::pair<Key, Val>>* rel, Func f) : relation(rel), key_func(std::move(f)) {}

        size_t count(const Tuple& prefix) {
            auto kv = key_func(prefix);
            if (old_kv && old_kv->first == kv) return old_kv->second ? std::numeric_limits<size_t>::max() : 0;
            bool present = std::binary_search(relation->elements.begin(), relation->elements.end(), kv);
            old_kv = {kv, present};
            return present ? std::numeric_limits<size_t>::max() : 0;
        }

        void for_each_count(const Tuple& t, auto&& op) { op(0, count(t) == 0 ? 0 : 1); }
        void propose(const Tuple&, size_t, std::vector<const Unit*>& v) { v.push_back(&UNIT_INSTANCE); }
        void intersect(const Tuple&, size_t, std::vector<const Unit*>& v) {}
    };
}

namespace filter_anti {
    template<typename Key, typename Val, typename Tuple, typename Func>
    class FilterAnti {
        const Relation<std::pair<Key, Val>>* relation;
        Func key_func;
        std::optional<std::pair<std::pair<Key, Val>, bool>> old_kv;

    public:
        FilterAnti(const Relation<std::pair<Key, Val>>* rel, Func f)
            : relation(rel), key_func(std::move(f)) {}

        size_t count(const Tuple& prefix) {
            auto kv = key_func(prefix);

            if (old_kv && old_kv->first == kv) {
                return old_kv->second ? 0 : std::numeric_limits<size_t>::max();
            }

            bool present = std::binary_search(relation->elements.begin(),
                                              relation->elements.end(), kv);
            old_kv = {kv, present};

            return present ? 0 : std::numeric_limits<size_t>::max();
        }

        void propose(const Tuple&, std::vector<const void*>&) {
            throw std::runtime_error("FilterAnti::propose(): variable unbound.");
        }

        void intersect(const Tuple&, std::vector<const void*>&) { }

        void for_each_count(const Tuple& t, auto&& op) {
            op(0, count(t) == 0 ? 0 : 1);
        }

        void propose(const Tuple&, size_t min_index, std::vector<const Unit*>& values) {
            if (min_index != 0) throw std::logic_error("min_index mismatch");
            values.push_back(&UNIT_INSTANCE);
        }

        void intersect(const Tuple&, size_t min_index, std::vector<const Unit*>& values) {
            if (min_index != 0 || values.size() != 1) throw std::logic_error("logic error");
        }
    };
}

template<typename Key, typename Val>
struct RelationLeaper {
    const Relation<std::pair<Key, Val>>* self;

    template<typename Tuple, typename Func>
    auto extend_with(Func&& f) const { return extend_with::ExtendWith<Key, Val, Tuple, Func>(self, std::forward<Func>(f)); }

    template<typename Tuple, typename Func>
    auto extend_anti(Func&& f) const { return extend_anti::ExtendAnti<Key, Val, Tuple, Func>(self, std::forward<Func>(f)); }

    template<typename Tuple, typename Func>
    auto filter_with(Func&& f) const { return filter_with::FilterWith<Key, Val, Tuple, Func>(self, std::forward<Func>(f)); }

    template<typename Tuple, typename Func>
    auto filter_anti(Func&& f) const {
        return filter_anti::FilterAnti<Key, Val, Tuple, Func>(self, std::forward<Func>(f));
    }
};
