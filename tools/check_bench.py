#!/usr/bin/env python3
"""check_bench.py — ./verify's smoke check of the cost bench (ROADMAP T-020; D-045).

  check_bench.py <echo_bench>

Runs `echo_bench --smoke` (one repeat of a tiny window, no load guard) and checks the FORM of what it
prints, never the timing: CPU is Layer-E, measured and reported, never a gate (RS-8.3). So nothing below
compares a percentage with a budget or with a previous run. What it catches is a bench that rots unseen:
a crash, a row that went missing, a field that stopped printing, a value that is not a finite number, a
row's own arithmetic disagreeing with itself, or a moving row whose engine-side evidence says it never moved
(critic T-020 M5: a row whose move silently stopped would otherwise print a well-formed static figure).

The detector first sees its plants (each a copy of the real output with one fault), then the real output.
"""
import json
import math
import subprocess
import sys

ROWS = ["default", "worst-repitch", "worst-crossfade", "worst-morph", "auto-time", "auto-fb", "morph", "worst"]
VARIANTS = ("worst-repitch", "worst-crossfade", "worst-morph")
INPUTS = ["noise", "chord"]
LEDGER_NUM = ["sr", "block", "timed_s", "best_s", "pct_m3", "cal_ms", "ratio", "minspec_pct_assumed_x1.5", "budget_pct",
              "load_lo", "load_hi", "guard_waits", "moves", "fades", "fallbacks", "speed_min", "speed_max"]
RATES = [44100, 48000, 96000, 192000]


