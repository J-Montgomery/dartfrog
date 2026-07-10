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
