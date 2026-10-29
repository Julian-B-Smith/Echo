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

Also (T-022): docs/face.md covers every key exactly once, with a keyboard order
1 ... N and the fixedControls on the face in their declared order (face_problems).
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


# A11's structural rule (contract §12.1, §12.2; T-018): every flip-class key declares how it
# switches. Each clause of its declaration (a mode-dependent key lists one per mode, separated by
# ';') must be an equal-gain crossfade with a stated length (ms, or the time.xfadeMs key), a gate,
# a seeded jump, or a deferral. A bare jump of signal-path state is not admitted.
import re
# FULL match per clause (critic T-018 M4: a search would admit "instant, no gate" or "not a seeded jump"):
# an optional "mode = " prefix, then exactly one admitted form, each STARTING the clause.
SWITCH_OK = re.compile(r"(\w+ = )?(equal-gain crossfade, (\d+(\.\d+)? ms|time\.xfadeMs)|seeded jump( .*| ?\(.*)?|deferred: .+|gate( .*)?)")


def switch_problems(params):
    out = []
    for x in params:
        if x.get("morph") != "jump":
            continue
        decl = x.get("switch", "")
        body = decl.split(":", 1)[1] if decl.startswith("moves the read head:") else decl
        clauses = [c.strip() for c in body.split(";")] if decl.startswith("moves the read head:") else [decl.strip()]
        for c in clauses:
            if not SWITCH_OK.fullmatch(c):
                out.append(f"{x['key']}: switch clause {c!r} declares no crossfade length, gate, seeded jump or deferral")
    return out


# T-022 (bar row 16): docs/face.md places every key once, on the face or in the expanded view, with a
# keyboard-order number; the numbers are 1 ... N each once; the fixedControls sit on the face, in
# their declared order, under their declared labels. A key table row reads
# `| <keyboard> | `<key>` | <name> | ... |`; the "## ... face" / "## ... expanded" headings say where.
FACE_ROW = re.compile(r"\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*([^|]+?)\s*\|")


def parse_face(text):
    """[(section, keyboard, key, name)] from the face spec's tables; section is 'face' or 'expanded'."""
    rows, section = [], None
    for line in text.splitlines():
        if line.startswith("## "):
            low = line.lower()
            section = "expanded" if "expanded" in low else ("face" if "face" in low else None)
            continue
        m = FACE_ROW.match(line)
        if m and section:
            rows.append((section, int(m.group(1)), m.group(2), m.group(3)))
    return rows


def face_problems(rows, spec_keys, fixed):
    out = []
    keys = [r[2] for r in rows]
    for k in spec_keys:
        if keys.count(k) == 0:
            out.append(f"face.md: {k} is on neither the face nor the expanded view")
        elif keys.count(k) > 1:
            out.append(f"face.md: {k} appears {keys.count(k)} times")
    for k in sorted(set(keys) - set(spec_keys)):
        out.append(f"face.md: {k} is not a key in params.json")
    nums = sorted(r[1] for r in rows)
    if nums != list(range(1, len(spec_keys) + 1)):
        out.append(f"face.md: the keyboard order is {nums}, not 1 ... {len(spec_keys)} each once")
    last = 0
    for fc in sorted(fixed, key=lambda f: f["k"]):
        hit = [r for r in rows if r[2] == fc["key"]]
        if not hit or hit[0][0] != "face":
            out.append(f"face.md: fixed control {fc['key']} is not on the face")
            continue
        if hit[0][3] != fc["label"]:
            out.append(f"face.md: fixed control {fc['key']} is labelled {hit[0][3]!r}, not {fc['label']!r}")
        if hit[0][1] <= last:
            out.append(f"face.md: fixed control {fc['key']} is out of its declared order")
        last = hit[0][1]
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

    # A11's switch declarations, proven first on a planted bare switch (its crossfade removed).
    full_spec = json.loads((root / "spec" / "params.json").read_text())["params"]
    for what, text in (("a bare switch", "instant"), ("a crossfade with its length removed", "equal-gain crossfade"),
                       ("a negated form", "not a seeded jump")):
        planted = copy.deepcopy(full_spec)
        for x in planted:
            if x["key"] == "route.mode":
                x["switch"] = text
        if not switch_problems(planted):
            print(f"check_manifest: the switch check did NOT see {what} (vacuous)", file=sys.stderr)
            return 1


    # T-022: docs/face.md covers every key; the check first sees its plants (copies with one fault each).
    spec_all = json.loads((root / "spec" / "params.json").read_text())
    spec_keys = [x["key"] for x in spec_all["params"]]
    fixed = spec_all["fixedControls"]
    face_rows = parse_face((root / "docs" / "face.md").read_text())
    face_plants = []
    face_plants.append(("a key missing from the face spec", [r for r in face_rows if r[2] != "time.glide"]))
    face_plants.append(("a key listed twice", face_rows + [face_rows[10]]))
    face_plants.append(("an unknown key", face_rows + [("expanded", 30, "time.bogus", "Bogus")]))
    face_plants.append(("a fixed control moved to the expanded view", [("expanded",) + r[1:] if r[2] == "mix" else r for r in face_rows]))
    face_plants.append(("two fixed controls out of order", [(r[0], {5: 6, 6: 5}.get(r[1], r[1])) + r[2:] for r in face_rows]))
    face_plants.append(("a gap in the keyboard order", [(r[0], 30 if r[1] == 29 else r[1]) + r[2:] for r in face_rows]))
    for what, planted in face_plants:
        if not face_problems(planted, spec_keys, fixed):
            print(f"check_manifest: the face check did NOT see {what} (vacuous)", file=sys.stderr)
            return 1

    problems = diff(spec, cpp) + switch_problems(full_spec) + face_problems(face_rows, spec_keys, fixed)
    if spec_actions != cpp_actions:
        problems.append(f"actions: spec={spec_actions!r} c++={cpp_actions!r}")
    for line in problems:
        print(f"check_manifest: {line}", file=sys.stderr)
    if problems:
        return 1
    nflip = sum(1 for x in full_spec if x.get("morph") == "jump")
    print(f"check_manifest: OK — {len(cpp)} keys and {len(cpp_actions)} action(s) equal spec/params.json; {len(plants)} planted drifts seen; "
          f"all {nflip} flip keys declare an admitted switch (A11; 3 planted switch faults seen); "
          f"docs/face.md places all {len(spec_keys)} keys once, keyboard 1-{len(spec_keys)}, the fixed four in order on the face "
          f"({len(face_plants)} planted faults seen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
