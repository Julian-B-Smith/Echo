# b4-2-service-view — the render service and the display model (ROADMAP T-031, T-032; M4 batch B4-2)

- **Queue items:** T-031 (the render service), T-032 (the display model); also T-031 criterion 6, a resumable form of
  T-030's renderer.
- **Why:** M4's gates 3 and 4 (off the audio thread; a redraw within 100 ms) need a worker, a cache and head-first
  frames. Gate 1 needs the drawing to be the render's own samples. D-011's main view needs a stem echogram on a tempo
  grid, the per-generation spectrum strip, and the EDC.
- **Built:**
  - **`core/include/echo/ir_render.h`:** `start()` / `renderNext()`. Blocks are aligned to absolute multiples of 512,
    so any split is bit-identical to the one-shot render, rest point included. `IrInfo.times` gives the resolved line
    times. The fp-contract pragma is scoped here too.
  - **`core/include/echo/ir_service.h` (new):**
    - one worker thread, with preallocated slots keyed by `irHash`;
    - frame 1 is 96 000 samples (D-033), then the same render is extended in 0.1 s chunks; every chunk is
      cancellable;
    - a request goes on screen only at its first publication;
    - cut renders are reused first and are never cache hits;
    - `pause()` / `resume()` for a closed editor; `stop()` frees everything;
    - the API is UI-thread only and takes a lock.
  - **`core/include/echo/ir_view.h` (new, pure):**
    - `irColumns`: each column is the render's own peak sample; past a partial frame it reads "not rendered";
    - `irDb`, `irBeats` and `irDivisionMarks`;
    - `irGenerationWindows`: per-routing arrivals from SPEC §4;
    - `irGenerationSpectra` and `irEdc`.
  - **`core/tests/ir_tests.cpp`:** 13 new checks (20 in all) and 14 new M4 plants (23 M4 plants in all). CMake links
    Threads for the two IR binaries.
- **Evidence consulted:**
  - ROADMAP T-031, T-032, M4's gate;
  - D-011, D-027, D-030 – D-032;
  - SPEC §2 (the wet carries the loop filter once), §4 (routing), §10;
  - `engine.h` (`routingFor`, `resolveBoth`, `resolvedTimes`);
  - the rack-slot contract §8 (published atomics; no GUI reads of audio memory) and B432 (0 % with the editor closed),
    from horde's packet.
- **Independent review:** an Opus critic subagent (fresh context, read-only) returned FIX-THEN-SHIP. ThreadSanitizer
  could not start on this Mac (an empty program segfaults under it), so the concurrency verdict is by reading. All
  findings are fixed, ruled or recorded:
  - **H1, the strip's spacing was wrong for mid/side and ping-pong:** now per-routing arrivals; undefined cases are
    refused; every routing is tested.
  - **H2, the strip's oracle was too weak:** it is now the first repeat's own shape, with a control. Plant
    `viewGenIndexOffByOne`.
  - **M1, the head and D-032's evidence:** the evidence was wrong. Two polls followed (D-033); frame 1 is 96 000
    samples.
  - **M2, cut renders evicted complete ones:** fixed. Plant `irCancelKeepsSlot`, checked under a drag.
  - **M3, an empty frame went on screen per request:** fixed. Plant `irPublishesEmpty`.
  - **M4, unrendered columns read as zero:** fixed. Plant `viewUnrenderedIsZero`.
  - **M5, no in-flight test:** added. The head is now cancellable too.
  - **M6, the pause check would flake:** it now polls.
  - **M7, no bars:** criterion amended; bar lines need the host's meter (T-034).
  - **LOW:**
    - memory units corrected and freed at `stop()`;
    - `request()` after `stop()` refused;
    - the libm caveat stated in the headers;
    - resume restarts a cut render (commented).
  - **L3, not fixed here:** `core/tests/alloc_probe.h`'s comments ("single-threaded", "no mutex in
    core/include/echo/") are now false for `ir_service.h`. The file is in M3's B2 scope, so it is left to M3 and
    noted in that session's hand-over.
