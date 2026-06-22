#pragma once
#include <array>

#include "../dartfrog.hpp"
#include "var.hpp"
#include "wcoj.hpp"

namespace df::datalog {

struct IPredicate {
    virtual bool step() = 0;
    virtual void snapshot() = 0;
    virtual ~IPredicate() = default;
};

template <class HeadTerm, class PosTuple, class FilterTuple> struct WcojRunner {
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
                auto coll = make_filter_collection<S>(pos);
                head.pred->insert(df::leapjoin(
                    std::span<const std::array<V, NV>>(full.elements), coll,
                    [&](const std::array<V, NV> &a, const df::Unit &) {
                        return project(a);
                    }));
            }
        }
    }

    template <int S>
    auto make_filter_collection(const std::array<int, NV> &pos) const {
        using Tup = std::array<V, NV>;
        return to_unit_coll<Tup>(std::tuple_cat(
            std::make_tuple(df::filters::passthrough<Tup>()),
            make_semijoin_leapers<S>(pos, std::make_index_sequence<NA>{}),
            make_filter_leapers(
                pos,
                std::make_index_sequence<std::tuple_size_v<FilterTuple>>{})));
    }

    template <class Tup, class... E>
    static auto to_unit_coll(std::tuple<E...> &&t) {
        return df::LeaperCollection<Tup, df::Unit, E...>{std::move(t)};
    }

    template <int S, size_t... I>
    auto make_semijoin_leapers(const std::array<int, NV> &pos,
                               std::index_sequence<I...>) const {
        return std::tuple_cat(make_one_semijoin<S, I>(pos)...);
    }

    template <int S, size_t I>
    auto make_one_semijoin(const std::array<int, NV> &pos) const {
        using Tup = std::array<V, NV>;
        constexpr auto ids = atom_ids<PosTuple>();
        constexpr int i1 = ids[I][0], i2 = ids[I][1];
        if constexpr ((int)I == S || i1 < 0 || i2 < 0) {
            return std::tuple<>{};
        } else {
            constexpr auto cpos = invert<NV>(make_order<PosTuple>(S));
            constexpr bool repeated = (i1 == i2);
            constexpr bool seedbound =
                !repeated && (std::max(cpos[i1], cpos[i2]) <= 1);
            if constexpr (repeated || seedbound) {
                auto *pred = std::get<I>(atoms).pred;
                int p1 = pos[i1], p2 = pos[i2];
                df::RelationLeaper<V, V> rl{&pred->snap_fwd};
                return std::make_tuple(
                    rl.template filter_with<Tup>([p1, p2](const Tup &a) {
                        return std::pair<V, V>{a[p1], a[p2]};
                    }));
            } else {
                return std::tuple<>{};
            }
        }
    }

    template <size_t... F>
    auto make_filter_leapers(const std::array<int, NV> &pos,
                             std::index_sequence<F...>) const {
        return std::tuple_cat(make_one_filter<F>(pos)...);
    }

    template <size_t F>
    auto make_one_filter(const std::array<int, NV> &pos) const {
        using Tup = std::array<V, NV>;
        using Filt = std::tuple_element_t<F, FilterTuple>;
        using A = typename filter_vars<Filt>::a_t;
        using B = typename filter_vars<Filt>::b_t;
        constexpr int ia = index_of<A, UV>::value;
        constexpr int ib = index_of<B, UV>::value;
        static_assert(ia >= 0 && ib >= 0,
                      "filter variable not bound by a positive body atom");
        int pa = pos[ia], pb = pos[ib];
        if constexpr (is_negated<Filt>::value) {
            auto *pred = std::get<F>(filters).pred;
            df::RelationLeaper<V, V> rl{&pred->snap_fwd};
            return std::make_tuple(
                rl.template filter_anti<Tup>([pa, pb](const Tup &a) {
                    return std::pair<V, V>{a[pa], a[pb]};
                }));
        } else {
            constexpr Cmp op = filter_vars<Filt>::op;
            return std::make_tuple(
                df::filters::prefix_filter<Tup>([pa, pb](const Tup &a) -> bool {
                    const V &x = a[pa];
                    const V &y = a[pb];
                    if constexpr (op == Cmp::Lt)
                        return x < y;
                    else if constexpr (op == Cmp::Le)
                        return x <= y;
                    else if constexpr (op == Cmp::Gt)
                        return x > y;
                    else if constexpr (op == Cmp::Ge)
                        return x >= y;
                    else if constexpr (op == Cmp::Ne)
                        return x != y;
                    else
                        return x == y;
                }));
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
        WcojRunner<Term<HPred, HV1, HV2>, Pos, Filt> runner{
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
