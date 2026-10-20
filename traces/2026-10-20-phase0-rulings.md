# phase0-rulings — D-004–D-011 ruled by poll; Phase 0 closed

- **Queue item:** T-001 (Phase 0 gate: DECISIONS records what prior art changes).
- **Why:** PR #1 delivered `docs/prior-art.md` and eight PROPOSED decisions; the
  phase could not close until the human ruled on them.
- **Evidence consulted:** DECISIONS D-004–D-011; `docs/prior-art.md` §2 and the
  Phase A open list; `intake/legacy/ADR-142-standard-delay.md:32`; D-000 Q4.
  Re-derived before polling: float32 position ulp 0.25 at 2.3 M samples; linear
  interp |cos(ω/2)| → −16.7 dB (44.1 kHz) / −0.47 dB (192 kHz) at 20 kHz;
  12·log2(1 + 11.99/0.1) ≈ 83 st; 2·0.707 = 1.41; s = 1 − dD/dt with
  D = k/BPM ⇒ 1 + D·BPM′/BPM.
- **Rulings:** seven ratified as written; D-005 ratified AS AMENDED (filter path
  bounded, g > 1 allowed and parked by the ceiling) after the lead found it
  contradicted ADR-142 and the oracle's past-unity row; Q-A4 left open for the
  Phase A spec. Recorded in D-013; ROADMAP status, T-001 and the Phase A gate
  updated; LIBRARY L0004 (candidate) records the near-miss.
- **Alternatives rejected:** D-005 "always < 1"; D-009 "M2 only for 1.0".
- **Verify:** `./verify fast` exit 0 before commit (see commit hash).
- **Open questions:** Q-A1, Q-A2, Q-A3, Q-A4; T-002 filings.
