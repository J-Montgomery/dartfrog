#pragma once
#include <array>
#include <functional>

#include "../dartfrog.hpp"
#include "query_planner.hpp"
#include "var.hpp"

namespace df::datalog {

struct IPredicate {
    virtual bool step() = 0;
    virtual void snapshot() = 0;
    virtual ~IPredicate() = default;
};

template <class HeadTerm, class PosTuple, class FilterTuple>
struct QueryPlanner {
    HeadTerm head;
    PosTuple atoms;
    FilterTuple filters;

    using V = typename atom_traits<
        std::tuple_element_t<0, PosTuple>>::pred_t::TupleT::first_type;
    using UV = uvars_t<PosTuple>;
    static constexpr size_t NV = list_size<UV>::value;
    static constexpr size_t NA = std::tuple_size_v<PosTuple>;

    void operator()() const {
        [&]<size_t... S>(std::index_sequence<S...>) {
            (do_source<(int)S>(), ...);
        }(std::make_index_sequence<NA>{});
    }

    template <int S> void do_source() const {
        constexpr auto ids = atom_ids<PosTuple>();
        if constexpr (ids[S][0] >= 0 && ids[S][1] >= 0 &&
                      ids[S][0] != ids[S][1]) {
            auto *sp = std::get<S>(atoms).pred;
            auto src = sp->var.recent();
            if (src.empty())
                return;

            std::vector<std::array<V, 2>> init;
            init.reserve(src.size());
            for (auto &kv : src)
                init.push_back({kv.first, kv.second});

            auto full = extend<V, NV, S, 2>(
                df::Relation<std::array<V, 2>>::from_vec(std::move(init)),
                atoms);

            constexpr auto pos = invert<NV>(make_order<PosTuple>(S));
            constexpr int h1 =
                index_of<typename atom_traits<HeadTerm>::v1_t, UV>::value;
            constexpr int h2 =
                index_of<typename atom_traits<HeadTerm>::v2_t, UV>::value;
            static_assert(h1 >= 0 && h2 >= 0,
                          "head variable not bound in body");

            auto project = [&](const std::array<V, NV> &a) {
                return std::pair<V, V>{a[pos[h1]], a[pos[h2]]};
            };

            if constexpr (!has_residual_filters<S, PosTuple, FilterTuple>()) {
                head.pred->insert(
                    df::Relation<std::pair<V, V>>::from_map(full, project));
            } else {
                auto keep = make_residual_test<S>(pos);
                std::vector<std::pair<V, V>> out;
                out.reserve(full.elements.size());
                for (const auto &a : full.elements)
                    if (keep(a))
                        out.push_back({a[pos[h1]], a[pos[h2]]});
                head.pred->insert(
                    df::Relation<std::pair<V, V>>::from_vec(std::move(out)));
            }
        }
    }

    template <int S>
    auto make_residual_test(const std::array<int, NV> &pos) const {
        using Tup = std::array<V, NV>;
        std::vector<std::function<bool(const Tup &)>> tests;

        [&]<size_t... I>(std::index_sequence<I...>) {
            (add_semijoin<S, I>(pos, tests), ...);
        }(std::make_index_sequence<NA>{});

        [&]<size_t... F>(std::index_sequence<F...>) {
            (add_filter<F>(pos, tests), ...);
        }(std::make_index_sequence<std::tuple_size_v<FilterTuple>>{});

        return [tests = std::move(tests)](const Tup &a) {
            for (auto &t : tests)
                if (!t(a))
                    return false;
            return true;
        };
    }

    template <int S, size_t I, class Tests>
    void add_semijoin(const std::array<int, NV> &pos, Tests &tests) const {
        using Tup = std::array<V, NV>;
        constexpr auto ids = atom_ids<PosTuple>();
        constexpr int i1 = ids[I][0], i2 = ids[I][1];
        if constexpr ((int)I != S && i1 >= 0 && i2 >= 0) {
            constexpr auto cpos = invert<NV>(make_order<PosTuple>(S));
            constexpr bool semijoin =
                (i1 == i2) || std::max(cpos[i1], cpos[i2]) <= 1;
            if constexpr (semijoin) {
                const auto *snap = &std::get<I>(atoms).pred->snap_fwd;
                int p1 = pos[i1], p2 = pos[i2];
                tests.push_back([snap, p1, p2](const Tup &a) {
                    return snap->binary_search({a[p1], a[p2]}).has_value();
                });
            }
        }
    }

