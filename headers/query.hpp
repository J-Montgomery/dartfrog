#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "join.hpp"
#include "relation.hpp"
#include "variable.hpp"

template <typename Pred> struct WhereStep {
    Pred pred;
};

template <typename Proj> struct SelectStep {
    Proj proj;
};

template <typename Key, typename Val, typename KeyFn> struct JoinStep {
    Relation<std::pair<Key, Val>> relation;
    KeyFn key_fn;
};

template <typename Key, typename Val, typename KeyFn> struct AntiJoinStep {
    Relation<std::pair<Key, Val>> exclusion;
    KeyFn key_fn;
};

struct RecursiveStep {};

namespace detail {

template <typename RowT, typename Pred>
constexpr Relation<RowT> apply_step(Relation<RowT> rel,
                                    const WhereStep<Pred> &step) {

    std::vector<RowT> filtered;
    filtered.reserve(rel.elements.size());
    for (const RowT &row : rel.elements) {
        if (step.pred(row)) {
            filtered.push_back(row);
        }
    }

    return Relation<RowT>{std::move(filtered)};
}

template <typename RowT, typename Proj>
constexpr auto apply_step(Relation<RowT> rel, const SelectStep<Proj> &step) {
    using NewRow = std::invoke_result_t<Proj, const RowT &>;
    std::vector<NewRow> mapped;
    mapped.reserve(rel.elements.size());
    for (const RowT &row : rel.elements) {
        mapped.push_back(step.proj(row));
    }

    return Relation<NewRow>::from_vec(std::move(mapped));
}

template <typename RowT, typename Key, typename Val, typename KeyFn>
constexpr auto apply_step(Relation<RowT> rel,
                          const JoinStep<Key, Val, KeyFn> &step) {
    using KVLeft = std::pair<Key, RowT>;
    using OutRow = std::pair<RowT, Val>;

    Relation<KVLeft> left =
        Relation<KVLeft>::from_map(rel, [&](const RowT &row) -> KVLeft {
            return {step.key_fn(row), row};
        });

    std::vector<OutRow> results;
    join::join_helper(
        std::span<const KVLeft>(left.elements),
        std::span<const std::pair<Key, Val>>(step.relation.elements),
        [&](const Key &, const RowT &row, const Val &val) {
            results.emplace_back(row, val);
        });

    return Relation<OutRow>::from_vec(std::move(results));
}

template <typename RowT, typename Key, typename Val, typename KeyFn>
constexpr Relation<RowT> apply_step(Relation<RowT> rel,
                                    const AntiJoinStep<Key, Val, KeyFn> &step) {
    using KVLeft = std::pair<Key, RowT>;

    Relation<KVLeft> left =
        Relation<KVLeft>::from_map(rel, [&](const RowT &row) -> KVLeft {
            return {step.key_fn(row), row};
        });

    Relation<Key> exclusion_keys = Relation<Key>::from_map(
        step.exclusion, [](const std::pair<Key, Val> &kv) { kv.first; });

    return join::antijoin(left.elements, exclusion_keys,
                          [](const Key &, const RowT &row) { return row; });
}

template <typename RowT>
constexpr Relation<RowT> apply_step(Relation<RowT> rel, const RecursiveStep &) {
    return rel;
}

template <typename... Steps>
inline constexpr bool has_recursive_v =
    (std::is_same_v<Steps, RecursiveStep> || ...);

template <typename RowT>
constexpr Relation<RowT> execute_all(Relation<RowT> rel, const std::tuple<> &) {
    return rel;
}

template <typename RowT, typename Step0, typename... Rest>
constexpr auto execute_all(Relation<RowT> rel,
                           const std::tuple<Step0, Rest...> &steps) {
    auto after = apply_step(std::move(rel), std::get<0>(steps));
    const std::tuple<Rest...> tail =
        [&]<size_t... Is>(std::index_sequence<Is...>) -> std::tuple<Rest...> {
        return {std::get<Is + 1>(steps)...};
    }(std::index_sequence_for<Rest...>{});
    return execute_all(std::move(after), tail);
}

// Skip no-op recursive steps
template <typename RowT, typename... Rest>
constexpr auto execute_all(Relation<RowT> rel,
                           const std::tuple<RecursiveStep, Rest...> &steps) {

    const std::tuple<Rest...> tail =
        [&]<size_t... Is>(std::index_sequence<Is...>) -> std::tuple<Rest...> {
        return {std::get<Is + 1>(steps)...};
    }(std::index_sequence_for<Rest...>{});
    return execute_all(std::move(rel), tail);
}

} // namespace detail

