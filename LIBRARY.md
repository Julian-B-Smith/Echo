# LIBRARY — Echo durable lessons

Entry format: `[Lxxxx] title | tier | added | tags | lesson | evidence | falsifier | supersedes`.
New lessons enter as `candidate`. Never edit INDEX without LIBRARY, or vice versa.

<a id="L0001"></a>
[L0001] State the delay-buffer memory figure at the worst host rate, computed from the formula | candidate | added: 2026-10-19 | tags: performance-budget, sample-rate-independence | lesson: Delay memory = max seconds × SR × channels × bytes, and horde's rack runs at the HOST rate (ADR-237), so the 1.0 bar's "worst case included" memory MUST row means 192 kHz, not 48 kHz — for 12 s stereo float32 that is 17.58 MiB, 4× the 48 kHz figure. Compute it, never estimate it. | evidence: at survey the lead's poll text said "~4.2 MiB" for 12 s at 48 kHz; the formula gives 4,608,000 B = 4.39 MiB (DECISIONS D-000 Q3b); intake/module-1.0-bar.md row 54 + ADR-237 in intake/rack-slot-contract.md §12.3. | falsifier: horde caps the host rate below 192 kHz for hosted modules, or Echo runs its lines at a fixed internal rate. | supersedes: —