    template <size_t F, class Tests>
    void add_filter(const std::array<int, NV> &pos, Tests &tests) const {
        using Tup = std::array<V, NV>;
        using Filt = std::tuple_element_t<F, FilterTuple>;
        constexpr int ia = index_of<typename filter_vars<Filt>::a_t, UV>::value;
        constexpr int ib = index_of<typename filter_vars<Filt>::b_t, UV>::value;
        static_assert(ia >= 0 && ib >= 0,
                      "filter variable not bound by a positive body atom");
        int pa = pos[ia], pb = pos[ib];
        if constexpr (is_negated<Filt>::value) {
            const auto *snap = &std::get<F>(filters).pred->snap_fwd;
            tests.push_back([snap, pa, pb](const Tup &a) {
                return !snap->binary_search({a[pa], a[pb]});
            });
        } else {
            constexpr Cmp op = filter_vars<Filt>::op;
            tests.push_back(
                [pa, pb](const Tup &a) { return cmp_apply<op>(a[pa], a[pb]); });
        }
    }
};

class Datalog {
    std::vector<IPredicate *> predicates;
    std::vector<std::function<void()>> evaluators;

  public:
    void register_predicate(IPredicate *p) { predicates.push_back(p); }

    template <class HPred, class HV1, class HV2, class BPred, class BV1,
              class BV2>
    void
    add_rule(const Rule<Term<HPred, HV1, HV2>, Term<BPred, BV1, BV2>> &rule) {
        static constexpr bool direct =
            std::is_same_v<HV1, BV1> && std::is_same_v<HV2, BV2>;
        static constexpr bool swap =
            std::is_same_v<HV1, BV2> && std::is_same_v<HV2, BV1>;
        static_assert(direct || swap,
                      "Rule variables mismatch between Head and Body");
        evaluators.push_back([=]() {
            if constexpr (direct)
                rule.head.pred->insert(
                    df::Relation<typename HPred::TupleT>::from_iter(
                        rule.body.pred->var.recent()));
            else
                rule.head.pred->insert(
                    df::Relation<typename HPred::TupleT>::from_iter(
                        rule.body.pred->rev_var.recent()));
        });
    }

    template <class HPred, class HV1, class HV2, class Pos, class Filt>
    void
    add_rule(const Rule<Term<HPred, HV1, HV2>, Conjunction<Pos, Filt>> &rule) {
        QueryPlanner<Term<HPred, HV1, HV2>, Pos, Filt> runner{
            rule.head, rule.body.pos, rule.body.filt};
        evaluators.push_back([runner = std::move(runner)]() { runner(); });
    }

    void solve() {
        while (true) {
            bool changed = false;
            for (auto *p : predicates)
                if (p->step())
                    changed = true;
            if (!changed)
                break;
            for (auto *p : predicates)
                p->snapshot();
            for (auto &e : evaluators)
                e();
        }
    }
};

template <typename T1, typename T2> struct Predicate : IPredicate {
    using TupleT = std::pair<T1, T2>;
    using RevTupleT = std::pair<T2, T1>;

    df::Variable<TupleT> var;
    df::Variable<RevTupleT> rev_var;
    df::Relation<TupleT> snap_fwd;
    df::Relation<RevTupleT> snap_rev;

    Predicate(Datalog &dl) { dl.register_predicate(this); }

    void insert(const df::Relation<TupleT> &rel) {
        var.insert(rel);
        std::vector<RevTupleT> rv;
        rv.reserve(rel.size());
        for (const auto &p : rel)
            rv.push_back({p.second, p.first});
        rev_var.insert(df::Relation<RevTupleT>::from_vec(std::move(rv)));
    }

    template <class Tup>
    static void fold_delta(df::Relation<Tup> &snap,
                           const df::Variable<Tup> &v) {
        if (v.recent_data.empty())
            return;
        snap = std::move(snap).merge(
            df::Relation<Tup>(std::vector<Tup>(v.recent_data.elements)));
    }

    void snapshot() override {
        fold_delta(snap_fwd, var);
        fold_delta(snap_rev, rev_var);
    }

    bool step() override {
        bool c1 = var.changed();
        bool c2 = rev_var.changed();
        return c1 || c2;
    }
    template <class V1, class V2> auto operator()(V1 v1, V2 v2) {
        return Term<Predicate, V1, V2>{this, v1, v2};
    }
    std::vector<TupleT> extract() { return std::move(var).complete().elements; }
};
} // namespace df::datalog
