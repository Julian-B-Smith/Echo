# t040-exp-reversal — T-040: the exponential law's 0.1 ms speed-step bound, the reversal and onset gate, the lab's twin

- **Queue item:** T-040 (D-049, D-049 A1, A2); phase 1 (measure), phase 2 (land), phase 3 (critic fixes), phase 4 (the re-check, D-049 A3). Supersedes the phase-1 entry
  `2026-10-30-t040-phase1-measure.md` (same session, untracked, renamed into this one).
- **Why:** under the exponential law `h.V` is a memoryless function of (D, T), so a retarget steps the read speed in
  one sample: 1.0 at an onset, 1.5 at a reversal (cap to cap). A speed step reads 3.49 x step x |cos(probe phase)| on
  the click metric, so an onset reads up to 3.5 and a reversal up to 5.1, and the old gate passed the onsets only
  because its fixed event times landed on a kind phase (2.299 at the suite's phase, 3.509 at the worst). D-049 A1 ruled
  a 0.1 ms bound on the speed (`1/(k::kExpSlewS * fs)` per sample, after the cap), a gate that takes the worst of 8
  probe phases, and the same bound in the lab.
- **Evidence consulted:** CLAUDE.md §Domain; D-049, D-049 A1, D-023; ROADMAP T-040 and T-041; SPEC §3.5-3.6, §15;
  `core/include/echo/engine.h` (stepHead, backstop); `core/tests/core_tests.cpp` (clickCases, clickSweep, expLaw,
  repitchCents, the admission rows); `lab/echo-lab.html` (stepHead, logpole), `tools/lab_smoke.mjs` section 18.
  Phase-1 measurements ran in scratch copies of `core/` (outside the repo, Release): 192 reversal cases x 16 probe phases
  on the current law and nine prototype fixes.
- **What changed (phase 2):**
  - `core/include/echo/engine.h`: `k::kExpSlewS = 1e-4`; in the exponential branch, after the cap,
    `h.V = clamp(h.V, vPrev - a, vPrev + a)` with `a = 1 / (kExpSlewS * sr_)`; skipped by plant `expSlewOff` (bound 10 ms under `expSlewLong`); counts its binds
    (`SpeedStats::slewBinds`, `lastSlewBind`). The tape law and the cap are untouched.
  - `core/include/echo/plants.h`: `expSlewOff`, `expSlewLong`, `phaseSweepCollapsed`.
  - `core/tests/core_tests.cpp`: checks `reversalClick`, `onsetClick`, `expSlewBinds` (registered, in admission row A11; the admission "moving-time
    clicks" threshold row names it); `clickRun` takes a probe-phase shift and a sample rate; `expLaw` carries a comment that its
    500 -> 450 ms move stays under the bound (0.176 < 0.208 per sample), so its recurrence is exact (tolerance untouched);
    `--click-report` also prints the new tables (and, in `echo_plants`, the same under `expSlewOff`).
  - `spec/SPEC.md` §3.6 (status-header amendment line, the exponential row's law and character cells, a paragraph in the
    Bounds block, the stale "4.1" in the §15 finding).
  - `lab/echo-lab.html`: `ECHO_K.expSlewS`, the same bound after the cap in the `logpole` law, plant `expSlewOff`;
    `tools/lab_smoke.mjs` section 18b (the reversal case and its planted control).
  - `docs/admission.md` regenerated (`echo_tests --admission-out`; 2 lines move); `README.md` counts 100 -> 101 checks,
    137 -> 138 planted faults.
- **The gates (after the critic, D-049 A2):** three checks in `core/tests/core_tests.cpp`, all in admission row A11.
  - `reversalClick`: the FULL matrix, both laws x `time.ms` / `time.div` / `time.scale` free / `time.scale` synced x
    1 and 2 octaves x down and up x reversed at 100 / 200 / 400 ms x 48 and 44.1 kHz = 192 cases, each at 8 probe
    phases spread over HALF a probe period (|cos| has a half-cycle period), the worst gated; count guard
    (`kReversalCases`).
  - `onsetClick`: every existing `time.law = 1` click case (7, selected by key, count-guarded) plus two plain onsets
    (500 -> 250, 125 -> 500), each at both rates and 8 phases = 18 cases; its own check so `expSlewOff` fails it for its
    own reason (the reversal check returns at its first failure and never reached the onsets).
  - `expSlewBinds`: the engine counts the bound's binds and the sample of the last one (`SpeedStats::slewBinds`,
    `lastSlewBind`); across 44.1 / 48 / 96 / 192 kHz x glides 0.01 / 0.3 / 1.0 x with and without 20 ms / 10 Hz
    modulation x six scenarios (onsets down and up, reversed 20 ms in, reversed twice at 100 ms, 2 and 1 ms targets,
    a `time.glide` change mid-move) = 144 runs, the last bind of every segment between events must be within
    1.5 x 0.1 ms of the segment's first sample. Measured latest bind 0.1354 ms (192 kHz).
  - Must read <= 2.5 (`kClickMax`, untouched), zero backstop hits, zero floor/overrun reads, s in [0.25, 2.25].
  - The tape law at 96 / 192 kHz is not run (T-041).
- **Critic findings and fixes (FIX-THEN-SHIP):**
  - **M1** `phaseShift` spread 8 phases over a whole period (4 distinct readings): now `j * period / (2 * 8)`.
  - **M2 + ruling (D-049 A2)** the runtime trim was never authorised: the full matrix is back, the "Trimmed for runtime
    (D-049 A1...)" comment deleted, `clickRun` takes a sample rate so the exponential onsets run at 44.1 kHz as well.
  - **H1** the phase sweep had no control: plant `phaseSweepCollapsed` (`phaseShift` returns 0) and a detector,
    `phaseSweepWorks`, run first in `reversalClick` and `onsetClick`: the exponential 250 -> 500 ms move (a bounded
    speed step of 0.5) reads 0.197 .. 0.741 over the 8 phases (spread 0.543, same at 44.1 kHz); required > 0.3.
  - **L1** under `expSlewOff` the first failing reversal ended the check: the onsets are their own check
    (`onsetClick`) with count guards; both fail for their own reasons under the plant.
  - **L3** "binds only where the rate would step" now has a gate (`expSlewBinds`) and a plant (`expSlewLong`, a 10 ms
    bound). The limit is **1.5 x 0.1 ms = 0.15 ms, not the 0.1 ms the brief gave**: a cap-to-cap turnover is 1.5 units
    and binds for 1.5 x the unit time (SPEC §3.6 says 0.15 ms), and a 0.1 ms limit flagged a legitimate reversal at
    48 kHz, glide 0.01, reversed 20 ms in (bind 5 samples = 0.104 ms after the event). The plant's late bind is
    371 ms after the event, so the margin is not what the limit protects. `expLaw`'s comment now says its exactness is
    48 kHz specific (at 96 kHz the same move binds, 0.071 samples off the bare recurrence).
  - **L4** `blockIndependence` gained an exponential-law reversal at sample 69001 that binds the bound (one bind per
    head, confirmed with the new counter); `rampPerBlock` and `blockSteppedD` still fire on it.
  - **L2** SPEC §3.6: the column header labels its sources (tape on the lab, exponential on the C++ core); "M2 <= 1.01
    on every case" is replaced by the gated figures (reversals <= 1.009, plain onsets <= 0.964 at 44.1 / 48 kHz, worst
    of 8 phases) and the ungated 96 / 192 kHz figures (1.029 / 1.044, the critic's census, quoted not re-measured);
    "a `time.glide` change" joins the events; the law cell refers to the bound, and Bounds keeps it as "not part of the law".
  - **L6** the two redundant plain onsets (500 -> 125, 500 -> 250 at glide 0.01; both read 0.935 like 500 -> 250) are
    dropped; the "~1000 cases" comment is the real count (1536 renders).
  - **L7/9** the lab smoke case is unchanged (critic confirmed it).
- **Phase 4: the critic's re-check (D-049 A3; `SpeedStats::slewBinds` / `lastSlewBind` approved; the tape cap kink and the
  D-006 fallback's frozen-head step belong to T-041, which now blocks T-023):**
  1. **SPEC §3.6:** "Not gated: 96 and 192 kHz read ..." is narrowed to reversals; through the fallback crossfade the
     exponential law reads about 3.5 and 7.0 there, "T-041's item".
  2. **L1 (the sweep guard covered one path):** `phaseSweepWorks` is now its own check, called once, with three
     detectors: the 8 shifts are distinct and increasing and the last is 7/16 of a probe period (within one sample); the
     `clickRun` shift path (250 -> 500 ms exponential, spread 0.543 > 0.3); the `reversalRun` shift path (tape 2 oct
     down, reversed at 400 ms, 48 kHz: 0.789 .. 2.440, spread 1.650 > 1.0). `phaseSweepCollapsed` fails it on the
     structure (its own reason).
  3. **L2:** the `blockIndependence` binding event is now a cap-to-cap turnover: `time.ms` 1500 at 68600 (speed to the
     0.5 cap), `time.ms` 20 at 69300 (turnover, 6 binds per head, ~7 samples at 48 kHz; confirmed with the counter).
     Blocks are cut at events, so the event starts a block and the bind runs on inside it: the 1- and 7-sample patterns
     cut inside the span. New plant `expSlewBlockStart` takes `vPrev` from the block's first sample; `blockIndependence`
     catches it, and `rampPerBlock` and `blockSteppedD` still fire.
  4. **L3:** every `expSlewBinds` guard has a plant: "a bind before any move" - `expSpuriousRetarget` (the target drops
     to a quarter for samples 4800 - 4900, a move nobody asked for); `binding > 0` - `expSlewOff` (no bound, no bind).
  5. **L4:** `clickRun` aborts if a case with a tempo function is asked to run at a rate other than 48 kHz (its tempo
     function is indexed in 48 kHz samples).
  6. **Speed-ups:** (a) `parallelFor`, a fixed pool of at most `hardware_concurrency()` (at least 1; `--threads N` lowers
     it) over (case x phase) work items; each item owns its engine and buffers and writes only its own slot of a result
     vector; every REQUIRE and every report line then runs on the main thread in the fixed case order. `--click-report
     --threads 1` and `--threads 8` outputs are byte-identical (cmp: identical, 241 REPORT lines). `reversalClick`,
     `onsetClick` and `expSlewBinds` (as `renderBinds` jobs) use it; `core/CMakeLists.txt` links `echo_tests` and
     `echo_plants` with `Threads::Threads` (the one `find_package(Threads)` moved up). `core/tests/alloc_probe.h`'s
     `t_inNew` is now a relaxed `std::atomic<bool>`: workers allocate through the replaced `operator new` and a plain
     global written from several threads is a data race (nothing is armed then, so the counts are unchanged; the checks
     that count allocations stay single-threaded). Shared state the workers touch: the const case vectors, the plant
     flags and `g_report` / `kClickMax` (read only), the atomic work counter. A ThreadSanitizer build could not start in
     this sandbox (segfault at launch, with and without the alloc probe), so the no-race claim rests on that audit;
     CI's ASan + UBSan run covers memory, not races. (b) `reversalClick` runs the exponential law first. (c)
     `phaseSweepWorks` runs once.
  - **Runtime, per binary (idle machine, `echo_tests --click-report`, which is the click gates and nothing else):**
    14.7 s before; 10.4 s serial (`--threads 1`) and 2.9 s wall on 8 threads after. Whole binaries: `echo_tests` 54.0 s ->
    37.9 s wall, `echo_plants` 26.5 s -> 21.5 s wall. `./verify fast` wall 1:41 (before) against 2:08 and 2:20 (after,
    warm and cold) - not comparable: the machine's load average was 11 during the later runs (the same `--click-report`
    read 7.0 s on 8 threads and 18.6 s serial under that load), so the verify wall time is noise-dominated here.
  - **New and changed plant lines (verbatim):**
    - `PASS  plant fires: phaseSweepCollapsed -> the probe-phase sweep is not distinct: shift 0 is 0 samples, shift 1 is 0 at 48000 Hz (every phase is the same phase)`
    - `PASS  plant fires: expSlewBlockStart -> block pattern 0 differs at sample 68601`
    - `PASS  plant fires: expSpuriousRetarget -> onset down 500 -> 125 ms at 44100 Hz, glide 0.01, unmodulated: a bind (sample 4904) before any move`
    - `PASS  plant fires: expSlewOff -> no run bound at all: the census sees nothing (vacuous)`
    - unchanged: `expSlewOff` in `reversalClick` (3.488) and in `onsetClick` (3.494), `expSlewLong` (371.633 ms), `rampPerBlock` (sample 29759), `blockSteppedD` (sample 12001).
  - **Counts:** 104 checks (`phaseSweepWorks` is the new one), 142 planted faults; README and `docs/admission.md` regenerated.
