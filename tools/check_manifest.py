#!/usr/bin/env python3
"""check_manifest.py — the C++ parameter table equals spec/params.json.

spec/params.json is the authority (SPEC §9). `echo_manifest` prints
core/include/echo/params.h as JSON; this compares the two key by key: order,
name, unit, type, range, default, morph class, law and labels. A drift in
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

FIELDS = ("key", "name", "unit", "type", "min", "max", "def", "morph", "law")


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
    for what, planted in plants:
        if not diff(spec, planted):
            print(f"check_manifest: the detector did NOT see {what} (vacuous)", file=sys.stderr)
            return 1

    problems = diff(spec, cpp)
    for line in problems:
        print(f"check_manifest: {line}", file=sys.stderr)
    if problems:
        return 1
    print(f"check_manifest: OK — {len(cpp)} keys equal spec/params.json; {len(plants)} planted drifts seen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
