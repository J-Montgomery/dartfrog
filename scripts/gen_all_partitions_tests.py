#!/usr/bin/env python3
"""Generate all-set-partitions test cases for the Dartfrog query planner.

Creates tests by partitioning N=3..6 variables all possible non-trivial
partitions (size >= 2). Each partition is a possible join graph.
The expected values are provided by souffle.

Counts (OEIS: A000296): n=3:1, n=4:4, n=5:11, n=6:41
"""

import os
import subprocess
import tempfile


# Compute all possible bell partitions for [elements]
def set_partitions(elements):
    if not elements:
        yield []
        return
    first, rest = elements[0], elements[1:]
    for partition in set_partitions(rest):
        for i in range(len(partition)):
            new_partition = [block[:] for block in partition]
            new_partition[i].append(first)
            yield new_partition
        yield [[first]] + partition


def has_no_singletons(partition):
    return all(len(block) >= 2 for block in partition)


def build_rule(all_edges, n):
    if not all_edges:
        return None
    all_vars = set()
    for a, b in all_edges:
        all_vars.add(a)
        all_vars.add(b)
    if all_vars != set(range(n)):
        return None
    head_vars = ", ".join("Var<%d>{}" % i for i in range(n))
    head = "result(%s)" % head_vars
    atoms = []
    for a, b in all_edges:
        atoms.append("edge(Var<%d>{}, Var<%d>{})" % (a, b))
    body = " && ".join(atoms)
    return head, body


def compute_expected_souffle(partition, n):
    chains = [sorted(block) for block in partition]
    base = 0
    edge_data = []
    for chain_vars in chains:
        for i in range(len(chain_vars) - 1):
            edge_data.append((base + chain_vars[i], base + chain_vars[i + 1]))
        base += 1000

    atoms = []
    for chain_vars in chains:
        for i in range(len(chain_vars) - 1):
            atoms.append((chain_vars[i], chain_vars[i + 1]))

    with tempfile.TemporaryDirectory() as tmpdir:
        dl_file = os.path.join(tmpdir, "query.dl")
        facts_file = os.path.join(tmpdir, "edge.facts")
        output_file = os.path.join(tmpdir, "result.output")

        head_vars_str = ", ".join("V%d" % i for i in range(n))
        body_parts = []
        for a, b in atoms:
            body_parts.append("edge(V%d, V%d)" % (a, b))
        body = " :- " + ", ".join(body_parts) + "."
        rule = "result(%s)%s" % (head_vars_str, body)

        with open(dl_file, "w") as f:
            f.write(".decl edge(V1: number, V2: number)\n")
            f.write(
                ".decl result(%s)\n" % ", ".join("V%d: number" % i for i in range(n))
            )
            f.write('.input edge(IO="file", filename="edge.facts", delimiter=",")\n')
            f.write(".output result\n")
            f.write(rule + "\n")

        with open(facts_file, "w") as f:
            for a, b in edge_data:
                f.write("%d,%d\n" % (a, b))

        result = subprocess.run(
            ["souffle", "-D", tmpdir, dl_file],
            capture_output=True,
            timeout=30,
            cwd=tmpdir,
        )
        if result.returncode != 0:
            return []

        expected = []
        csv_file = os.path.join(tmpdir, "result.csv")
        if os.path.exists(csv_file):
            with open(csv_file, "r") as f:
                for line in f:
                    line = line.strip()
                    if line:
                        vals = tuple(int(x) for x in line.split("\t"))
                        expected.append(vals)

        return sorted(expected)


def generate_test_func(n, partition, idx):
    all_edges = []
    chain_blocks = []
    for block in partition:
        sorted_block = sorted(block)
        chain_blocks.append(sorted_block)
        for i in range(len(sorted_block) - 1):
            all_edges.append((sorted_block[i], sorted_block[i + 1]))

    result = build_rule(all_edges, n)
    if result is None:
        return None

    head, body = result
    rule = "%s %%= %s" % (head, body)

    parts_str = "_".join("_".join(str(v) for v in sorted(b)) for b in partition)
    func_name = "n%d_p%d_%s" % (n, idx, parts_str)

    expected = compute_expected_souffle(partition, n)

    edge_vals = []
    base = 0
    for chain_vars in chain_blocks:
        for i in range(len(chain_vars) - 1):
            a = base + chain_vars[i]
            b = base + chain_vars[i + 1]
            edge_vals.append("{%d, %d}" % (a, b))
        base += 1000
    edges_code = "{%s}" % ", ".join(edge_vals)
    insert_code = "    edge.insert(df::Relation<Edge>::from_vec(%s));" % edges_code

    expected_code = (
        "{"
        + ", ".join("{%s}" % ", ".join(str(v) for v in tup) for tup in expected)
        + "}"
    )

    lines = []
    lines.append("TEST(AllPartitions, %s) {" % func_name)
    lines.append("    Datalog dl;")
    lines.append("    Predicate<int32_t, %d> result(dl);" % n)
    lines.append("    Predicate<int32_t, 2> edge(dl);")
    lines.append("    dl.add_rule(%s);" % rule)
    lines.append("    edge.insert(df::Relation<Edge>::from_vec(%s));" % edges_code)
    lines.append("    dl.solve();")
    lines.append("    auto actual = result.extract();")
    lines.append("    std::sort(actual.begin(), actual.end());")
    lines.append(
        "    std::vector<std::array<int32_t, %d>> expected = %s;" % (n, expected_code)
    )
    lines.append("    ASSERT_EQ(actual.size(), expected.size());")
    lines.append("    for (size_t i = 0; i < actual.size(); i++)")
    lines.append("        ASSERT_EQ(actual[i], expected[i]);")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    output_path = os.path.join(project_dir, "tests", "all_partitions_tests.cpp")

    lines = []
    lines.append("// Auto-generated by gen_all_partitions_tests.py")
    lines.append("#include <gtest/gtest.h>")
    lines.append("#include <algorithm>")
    lines.append("#include <array>")
    lines.append("#include <vector>")
    lines.append("")
    lines.append('#include "dartfrog.hpp"')
    lines.append('#include "datalog.hpp"')
    lines.append("")
    lines.append("")
    lines.append("using df::datalog::Datalog;")
    lines.append("using df::datalog::Predicate;")
    lines.append("using df::datalog::Var;")
    lines.append("using Edge = std::array<int32_t, 2>;")
    lines.append("")

    total = 0
    for n in range(3, 7):
        elements = list(range(n))
        partitions = [p for p in set_partitions(elements) if has_no_singletons(p)]

        for idx, partition in enumerate(partitions):
            func = generate_test_func(n, partition, idx)
            if func:
                lines.append(func)
                total += 1

    lines.append("")

    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    print("Generated %d test cases for n=3..6" % total)


if __name__ == "__main__":
    main()
