# Dartfrog
----

[Datafrog](https://github.com/rust-lang/datafrog) with a few more warts

## What Is It?

This header-only library is a port and expansion of the Datafrog crate to C++

- `#include <dartfrog.hpp>` - Datafrog-equivalent APIs
- `#include <datalog.hpp>` - A Datalog inspired, compile-time DSL

## Building

```bash
cmake -G "Unix Makefiles" -B bin
cmake --build bin
ctest --test-dir bin
```

## Syntax Examples

### Dartfrog

Transitive Closure

```cpp
#include <dartfrog.hpp>

auto [iter1, edge] = Iteration{}.variable<std::pair<int, int>>();
auto [iter, path] = std::move(iter1).variable<std::pair<int, int>>();

edge->insert(Relation<std::pair<int, int>>::from_vec({{1, 2}, {2, 3}}));

while (iter.changed()) {
    path->from_join(*edge, *edge, *path,
                    [](int x, int z, int _) { return std::pair{x, z}; });
}

auto result = std::move(*path).complete();
```

### Dartfrog DSL

Transitive Closure

```cpp
#include <datalog.hpp>


auto x = Var<0>();
auto y = Var<1>();
auto z = Var<2>();

Datalog dl;
Predicate<int, 2> Edge(dl), Path(dl);

std::vector<std::array<int, 2>> edges = {{1, 2}, {2, 3}};
Edge.insert(rel<int>(edges));

dl.add_rule(Path(x, y) %= Edge(x, y));
dl.add_rule(Path(x, z) %= Path(x, y) && Edge(y, z));
dl.solve();

auto result = Path.extract();
```

### Translating Datalog into Dartfrog DSL

Let's translate the datalog rule `head(VAR1, VAR2) :- foo(VAR1), bar(VAR2).`

First you must instantiate a `Datalog` query object. This will act as the executable program we'll add rules to. For example:

```cpp
#include <datalog.hpp>

Datalog dl;
```

You'll also need to declare variables, which can be done with the `DL_VARS()` macro:

```cpp
DL_VARS(VAR1, VAR2);
```

which is syntactic sugar for the equivalent:

```cpp
Var<0> VAR1;
Var<1> VAR2;
```

Note that Dartfrog variables behave much differently than in Datalog. Datalog variables are  placeholder names that are unified during computation, and values are bound identically to any instance of the variable within the program. A Dartfrog variable is nothing more than a column
index with a convenient name. Each head variable will project the indexed columns from the body atoms into the result tuple. 

For now we'll start our rule by declaring our predicates, their arity, and their type.

```cpp
Predicate<int, 2> head(dl); /* head/2 */
Predicate<int, 1> foo(dl);  /* foo/1  */
Predicate<int, 1> bar(dl);  /* bar/1  */
```

Next we can write our rule using our predicates and our variables

```
RULE(dl, head(VAR1, VAR2) <= foo(VAR1), bar(VAR2));
```

And now we can solve this to fixpoint:

```cpp
dl.solve();
```

But we haven't inserted any facts, so this won't do anything yet. In datalog we might write facts like this
```
FOO(1).
FOO(2).

BAR(1).
BAR(2).
```

In Dartfrog we assert a list of facts for each predicate instead:

```cpp
foo.insert(rel({{1}, {2}}));
bar.insert(rel({{1}, {2}}));
```

Now after we run `dl.solve()` we can look at the `head` predicate to see the cross-product in the IDB:

```cpp
auto results = head.extract();
ASSERT_EQ(results, {{1, 1}, {1, 2}, {2, 1}, {2, 2}});
```

If our rule was an equi-join instead like `head(VAR1, VAR2, VAR3) :- foo(VAR1, VAR2), bar(VAR2, VAR3).`, dartfrog would join across the shared VAR2 in both atoms. `make_reindexed` can be used to tell the engine to automatically build secondary indices to perform these kinds of equijoins faster in exchange for more memory.