- **Plants and their failure lines (verbatim):**
  - `expSlewOff`, in `reversalClick` (core, `echo_plants`):
    `PASS  plant fires: expSlewOff -> exponential law, time.ms, 1 oct down, reversed at 100 ms, 48000 Hz reads M2 3.488 at its worst of 8 probe phases (threshold 2.500)`
  - `expSlewOff`, in `onsetClick` (its own reason):
    `PASS  plant fires: expSlewOff -> exponential onset move 300 -> 200 ms, exponential, glide 0.05, 48000 Hz reads M2 3.494 at its worst of 8 probe phases (threshold 2.500)`
  - `phaseSweepCollapsed` (phase 3 line, superseded in phase 4 by the structural one above): `... the 250 -> 500 ms exponential move reads 0.197 .. 0.197 over 8 phases at 48000 Hz (spread 0.000, needs > 0.30): every phase is the same phase`
  - `expSlewLong` (`expSlewBinds`):
    `PASS  plant fires: expSlewLong -> onset down 500 -> 125 ms at 44100 Hz, glide 0.01, unmodulated: the bound still binds 16389 samples = 371.633 ms after the last event (limit 0.15 ms): it binds in a relaxation`
  - lab (`tools/lab_smoke.mjs`), unchanged: `PASS  plant fires: M2 flags the exponential law without its speed-step bound (4.84)`
  - `blockIndependence` after the new event: `PASS  plant fires: rampPerBlock -> block pattern 0 differs at sample 29759`, `PASS  plant fires: blockSteppedD -> block pattern 0 differs at sample 12001`.
