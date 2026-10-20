# INDEX — Echo knowledge map

One line per LIBRARY lesson. Read this in full at ORIENT; pull only matching
entries from LIBRARY.md. Tags: time-accuracy · feedback-stability ·
modulation-repitch · stereo-mono-safety · sample-rate-independence ·
performance-budget.

- [L0001](LIBRARY.md#L0001) — state the delay-buffer memory figure at 192 kHz, from the formula (performance-budget, sample-rate-independence) · candidate
- [L0002](LIBRARY.md#L0002) — kernel L1 norm bounds sample peaks, not loop gain; check |H| for stability (feedback-stability, time-accuracy) · candidate
- [L0003](LIBRARY.md#L0003) — linear interpolation darkens repeats at an SR-dependent rate (sample-rate-independence, time-accuracy) · candidate
- [L0004](LIBRARY.md#L0004) — bound the loop filter path (|H_loop| ≤ 1), not the knob, when past-unity feedback is a feature (feedback-stability) · candidate
- [L0006](LIBRARY.md#L0006) — the textbook DC blocker peaks at 2/(1+R) > 1 at Nyquist; normalise it inside a bounded loop (feedback-stability) · candidate
- [L0007](LIBRARY.md#L0007) — a click probe commensurate with the test delays is blind to jumps (time-accuracy, modulation-repitch) · candidate
- [L0008](LIBRARY.md#L0008) — a modulation ramp's length is a read speed; and M2 reads speed kinks between glides and jumps (modulation-repitch) · candidate
- [L0009](LIBRARY.md#L0009) — once a module can rest, silence-fed checks can pass vacuously; prove each run stayed awake (time-accuracy, modulation-repitch) · candidate
