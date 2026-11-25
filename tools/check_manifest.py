#!/usr/bin/env python3
"""check_manifest.py — the C++ parameter table equals spec/params.json.

spec/params.json is the authority (SPEC §9). `echo_manifest` prints
core/include/echo/params.h as JSON; this compares the two key by key: order,
name, unit, type, range, default, morph class, law, rate class, smoothing
and labels (rate and smoothing since T-016, closing T-007's item). A drift in
either file goes red. Since T-045 it also compares the fixed row (`echo_manifest
--fixed`, params.h kFixedControls) with params.json's `fixedControls`, validates
a keyed entry (`keyBy`) by horde's proposed refusals, and reads each flip key's
§12.7 `switch` object as well as the older prose form.

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


# A11's structural rule (contract §12.1, §12.2; T-018): every flip-class key declares how it
# switches. Each clause of its declaration (a mode-dependent key lists one per mode, separated by
# ';') must be an equal-gain crossfade with a stated length (ms, or the time.xfadeMs key), a gate,
# a seeded jump, or a deferral. A bare jump of signal-path state is not admitted.
import re
# FULL match per clause (critic T-018 M4: a search would admit "instant, no gate" or "not a seeded jump"):
# an optional "mode = " prefix, then exactly one admitted form, each STARTING the clause.
SWITCH_OK = re.compile(r"(\w+ = )?(equal-gain crossfade, (\d+(\.\d+)? ms|time\.xfadeMs)|seeded jump( .*| ?\(.*)?|deferred: .+|gate( .*)?)")


# §12.7 (ADR-309): a `switch` OBJECT: `method` one of four, `length_s` in seconds (0 for silent), `law` where it applies.
# `crossfade` is the equal-gain crossfade, the only law admitted, so it must say so and must have a length; Echo declares no `of`.
SWITCH_METHODS = ("crossfade", "gate", "seeded-jump", "silent")


def switch_object_problems(key, sw):
    out = []
    if sw.get("method") not in SWITCH_METHODS:
        out.append(f"{key}: switch method {sw.get('method')!r} is outside crossfade, gate, seeded-jump, silent")
    length = sw.get("length_s")
    if isinstance(length, bool) or not isinstance(length, (int, float)) or length < 0:
        out.append(f"{key}: switch length_s {length!r} is missing or not a number of seconds")
    elif sw.get("method") == "crossfade" and length == 0:
        out.append(f"{key}: a crossfade of length 0 s is a bare jump")
    elif sw.get("method") == "silent" and length != 0:
        out.append(f"{key}: a silent switch has length_s 0, not {length!r}")
    if sw.get("method") == "crossfade" and sw.get("law") != "equal-gain":
        out.append(f"{key}: a crossfade declares law 'equal-gain' (the only law admitted), not {sw.get('law')!r}")
    if sw.get("method") == "silent" and "law" in sw:
        out.append(f"{key}: a silent switch blends nothing, so it declares no law (got {sw.get('law')!r})")
    for extra in set(sw) - {"method", "length_s", "law"}:
        out.append(f"{key}: switch field {extra!r}: Echo declares no `of`, and only method, length_s and law are read here")
    return out


# Prose is the older form, kept ONLY for these two keys until horde answers echo-005 Ask 2 (a §12.7 form that names
# time.change as the switch's source). Every other flip key declares the object.
PROSE_UNTIL_ECHO_005_ASK_2 = ("time.mode", "time.div")


def switch_problems(params):
    """Every flip key's switch, in either admitted form: a §12.7 object, or the older prose (time.mode and time.div,
    until echo-005 Ask 2 is answered)."""
    out = []
    for x in params:
        if x.get("morph") != "jump":
            continue
        decl = x.get("switch", "")
        if isinstance(decl, dict):
            out += switch_object_problems(x["key"], decl)
            continue
        if not isinstance(decl, str):
            out.append(f"{x['key']}: switch {decl!r} is neither a §12.7 object nor prose")
            continue
        if x["key"] not in PROSE_UNTIL_ECHO_005_ASK_2:
            out.append(f"{x['key']}: switch is prose; only {', '.join(PROSE_UNTIL_ECHO_005_ASK_2)} may stay prose (echo-005 Ask 2), every other key declares a §12.7 object")
            continue
        body = decl.split(":", 1)[1] if decl.startswith("moves the read head:") else decl
        clauses = [c.strip() for c in body.split(";")] if decl.startswith("moves the read head:") else [decl.strip()]
        for c in clauses:
            if not SWITCH_OK.fullmatch(c):
                out.append(f"{x['key']}: switch clause {c!r} declares no crossfade length, gate, seeded jump or deferral")
    return out


def display_problems(params):
    """`display` (the names a host shows; D-050 A5 ruling 10): required on route.mode, optional elsewhere; always a list of
    non-empty strings, one per label."""
    out = []
    for x in params:
        d = x.get("display")
        if d is None:
            if x["key"] == "route.mode":
                out.append("route.mode: display is required (Standard, Ping-Pong, Mid/Side for horde's buttons)")
            continue
        labels = x.get("labels", [])
        if not isinstance(d, list) or not all(isinstance(n, str) and n for n in d):
            out.append(f"{x['key']}: display must be a list of non-empty strings")
        elif len(d) != len(labels):
            out.append(f"{x['key']}: display has {len(d)} names for {len(labels)} labels")
    return out


# The fixed row (RS-5.8, SPEC §16.4; T-045). A plain entry names `key`; a keyed one (horde ADR-399) names `keyBy`: the stepped
# key whose label picks the key the entry carries. horde's proposed refusals, each with a planted control in main().
def fixed_problems(params, fixed):
    out = []
    by = {x["key"]: x for x in params}
    keyed = [f for f in fixed if "keyBy" in f]
    if len(keyed) > 1:
        out.append(f"{len(keyed)} fixed entries are keyed: at most one keyBy entry")
    for f in fixed:
        if ("key" in f) == ("keyBy" in f):
            out.append(f"fixed entry {f.get('k')}: names exactly one of `key` and `keyBy`")
            continue
        if "key" in f:
            if f["key"] not in by:
                out.append(f"fixed entry {f['k']}: key {f['key']!r} is not a key of the module")
            elif by[f["key"]].get("morph") == "device":
                out.append(f"fixed entry {f['k']}: key {f['key']!r} is a Device-class key (RS-4.5)")
            continue
        kb = f["keyBy"]
        if not isinstance(kb, dict) or not isinstance(kb.get("map"), dict):
            out.append(f"fixed entry {f['k']}: keyBy must be an object with a `map` object")
            continue
        sel = by.get(kb.get("key"))
        if sel is None or sel.get("type") != "stepped":
            out.append(f"fixed entry {f['k']}: keyBy.key {kb.get('key')!r} is not a stepped key of the module")
            continue
        m = kb.get("map", {})
        for lab in sel.get("labels", []):
            if lab not in m:
                out.append(f"fixed entry {f['k']}: the label {lab!r} of {sel['key']} is missing from the keyBy map")
        for lab in m:
            if lab not in sel.get("labels", []):
                out.append(f"fixed entry {f['k']}: the keyBy map names {lab!r}, which is not a label of {sel['key']}")
        for lab, k in m.items():
            if k == kb.get("key"):
                out.append(f"fixed entry {f['k']}: the keyBy map sends {lab!r} to its own selector key {k!r}")
            elif k not in by:
                out.append(f"fixed entry {f['k']}: the keyBy map sends {lab!r} to {k!r}, which is not a key of the module")
            elif by[k].get("morph") == "device":
                out.append(f"fixed entry {f['k']}: the keyBy map sends {lab!r} to the Device-class key {k!r} (RS-4.5)")
        if len(set(m.values())) > 2:
            out.append(f"fixed entry {f['k']}: the keyBy map names {len(set(m.values()))} keys: at most two (one reserved host id per slot)")
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
    nswitch = 0
    # (what, key, declaration). Prose faults are planted on time.div, a key that may still be prose, so each fails for its own
    # reason; object faults on route.mode. route.mode with its OLD VALID prose must be refused (prose is for two keys only).
    for what, key, text in (("a bare switch", "time.div", "instant"),
                            ("a crossfade with its length removed", "time.div", "equal-gain crossfade"),
                            ("a negated form", "time.div", "not a seeded jump"),
                            ("route.mode with its old valid prose (prose is for time.mode and time.div only)", "route.mode", "equal-gain crossfade, 20 ms"),
                            # the §12.7 object form (T-045)
                            ("an object whose method is outside the four", "route.mode", {"method": "instant", "length_s": 0}),
                            ("an object crossfade with no length_s", "route.mode", {"method": "crossfade", "law": "equal-gain"}),
                            ("an object crossfade of another law", "route.mode", {"method": "crossfade", "length_s": 0.02, "law": "equal-power"}),
                            ("an object silent switch with a length", "route.mode", {"method": "silent", "length_s": 0.02}),
                            ("an object silent switch with a law", "route.mode", {"method": "silent", "length_s": 0, "law": "equal-gain"}),
                            ("an object with a field this check does not know", "route.mode", {"method": "gate", "length_s": 0.01, "of": "the moon"}),
                            ("a key with no switch at all", "route.mode", None)):
        planted = copy.deepcopy(full_spec)
        for x in planted:
            if x["key"] == key:
                if text is None:
                    del x["switch"]
                else:
                    x["switch"] = text
        if not switch_problems(planted):
            print(f"check_manifest: the switch check did NOT see {what} (vacuous)", file=sys.stderr)
            return 1
        nswitch += 1

    ndisplay = 0
    # `display` (D-050 A5 ruling 10): route.mode requires it, parallel to its labels; elsewhere optional, same shape.
    for what, key, val in (("a dropped display entry", "route.mode", ["Standard", "Ping-Pong"]),
                           ("an empty display name", "route.mode", ["Standard", "", "Mid/Side"]),
                           ("a display that is not a list", "route.mode", "Standard"),
                           ("route.mode with no display", "route.mode", None),
                           ("a display on a key with no labels", "mix", ["a"])):
        planted = copy.deepcopy(full_spec)
        for x in planted:
            if x["key"] == key:
                if val is None:
                    x.pop("display", None)
                else:
                    x["display"] = val
        if not display_problems(planted):
            print(f"check_manifest: the display check did NOT see {what} (vacuous)", file=sys.stderr)
            return 1
        ndisplay += 1

    # The fixed row (T-045): the C++ table against params.json, and a keyed entry's refusals, each proven on its plant.
    spec_fixed = json.loads((root / "spec" / "params.json").read_text()).get("fixedControls", [])
    cpp_fixed = json.loads(subprocess.run([sys.argv[1], "--fixed"], check=True, capture_output=True, text=True).stdout)
    row_plants = []
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"]["sync"] = "time.ms"; row_plants.append(("the keyBy map's sync entry changed (the C++ row differs)", "diff", p))
    p = copy.deepcopy(spec_fixed); p[1]["key"] = "mix"; row_plants.append(("a plain entry's key changed (the C++ row differs)", "diff", p))
    p = copy.deepcopy(spec_fixed); p.pop(); row_plants.append(("a dropped entry (the C++ row differs)", "diff", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["key"] = "time.nope"; row_plants.append(("a keyBy on an unknown key", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["key"] = "time.ms"; row_plants.append(("a keyBy on a key that is not stepped", "rule", p))
    p = copy.deepcopy(spec_fixed); del p[0]["keyBy"]["map"]["hz"]; row_plants.append(("a label missing from the keyBy map", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"]["sync"] = "io.inGain"; row_plants.append(("a keyBy map sending a label to a Device-class key", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"]["sync"] = "time.nope"; row_plants.append(("a keyBy map sending a label to an unknown key", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"].update({"hz": "time.div", "sync": "mix"}); row_plants.append(("a keyBy map naming three keys", "rule", p))
    p = copy.deepcopy(spec_fixed); p[3] = {"k": 4, "label": "Type", "keyBy": {"key": "route.mode", "map": {"standard": "mix", "pingpong": "mix", "midside": "fb.amount"}}}
    row_plants.append(("a second keyBy entry", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["key"] = "time.ms"; row_plants.append(("an entry naming both key and keyBy", "rule", p))
    p = copy.deepcopy(spec_fixed); p[1]["key"] = "io.outGain"; row_plants.append(("a plain entry on a Device-class key", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"]["sync"] = "time.mode"; row_plants.append(("a keyBy map sending a label to its own selector key", "rule", p))
    p = copy.deepcopy(spec_fixed); p[0]["keyBy"]["map"] = ["time.ms", "time.ms", "time.div"]; row_plants.append(("a keyBy map that is not an object (a refusal, not a crash)", "rule", p))
    for what, kind, planted in row_plants:
        # "diff": the planted params.json row is compared with the real C++ row; "rule": a refusal fixed_problems must raise.
        if not (planted != cpp_fixed if kind == "diff" else fixed_problems(full_spec, planted)):
            print(f"check_manifest: the fixed-row check did NOT see {what} (vacuous)", file=sys.stderr)
            return 1

    problems = diff(spec, cpp) + switch_problems(full_spec) + display_problems(full_spec) + fixed_problems(full_spec, spec_fixed)
    if spec_fixed != cpp_fixed:
        problems.append(f"fixedControls: spec={spec_fixed!r} c++={cpp_fixed!r}")
    if spec_actions != cpp_actions:
        problems.append(f"actions: spec={spec_actions!r} c++={cpp_actions!r}")
    for line in problems:
        print(f"check_manifest: {line}", file=sys.stderr)
    if problems:
        return 1
    flips = [x for x in full_spec if x.get("morph") == "jump"]
    nobj = sum(1 for x in flips if isinstance(x.get("switch"), dict))
    print(f"check_manifest: OK — {len(cpp)} keys, {len(cpp_actions)} action(s) and {len(cpp_fixed)} fixed controls equal spec/params.json; {len(plants)} planted drifts seen; "
          f"all {len(flips)} flip keys declare an admitted switch ({nobj} as §12.7 objects, {len(flips) - nobj} in prose pending echo-005 Ask 2; A11; {nswitch} planted switch faults seen); display names parallel to labels ({ndisplay} planted faults seen); "
          f"the keyed fixed row is valid ({len(row_plants)} planted row faults seen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