- **Before / after (worst of 8 half-period phases, `--click-report`; before = `expSlewOff`, i.e. the unfixed law; 210 cases, 0 backstop hits both ways; over 2.5: 88 before, 0 after):**

  | family | before | after |
  |---|---|---|
  | exponential reversals, 48 kHz, 1 oct down / up | 3.488 / 3.324 | 0.927 / 0.843 |
  | exponential reversals, 48 kHz, 2 oct down / up | 4.829 / 5.108 | 0.998 / 1.009 |
  | exponential reversals, 44.1 kHz, 1 oct down / up | 3.214 / 3.054 | 0.962 / 0.862 |
  | exponential reversals, 44.1 kHz, 2 oct down / up | 4.433 / 4.700 | 0.993 / 0.955 |
  | tape reversals, 2 oct down, 48 kHz / 44.1 kHz (other cells <= 0.565) | 2.440 / 2.309 | 2.440 / 2.309 (unchanged: the tape's plain 500 -> 125 ms onset, T-041) |
  | plain onset 500 -> 250, 48 / 44.1 kHz | 3.509 / 3.231 | 0.934 / 0.964 |
  | plain onset 125 -> 500, 48 / 44.1 kHz | 1.718 / 1.583 | 0.744 / 0.744 |
  | move 300 -> 200 glide 0.05, 48 / 44.1 kHz | 3.494 / 3.215 | 0.931 / 0.954 |
  | octave steps, synced 1/4, 48 / 44.1 kHz | 4.111 / 3.786 | 0.955 / 0.982 |
  | scale sweep, synced, 48 / 44.1 kHz | 5.153 / 4.765 | 1.747 / 1.604 |
  | scale sweep, free (1.730 / 1.596), full sweep (0.789), 200-400 sweep (0.523) | unchanged by the bound | |

  The old single-phase `clickSweep` is unchanged and green. Glide arrival at glide 0.3 (500 -> 250, within 1 %): 1.2750 s -> 1.2750 s (+0.1 ms).
- **Runtime:** the new checks cost ~14.3 s per suite binary clean (`echo_tests --click-report` 14.7 s against 0.43 s with
  none of them), run in `echo_tests` and `echo_plants` (their plants stop at the first failure), ~29 s added to
  `./verify fast` over the pre-T-040 tree, ~21 s over the phase-2 cut that the ruling reversed; `./verify fast` wall
  1:41 warm. Over the ~10 s guide; the full matrix is the human's ruling (D-049 A2).
- **Alternatives rejected (phase 1, measured):** a slew of 0.25 ms or more (breaks `expLaw`'s 1e-6 recurrence;
  2 ms and up breaks the 1-cent `repitchCents` gate; every size from 0.5 ms reaches the 0.789 curvature floor, so M2 cannot
  choose among them); a second one-pole on V (moves the glide 19 - 83 ms earlier, breaks `expLaw` by tens of samples).
- **Verify:** `./verify fast` exit 0, git 440a044 + the working tree (`.harness/last-verify.json` target fast, exit 0);
  `echo_tests: OK (0 failing)` (104 checks), `echo_plants: OK (0 failing)` (142 plants), `admission: docs/admission.md equals this run's (rows A1 - A23); planted change seen`.
- **Open questions:** the tape law's 2 oct down move now gates at 2.440 against 2.5 (margin 0.06, worst of 8 phases at
  48 kHz; T-041 owns it); the two scale sweeps still read 1.73 and 1.68 on the fixed law at some phase (below the bar;
  independent of the bound: a 1 ms bound reads the same).