- **Measurements (dev Mac, Release, worst of 20, slow patch):**
  - frame 1 at 48 kHz: 18–46 ms cold, 23–42 ms in flight;
  - frame 1 at 192 kHz: 19–48 ms cold, 22–49 ms in flight;
  - the top of each range was measured inside `./verify fast`;
  - the 2 s head that D-033 rejected: 90–196 / 81–122 ms at 192 kHz.
- **Plants seen firing (`echo_ir_plants`):**
  - irDropsDevice -> I/O gains +6 / -12 dB: differs from the plain render at sample 0 (L 0.089100652418836801 vs 0.044656109500181937)
  - irLevelIgnored -> defaults (synced, loop filter on): differs from the plain render at sample 0 (L 0.8910065241883679 vs 0.089100652418836801)
  - irSkipsOutputFilter -> output filter: differs from the plain render at sample 9600 (L 0.01811317835368818 vs 0.0012843336763763067)
  - irSeedAfterReset -> drift modulation: differs from the plain render at sample 7460 (L -0.0011808737805289226 vs 0)
  - irReusesState -> 'free 250 ms, fb 0, filters off' after another patch differs from a fresh render at sample 0
  - irSeedAfterReset -> 'drift modulation' after another patch differs from a fresh render at sample 7460
  - irAcceptsClampedRate -> a 4000 Hz request was rendered (the engine runs at 8000 Hz)
  - irHashSkipsKey -> changing mod.seed leaves the hash unchanged: a stale drawing would be served
  - noTailGate -> fb 0 %: 5.65e-53 after the declared tail (16808 samples)
  - irRestAtMissesEnd -> a render of 6144 samples, resting in its last block, reports restAt -1
  - irLevelIgnored -> at -20 dBFS the first repeat peaks at 1, want 0.1
  - irAllocsInRender -> 2 allocations in a prepared render
  - irChunkResetsNoise -> input 4, split rule 0: the resumed render differs at sample 8641
  - irChunkResetsNoise -> input 4: a published frame differs from the one-shot render at sample 13440
  - irRenderInline -> a render ran on a thread other than the worker (the UI's?)
  - irWholeTailFirst -> 48 kHz: frame 1 covered 384000 samples, not the head (96000)
  - irPublishesEmpty -> an empty frame was on screen during the drag
  - irCacheIgnored -> A again was re-rendered (hits 0 -> 0, renders 2 -> 3)
  - irCancelKeepsSlot -> A's complete render was pushed out of the cache by the drag's cut renders (11 cancelled)
  - irNoCancel -> A ran to its end before B started (no cancellation)
  - irRendersWhilePaused -> rendered 33 chunks while paused
  - viewDecimatesPeaks -> 100 columns: column 6 shows 0.00511415, its peak is 0.00871414
  - viewUnrenderedIsZero -> a column past the rendered 30000 samples reads 34583, not 'not rendered'
  - gridFixedTempo -> 93 bpm: beat 1 at 24000.0000
  - viewGenWindowFromBase -> route 0, ch 1, filters off: gen 1 -> 2 steps 1.147 dB at 100 Hz, want -3.098
  - viewGenIndexOffByOne -> route 0, ch 0, loop filter: gen 1 -> 2 steps -10.129 dB at 100 Hz; the first repeat says -16.726
  - edcForward -> the EDC starts at -400 dB, not 0
- **Alternatives rejected:**
  - a time-budgeted head (how much frame 1 shows would depend on the machine);
  - macOS QoS on the worker (no clear effect);
  - an analytic |F| in the test (a second model of the engine).
- **Verify:** `./verify fast`, exit 0; the hash is in `.harness/last-verify.json` and in the PR.
- **Open questions:**
  - the service's memory at high rates (D-032: a byte budget instead of a slot count), to be ruled with the screen
    (T-034);
  - bar lines (the host's meter), T-034;
  - the wall-clock half of gate 4 is gated on the dev Mac only.
