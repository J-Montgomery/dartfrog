#pragma once

#include <concepts>
#include <numeric>
#include <span>
#include <stddef.h>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "dartfrog/join.hpp"
#include "dartfrog/relation.hpp"

namespace df {

template <typename T>
concept IterationVariable = requires(T v) {
    { v.changed() } -> std::convertible_to<bool>;
};

template <std::totally_ordered Tuple> class Variable {
  public:
    using value_type = Tuple;

    bool distinct = true;

    std::vector<Relation<Tuple>> stable;
    Relation<Tuple> recent_data;
    std::vector<Relation<Tuple>> to_add;

    constexpr Variable() : distinct(true) {}

    constexpr std::span<const Tuple> recent() const {
        return recent_data.elements;
    }

    constexpr void for_each_stable_set(auto &&f) const {
        for (const auto &batch : stable) {
            f(std::span<const Tuple>(batch.elements));
        }
    }

    constexpr size_t num_stable() const {
        return std::accumulate(
            stable.begin(), stable.end(), 0ULL,
            [](size_t sum, const auto &rel) { return sum + rel.size(); });
    }

    constexpr bool is_stable() const {
        return recent_data.empty() && to_add.empty();
    }

    constexpr Relation<Tuple> complete() && {
        if (!is_stable()) {
            throw std::logic_error("Variable is not stable");
        }

        Relation<Tuple> result;
        while (!stable.empty()) {
            Relation<Tuple> batch = std::move(stable.back());
            stable.pop_back();
            result = std::move(result).merge(std::move(batch));
        }

        return result;
    }

    constexpr bool changed() {
        if (!recent_data.empty()) {
            Relation<Tuple> current_recent = std::move(recent_data);
            recent_data = Relation<Tuple>{};

            while (!stable.empty() &&
                   stable.back().size() <= 2 * current_recent.size()) {
                auto last = std::move(stable.back());
                stable.pop_back();
                current_recent =
                    std::move(current_recent).merge(std::move(last));
            }
            stable.push_back(std::move(current_recent));
        }

        if (!to_add.empty()) {
            Relation<Tuple> current_to_add = std::move(to_add.back());
            to_add.pop_back();

            while (!to_add.empty()) {
                auto more = std::move(to_add.back());
                to_add.pop_back();
                current_to_add =
                    std::move(current_to_add).merge(std::move(more));
            }

            if (distinct) {
                df::dedup_against(current_to_add, stable);
            }
            recent_data = std::move(current_to_add);
        }

        return !recent_data.empty();
    }

    constexpr void insert(Relation<Tuple> relation) {
        if (relation.empty()) {
            return;
        }
        to_add.push_back(std::move(relation));
        if (distinct && should_compact_pending()) {
            compact_pending();
        }
    }

    template <typename R> constexpr void extend(R &&range) {
        insert(Relation<Tuple>::from_iter(std::forward<R>(range)));
    }

    template <class Input1, class Input2, class OutputVariable, class Logic>
    constexpr void from_join(const Input1 &input1, const Input2 &input2,
                             OutputVariable &output, Logic &&logic) {
        df::join_into(input1, input2, output, std::forward<Logic>(logic));
    }

    template <typename KVTuple, typename Input2, typename Logic>
        requires df::PairLike<KVTuple> &&
                 df::JoinInput<Input2, typename Input2::value_type>
    constexpr void from_join_filtered(const Variable<KVTuple> &input1,
                                      const Input2 &input2, Logic &&logic) {
        df::join_and_filter_into(input1, input2, *this,
                                 std::forward<Logic>(logic));
    }

    template <typename KVTuple, typename Logic>
        requires df::PairLike<KVTuple>
    constexpr void
    from_antijoin(const Variable<KVTuple> &input1,
                  const Relation<typename KVTuple::first_type> &input2,
                  Logic &&logic) {
        this->insert(
            df::antijoin(input1.recent(), input2, std::forward<Logic>(logic)));
    }

    template <typename Input1, typename Collection, typename Logic>
        requires std::totally_ordered<typename Input1::value_type> &&
                 std::totally_ordered<
                     typename std::remove_cvref_t<Collection>::value_type>
    constexpr void from_leapjoin(const Input1 &source, Collection &&leapers,
                                 Logic &&logic) {
        auto result =
            leapjoin(source.recent(), std::forward<Collection>(leapers),
                     std::forward<Logic>(logic));
        this->insert(Relation<Tuple>::from_vec(std::move(result.elements)));
    }

  private:
    static constexpr size_t pending_growth_factor_ = 2;
    static constexpr size_t min_dedup_size_ = 4096;

    constexpr size_t pending_size() const {
        return std::accumulate(
            to_add.begin(), to_add.end(), size_t{0},
            [](size_t sum, const auto &rel) { return sum + rel.size(); });
    }

    constexpr bool should_compact_pending() const {
        size_t reference = recent_data.size();
        if (reference < min_dedup_size_) {
            reference = min_dedup_size_;
        }
        return pending_size() > pending_growth_factor_ * reference;
    }

    constexpr void compact_pending() {
        if (to_add.empty()) {
            return;
        }
        Relation<Tuple> merged = std::move(to_add.back());
        to_add.pop_back();
        while (!to_add.empty()) {
            merged = std::move(merged).merge(std::move(to_add.back()));
            to_add.pop_back();
        }
        df::dedup_against(merged, stable);
        if (!merged.empty()) {
            to_add.push_back(std::move(merged));
        }
    }
};

template <typename T1, typename T2, typename Logic>
void map_into(const Variable<T1> &input, Variable<T2> &output, Logic &&logic) {
    std::vector<T2> results;
    results.reserve(input.recent().size());
    for (const auto &item : input.recent()) {
        results.push_back(logic(item));
    }

    output.insert(Relation<T2>::from_vec(std::move(results)));
}

} // namespace df
