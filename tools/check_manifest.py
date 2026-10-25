#!/usr/bin/env python3
"""check_manifest.py — the C++ parameter table equals spec/params.json.

spec/params.json is the authority (SPEC §9). `echo_manifest` prints
core/include/echo/params.h as JSON; this compares the two key by key: order,
name, unit, type, range, default, morph class, law, rate class, smoothing
and labels (rate and smoothing since T-016, closing T-007's item). A drift in
either file goes red.

Usage: check_manifest.py <path to echo_manifest>

Before trusting a clean comparison, the detector is proven on planted drifts
(a default, a label, a missing key, a reordered pair): each must be reported,
or the check itself is vacuous and exits red.
"""
import copy
import json
import pathlib
import subprocess
import sys

FIELDS = ("key", "name", "unit", "type", "min", "max", "def", "morph", "law", "rate", "smoothing")


def diff(spec, cpp):
    """Every mismatch between the spec's params and the C++ table, as text."""
    out = []
    if len(spec) != len(cpp):
        out.append(f"{len(spec)} keys in the spec, {len(cpp)} in the C++ table")
    for i, (s, c) in enumerate(zip(spec, cpp)):
        for f in FIELDS:
            if s.get(f) != c.get(f):
                out.append(f"#{i} {s.get('key')}: {f} spec={s.get(f)!r} c++={c.get(f)!r}")
        if s.get("labels", []) != c.get("labels", []):
            out.append(f"#{i} {s.get('key')}: labels differ")
    return out


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    spec = json.loads((root / "spec" / "params.json").read_text())["params"]
    cpp = json.loads(subprocess.run([sys.argv[1]], check=True, capture_output=True, text=True).stdout)

    # The detector first sees its plants.
    plants = []
    p = copy.deepcopy(cpp); p[13]["def"] += 1; plants.append(("a drifted default", p))
    p = copy.deepcopy(cpp); p[14]["labels"][1] = "pong"; plants.append(("a renamed label", p))
    p = copy.deepcopy(cpp); del p[-1]; plants.append(("a missing key", p))
    p = copy.deepcopy(cpp); p[6], p[7] = p[7], p[6]; plants.append(("a reordered pair", p))
    p = copy.deepcopy(cpp); p[2]["smoothing"] = "law"; plants.append(("a drifted smoothing", p))
    p = copy.deepcopy(cpp); p[2]["rate"] = "block"; plants.append(("a drifted rate class", p))
    for what, planted in plants:
        if not diff(spec, planted):
            print(f"check_manifest: the detector did NOT see {what} (vacuous)", file=sys.stderr)
            return 1

    # The action keys (critic T-016 L2): the C++ table against params.json's actions, with its plant.
    spec_actions = [a["key"] for a in json.loads((root / "spec" / "params.json").read_text()).get("actions", [])]
    cpp_actions = json.loads(subprocess.run([sys.argv[1], "--actions"], check=True, capture_output=True, text=True).stdout)
    renamed = cpp_actions[:-1] + [cpp_actions[-1] + "X"] if cpp_actions else ["X"]  # a C++ action renamed
    if spec_actions == renamed:
        print("check_manifest: the detector did NOT see a renamed action (vacuous)", file=sys.stderr)
        return 1

    problems = diff(spec, cpp)
    if spec_actions != cpp_actions:
        problems.append(f"actions: spec={spec_actions!r} c++={cpp_actions!r}")
    for line in problems:
        print(f"check_manifest: {line}", file=sys.stderr)
    if problems:
        return 1
    print(f"check_manifest: OK — {len(cpp)} keys and {len(cpp_actions)} action(s) equal spec/params.json; {len(plants)} planted drifts seen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