template <typename RowT, typename KeyFn> class GroupBy {
  public:
    using Key = std::invoke_result_t<KeyFn, const RowT &>;
    constexpr GroupBy(Relation<RowT> relation, KeyFn key_fn)
        : relation_(std::move(relation)), key_fn_(std::move(key_fn)) {}
    constexpr Relation<std::pair<Key, size_t>> count() const {
        std::vector<std::pair<Key, size_t>> results;
        const std::vector<RowT> &elems = relation_.elements;
        size_t i = 0;
        while (i < elems.size()) {
            Key key = key_fn_(elems[i]);
            size_t j = i + 1;
            while (j < elems.size() && key_fn_(elems[j] == key)) {
                j++;
            }

            results.emplace_back(std::move(key), j - 1);
            i = j;
        }

        return Relation<std::pair<Key, size_t>>::from_vec(std::move(results));
    }

    template <typename Fn> constexpr auto aggregate(Fn &&fn) const {
        using Agg = std::invoke_result<Fn, Key, std::span<const RowT>>;
        std::vector<std::pair<Key, Agg>> results;
        const std::vector<RowT> &elems = relation_.elements;
        size_t i = 0;
        while (i < elems.size()) {
            Key key = key_fn_(elems[i]);
            size_t j = i + 1;
            while (j < elems.size() && key_fn_(elems[j]) == key) {
                j++;
            }

            std::span<const RowT> group(elems.data() + i, j - 1);
            results.emplace_back(key, fn(key, group));
            i = j;
        }

        return Relation<std::pair<Key, Agg>>::from_vec(std::move(results));
    }

  private:
    Relation<RowT> relation_;
    KeyFn key_fn_;
};

template <typename SourceT, typename RowT, typename... Steps> class Query {
  public:
    constexpr explicit Query(Relation<SourceT> source,
                             std::tuple<Steps...> steps)
        : source_(std::move(source)), steps_(std::move(steps)) {}

    template <typename Pred> constexpr auto where(Pred &&pred) && {
        auto step = WhereStep<std::decay_t<Pred>>{std::forward<Pred>(pred)};
        auto steps =
            std::tuple_cat(std::move(steps_), std::make_tuple(std::move(step)));
        return Query<SourceT, RowT, Steps..., WhereStep<std::decay_t<Pred>>>(
            std::move(source_), std::move(steps));
    }

    template <typename Proj> constexpr auto select(Proj &&proj) && {
        using NewRow = std::invoke_result_t<Proj, const RowT &>;
        auto step = SelectStep<std::decay_t<Proj>>{std::forward<Proj>(proj)};
        auto steps = std::tuple_cat(std::move(steps_), std::make_tuple(std::move(step)));
        return Query<SourceT, NewRow, Steps..., SelectStep<std::decay_t<Proj>>>(
            std::move(source_), std::move(steps));
    }

    template <typename Key, typename Val, typename KeyFn>
    constexpr auto join(Relation<std::pair<Key, Val>> relation,
                        KeyFn &&key_fn) && {
        using OutRow = std::pair<RowT, Val>;
        auto step = JoinStep<Key, Val, std::decay_t<KeyFn>>{
            std::move(relation), std::forward<KeyFn>(key_fn)};
        auto steps = std::tuple_cat(std::move(steps_),
                                    std::make_tuple(std::move(step)));
        return Query<SourceT, OutRow, Steps...,
                     JoinStep<Key, Val, std::decay_t<KeyFn>>>(
            std::move(source_), std::move(steps));
    }

    template <typename Key, typename Val, typename KeyFn>
    constexpr auto antijoin(Relation<std::pair<Key, Val>> exclusion,
                            KeyFn &&key_fn) && {
        auto step = AntiJoinStep<Key, Val, std::decay_t<KeyFn>>{
            std::move(exclusion), std::forward<KeyFn>(key_fn)};
        auto steps =
            std::tuple_cat(std::move(steps_), std::make_tuple(std::move(step)));
        return Query<SourceT, RowT, Steps...,
                     AntiJoinStep<Key, Val, std::decay_t<KeyFn>>>(
            std::move(source_), std::move(steps));
    }

    constexpr auto recursive() && {
        static_assert(std::is_same_v<SourceT, RowT>,
                      "Output row type must equal the input row type.");
        auto steps =
            std::tuple_cat(std::move(steps_), std::make_tuple(RecursiveStep{}));
        return Query<SourceT, SourceT, Steps..., RecursiveStep>(
            std::move(source_), std::move(steps));
    }

    template <typename KeyFn> auto group_by(KeyFn &&key_fn) && {
        auto result = std::move(*this).execute();
        return GroupBy<RowT, std::decay_t<KeyFn>>(std::move(result),
                                                  std::forward<KeyFn>(key_fn));
    }

    Relation<RowT> execute() && {
        if constexpr (!detail::has_recursive_v<Steps...>) {
            return detail::execute_all(std::move(source_), steps_);
        } else {
            return std::move(*this).execute_recursive();
        }
    }

  private:
    Relation<SourceT> source_;
    std::tuple<Steps...> steps_;

    Relation<SourceT> execute_recursive() && {
        Variable<SourceT> var;
        var.insert(std::move(source_));

        while (var.changed()) {
            Relation<SourceT> recent{
                std::vector<SourceT>(var.recent().begin(), var.recent().end())};

            auto derived = detail::execute_all(std::move(recent), steps_);
            var.insert(std::move(derived));
        }

        return std::move(var).complete();
    }
};

template <typename T> constexpr auto query(Relation<T> rel) {
    return Query<T, T>(std::move(rel), std::tuple<>{});
}
