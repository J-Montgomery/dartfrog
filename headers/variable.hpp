#pragma once

#include <concepts>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "join.hpp"
#include "relation.hpp"

template <typename T>
concept VariableTrait = requires(T v, uint32_t r, std::ostream &s) {
    { v.changed() } -> std::convertible_to<bool>;
    v.dump_stats(r, s);
};

struct IVariable {
    virtual ~IVariable() = default;
    virtual bool changed() = 0;
    virtual void dump_stats(uint32_t round, std::ostream &s) const = 0;
};

template <std::totally_ordered Tuple> class Variable : public IVariable {
  public:
    using value_type = Tuple;

    bool distinct = true;
    std::string name_;

    std::vector<Relation<Tuple>> stable;
    Relation<Tuple> recent_data;
    std::vector<Relation<Tuple>> to_add;

    Variable(std::string name) : distinct(true), name_(std::move(name)) {}

    const std::string &name() const { return name_; }

    std::span<const Tuple> recent() const { return recent_data.elements; }

    void for_each_stable_set(auto &&f) const {
        for (const auto &batch : stable) {
            f(std::span<const Tuple>(batch.elements));
        }
    }

    size_t num_stable() const {
        return std::accumulate(
            stable.begin(), stable.end(), 0ULL,
            [](size_t sum, const auto &rel) { return sum + rel.size(); });
    }

    bool is_stable() const { return recent_data.empty() && to_add.empty(); }

    void dump_stats(uint32_t round, std::ostream &w) const override {
        w << "\"" << name_ << "\", " << round << ", " << num_stable() << ", "
          << recent_data.size() << "\n";
    }

    Relation<Tuple> complete() && {
        if (!is_stable()) {
            throw std::runtime_error("Variable is not stable");
        }

        Relation<Tuple> result;
        while (!stable.empty()) {
            Relation<Tuple> batch = std::move(stable.back());
            stable.pop_back();
            result = std::move(result).merge(std::move(batch));
        }

        return result;
    }

    bool changed() override {
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
                for (const auto &batch : stable) {
                    std::span<const Tuple> slice = batch.elements;

                    std::erase_if(current_to_add.elements, [&](const Tuple &x) {
                        if (slice.size() > 4 * current_to_add.size()) {
                            slice = join::gallop(
                                slice, [&](const Tuple &y) { return y < x; });
                        } else {
                            while (!slice.empty() && slice[0] < x) {
                                slice = slice.subspan(1);
                            }
                        }
                        return !slice.empty() && slice[0] == x;
                    });
                }
            }
            recent_data = std::move(current_to_add);
        }

        return !recent_data.empty();
    }

    void insert(Relation<Tuple> relation) {
        if (!relation.empty()) {
            to_add.push_back(std::move(relation));
        }
    }

    template <std::ranges::input_range R> void extend(R &&range) {
        insert(Relation<Tuple>::from_iter(std::forward<R>(range)));
    }

    template <class Variable1, class Variable2, class OutputVariable,
              class Logic>
    void from_join(const Variable1 &input1, const Variable2 &input2,
                   OutputVariable &output, Logic &&logic) {
        join::join_into(input1, input2, output, std::forward<Logic>(logic));
    }

    template <typename K, typename V1, typename V2 = V1, typename Logic,
              join::JoinInput<std::pair<K, V2>> I2>
    void from_join_filtered(const Variable<std::pair<K, V1>> &input1,
                            const I2 &input2, Logic &&logic) {
        join::join_and_filter_into(input1, input2, *this,
                                   std::forward<Logic>(logic));
    }

    template <typename K, typename V, typename Logic>
        requires std::totally_ordered<K> && std::totally_ordered<V>
    void from_antijoin(const Variable<std::pair<K, V>> &input1,
                       const Relation<K> &input2, Logic &&logic) {
        this->insert(join::antijoin(input1.recent(), input2,
                                    std::forward<Logic>(logic)));
    }

    template <typename SourceTuple, typename LeaperList, typename Logic>
        requires std::totally_ordered<SourceTuple> &&
                 std::totally_ordered<
                     typename std::remove_cvref_t<LeaperList>::value_type>
    void from_leapjoin(const Variable<SourceTuple> &source,
                       LeaperList &&leapers, Logic &&logic) {
        this->insert(leapjoin(source.recent(),
                              std::forward<LeaperList>(leapers),
                              std::forward<Logic>(logic)));
    }
};
