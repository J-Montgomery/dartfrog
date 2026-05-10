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
    using TupleType = Tuple;

    bool distinct = true;
    std::string name_;

    std::vector<Relation<Tuple>> stable;
    Relation<Tuple> recent;
    std::vector<Relation<Tuple>> to_add;

    Variable(std::string name) : distinct(true), name_(std::move(name)) {}

    const std::string &name() const { return name_; }

    size_t num_stable() const {
        return std::accumulate(
            stable->begin(), stable->end(), 0ULL,
            [](size_t sum, const auto &rel) { return sum + rel.size(); });
    }

    bool is_stable() const { return recent->empty() && to_add->empty(); }

    void dump_stats(uint32_t round, std::ostream &w) const override {
        w << "\"" << name_ << "\", " << round << ", " << num_stable() << ", "
          << recent->size() << "\n";
    }

    Relation<Tuple> complete() && {
        if (!stable->empty()) {
            throw std::runtime_error("Variable is not stable");
        }

        Relation<Tuple> result;
        while (!stable->empty()) {
            Relation<Tuple> batch = std::move(stable->back());
            stable->pop_back();
            result = std::move(result).merge(std::move(batch));
        }

        return result;
    }

    void insert(Relation<Tuple> relation) {
        if (!relation.empty()) {
            to_add->push_back(std::move(relation));
        }
    }

    template <std::ranges::input_range R> void extend(R &&range) {
        insert(Relation<Tuple>::from_iter(std::forward<R>(range)));
    }

    template <typename K, typename V1, typename V2, typename Res,
              typename Logic, join::JoinInput<std::pair<K, V2>> I2>
    void from_join(const Variable<std::pair<K, V1>> &input1, const I2 &input2,
                   Variable<Res> &output, Logic &&logic) {
        join::join_into(input1, input2, *this, std::forward<Logic>(logic));
    }

    template <typename K, typename V1, typename V2, typename Logic,
              join::JoinInput<std::pair<K, V2>> I2>
        requires std::totally_ordered<K> && std::totally_ordered<V1> &&
                 std::totally_ordered<V2>
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

    template <typename SourceTuple, typename Val, typename LeaperList,
              typename Logic>
        requires std::totally_ordered<SourceTuple> && std::totally_ordered<Val>
    void from_leapjoin(const Variable<SourceTuple> &source,
                       LeaperList &&leapers, Logic &&logic) {
        // this->insert(treefrog::leapjoin(source.recent(),
        //                                 std::forward<LeaperList>(leapers),
        //                                 std::forward<Logic>(logic)));
    }
};