def problems(text):
    """Every way the bench's output is malformed; empty when it is well formed."""
    out, seen, mem, ledger = [], set(), [], []
    for n, line in enumerate(text.splitlines(), 1):
        kind, _, body = line.partition(" ")
        if kind not in ("LEDGER", "MEMORY"):
            out.append(f"line {n}: not a LEDGER or MEMORY row: {line[:60]!r}")
            continue
        try:
            row = json.loads(body)
        except json.JSONDecodeError as e:
            out.append(f"line {n}: not JSON ({e})")
            continue
        if kind == "MEMORY":
            if not all(isinstance(row.get(k), (int, float)) and math.isfinite(row[k]) and row[k] > 0 for k in ("sr", "mib", "lines_mib")):
                out.append(f"line {n}: a MEMORY row without finite positive sr / mib / lines_mib")
            elif row["lines_mib"] > row["mib"]:
                out.append(f"line {n}: the lines ({row['lines_mib']} MiB) exceed the whole ({row['mib']} MiB)")
            else:
                mem.append(row)
            continue
        for k in LEDGER_NUM:
            v = row.get(k)
            if not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) or v < 0:
                out.append(f"line {n}: {row.get('row')}/{row.get('input')}: {k} is {v!r}, not a finite number >= 0")
        if not (isinstance(row.get("best_s"), (int, float)) and row["best_s"] > 0):
            out.append(f"line {n}: best_s is not positive: nothing was timed")
        if row.get("protocol") != "B441-3" or row.get("module") != "echo" or not isinstance(row.get("over_budget"), bool) \
                or not isinstance(row.get("forced"), bool):
            out.append(f"line {n}: protocol / module / over_budget / forced malformed")
        try:  # the row's own arithmetic, at its printed precision
            if abs(row["minspec_pct_assumed_x1.5"] - 1.5 * row["pct_m3"]) > 2e-4:
                out.append(f"line {n}: {row['row']}/{row['input']}: min-spec is not pct_m3 x 1.5")
            want = row["pct_m3"] / 100 / (row["cal_ms"] / 1000)
            if abs(row["ratio"] - want) > 2e-3 * want + 1e-5:
                out.append(f"line {n}: {row['row']}/{row['input']}: ratio {row['ratio']} is not pct / calibration ({want:.5f})")
            if row["over_budget"] != (row["minspec_pct_assumed_x1.5"] > row["budget_pct"]):
                out.append(f"line {n}: {row['row']}/{row['input']}: over_budget disagrees with the figures")
            moving = row["row"] != "default"
            if moving and row["moves"] <= 0:
                out.append(f"line {n}: {row['row']}/{row['input']}: no evidence of movement (moves 0): the row went static")
            if not moving and row["moves"] != 0:
                out.append(f"line {n}: default/{row['input']}: something moved in the static row")
            if row["row"] == "worst-crossfade" and row["fades"] <= 0:
                out.append(f"line {n}: worst-crossfade/{row['input']}: no fade in flight")
        except (KeyError, TypeError):
            pass  # a missing or non-numeric field is reported above
        ledger.append(row)
        if not isinstance(row.get("foreign_top3"), list):
            out.append(f"line {n}: foreign_top3 is not a list")
        if row.get("row") == "worst" and row.get("from") not in VARIANTS:
            out.append(f"line {n}: the worst row names no variant (from = {row.get('from')!r})")
        seen.add((row.get("row"), row.get("input")))
    for r in ROWS:
        for i in INPUTS:
            if (r, i) not in seen:
                out.append(f"no LEDGER row {r} / {i}")
    for i in INPUTS:  # the worst row is the largest of the three variants, and says which
        var = [r for r in ledger if r.get("input") == i and r.get("row") in VARIANTS and isinstance(r.get("pct_m3"), (int, float))]
        w = [r for r in ledger if r.get("input") == i and r.get("row") == "worst"]
        if var and w:
            top = max(var, key=lambda r: r["pct_m3"])
            if w[0].get("pct_m3") != top["pct_m3"] or w[0].get("from") != top["row"]:
                out.append(f"worst/{i}: reads {w[0].get('pct_m3')} from {w[0].get('from')}, the largest variant is {top['row']} at {top['pct_m3']}")
    if sorted(m["sr"] for m in mem) != RATES:
        out.append(f"MEMORY rows for {sorted(m['sr'] for m in mem)}, not {RATES}")
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    run = subprocess.run([argv[1], "--smoke"], capture_output=True, text=True)
    if run.returncode != 0:
        print(f"check_bench: echo_bench --smoke exited {run.returncode}: {run.stderr.strip()[:300]}", file=sys.stderr)
        return 1
    text = run.stdout
    lines = text.splitlines()
    first = json.loads(lines[0].partition(" ")[2])
    # Each plant must be seen FOR ITS OWN REASON: its message must appear (critic T-020 L6).
    plants = [
        ("a missing row", "\n".join(lines[1:]), "no LEDGER row default / noise"),
        ("a non-JSON row", "\n".join([lines[0][:-1]] + lines[1:]), "not JSON"),
        ("a NaN figure", text.replace('"pct_m3":', '"pct_m3":NaN,"was":', 1), "pct_m3 is nan"),
        ("a dropped field", text.replace(',"ratio":', ',"ratioX":', 1), "ratio is None"),
        ("a stray line", text + "bench: done\n", "not a LEDGER or MEMORY row"),
        ("a worst row with no variant", text.replace('"from":"worst-', '"from":"x-', 1), "names no variant"),
        ("a missing MEMORY rate", "\n".join(l for l in lines if '"sr":192000,"max_block"' not in l), "MEMORY rows for"),
        ("a static moving row", "\n".join(l.replace('"moves":', '"moves":0,"was":', 1) if '"row":"auto-fb"' in l else l for l in lines),
         "auto-fb/noise: no evidence of movement"),
        ("a crossfade with no fade", "\n".join(l.replace('"fades":', '"fades":0,"was":', 1) if '"row":"worst-crossfade"' in l else l for l in lines),
         "no fade in flight"),
        ("a wrong min-spec", text.replace('"minspec_pct_assumed_x1.5":', '"minspec_pct_assumed_x1.5":99,"was":', 1), "min-spec is not pct_m3 x 1.5"),
        ("a wrong ratio", text.replace('"ratio":', '"ratio":9,"was":', 1), "is not pct / calibration"),
        ("a worst that is not the largest", "\n".join(l.replace('"pct_m3":', '"pct_m3":0.0001,"was":', 1) if '"row":"worst"' in l else l for l in lines),
         "the largest variant is"),
    ]
    assert first["row"] == "default"  # the plants above assume the bench's row order
    for what, bad, why in plants:
        got = problems(bad)
        if not any(why in g for g in got):
            print(f"check_bench: the detector did NOT see {what} for its own reason ({why!r}; got {got[:2]})", file=sys.stderr)
            return 1
    found = problems(text)
    for p in found:
        print(f"check_bench: {p}", file=sys.stderr)
    if found:
        return 1
    print(f"check_bench: OK — echo_bench --smoke printed {sum(l.startswith('LEDGER') for l in lines)} well-formed LEDGER rows and "
          f"{sum(l.startswith('MEMORY') for l in lines)} MEMORY rows; {len(plants)} planted faults seen (form only: CPU is never gated, RS-8.3)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
