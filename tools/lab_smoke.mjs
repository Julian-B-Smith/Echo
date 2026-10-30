// lab_smoke.mjs — Echo's lab smoke, run by ./verify fast (ROADMAP Phase A gate).
//
// The lab is a listening twin, not a parity target (D-000 Q5), so this does NOT
// prove the DSP correct — that is the C++ invariant suite's job (M1). It proves
// the lab is fit to listen to: deterministic, silent when fed silence and
// exactly silent when its declared tail ends, finite, in step with the spec's
// manifest, and honest about the contract behaviour its A/B choices rest on.
// EVERY detector runs against a planted fault first and must read red on it
// (charter: "every detector has a must-fail planted control before it gates
// anything"). A plant that cannot fire is a blind detector, and its green
// elsewhere means nothing.
//
// Harness: the DSP + ANALYSIS sections are sliced at their banners and built
// with `new Function`, with Math, Date and performance SHADOWED by poisoned
// stand-ins, so a clock read or Math.random in the engine throws at run time
// (the static regex is the second, independent layer). Not `node:vm`: top-level
// consts in a vm context run ~10x slower. A missing banner throws: a silent
// mis-slice would make every check vacuous. Plants are either the engine's own
// `lab.plant` fault switches (named, inert by default) or source substitutions
// into a COPY of the slice; the lab file itself is never edited here.
import { readFileSync } from "node:fs";

const LAB = new URL("../lab/echo-lab.html", import.meta.url);
const SPEC = new URL("../spec/params.json", import.meta.url);
const html = readFileSync(LAB, "utf8");
const A = html.indexOf("/* ================= DSP"), B = html.indexOf("/* ================= UI");
if (A < 0 || B < 0 || B <= A) throw new Error("lab_smoke: DSP/UI banners not found — slice anchor lost");
const dsp = html.slice(A, B);

let fail = 0;
const check = (ok, msg) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${msg}`); if (!ok) fail = 1; };
const plant = (fired, msg) => check(fired, `plant fires: ${msg}`);
// A source substitution that silently matches nothing would make its plant a
// no-op that "passes" for the wrong reason — so every one must match exactly once.
const subst = (from, to) => { if (dsp.split(from).length !== 2) throw new Error(`lab_smoke: plant anchor not unique: ${from.slice(0, 60)}`); return dsp.replace(from, to); };

const EXPORTS = "return { EchoLab, ECHO_PARAMS, ECHO_K, echoRender, echoSource, echoQA1Spec, ECHO_QA1_MOVES, ECHO_QA1_CANDS, echoClickM2, ECHO_PROBE_HZ };";
const poisonedMath = Object.freeze(Object.assign(Object.create(null),
  Object.fromEntries(Object.getOwnPropertyNames(Math).map(k => [k, Math[k]])),
  { random() { throw new Error("Math.random called inside the lab DSP"); } }));
const clock = () => { throw new Error("wall-clock read inside the lab DSP"); };
const build = src => new Function("Math", "Date", "performance", src + "\n;" + EXPORTS)(poisonedMath, clock, { now: clock });
const X = build(dsp);

// ---- shared helpers ------------------------------------------------------------
const SR = 16000;   // the lab's invariants are rate-free (times in s, rates per s); 16 kHz keeps the gate fast
const noise = (n, seed) => { let a = seed >>> 0; const L = new Float32Array(n); for (let i = 0; i < n; i++) { a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; L[i] = (((t ^ (t >>> 14)) >>> 0) / 4294967296) * 2 - 1; } return L; };
const impulse = n => { const L = new Float32Array(n); L[0] = 1; return L; };
const burst = (n, len, seed) => { const L = noise(n, seed); L.fill(0, len); return L; };
const sameBits = (a, b) => a.length === b.length && a.every((x, i) => Object.is(x, b[i]));
const peak = a => a.reduce((m, x) => Math.max(m, Math.abs(x)), 0);
const nonzero = a => a.reduce((c, x) => c + (x !== 0), 0);
const blocks = n => Math.ceil(n / 128) * 128;
// Drive an engine block by block, with a per-block hook (param moves, sleeps, pokes).
function drive(e, srcL, srcR, nOut, bpmAt, hook) {
  const L = new Float32Array(nOut), R = new Float32Array(nOut), oL = new Float64Array(128), oR = new Float64Array(128);
  for (let o = 0; o < nOut; o += 128) {
    if (hook && hook(e, o) === "skip") continue;
    e.process(srcL.subarray(o, o + 128), (srcR || srcL).subarray(o, o + 128), oL, oR, 128, bpmAt ? bpmAt(o / e.sr) : 120);
    L.set(oL.subarray(0, Math.min(128, nOut - o)), o); R.set(oR.subarray(0, Math.min(128, nOut - o)), o);
  }
  return [L, R];
}
const engine = (Xm, params, lab, sr) => { const e = new Xm.EchoLab(sr || SR, lab); for (const [k, v] of Object.entries(params || {})) e.setParam(k, v); e.reset(); return e; };

// The stress patch: every stateful path live (drift with spread, all filters,
// past-unity feedback, a routing switch, a division flip, a tempo saw).
const STRESS = { "time.sync": 1, "time.div": 12, "time.offR": 20, "fb.amount": 110, mix: 60, "route.mode": 2,
  "filt.in.on": 1, "filt.in.hpHz": 120, "filt.out.on": 1, "filt.out.lpHz": 6000, "mod.depthMs": 6, "mod.shape": 1, "mod.spread": 70, "mod.seed": 4242 };
const STRESS_EV = [{ t: 0.6, key: "time.div", v: 9 }, { t: 1.1, key: "route.mode", v: 1 }, { t: 1.4, key: "filt.loop.on", v: 0 }];
const saw = t => 100 + 40 * ((t * 1.7) % 1);
const stress = (Xm, extra, src, lab) => Xm.echoRender({ sr: SR, seconds: 2, params: Object.assign({}, STRESS, extra || {}), events: STRESS_EV, lab,
  bpmAt: saw, srcL: src || noise(2 * SR, 1), srcR: src ? src : noise(2 * SR, 2) });

// ---- 1. Static ban (comments stripped first, so prose that NAMES a banned
// call does not trip it). AudioWorklet globals count as clocks too.
const strip = s => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
const banned = s => /Math\.random|Date\.now|new Date|performance\.now|\bcurrentTime\b|\bcurrentFrame\b|globalThis/.test(strip(s));
check(!banned(dsp), "static: DSP + ANALYSIS hold no Math.random / clock read (Date, performance, currentTime, currentFrame, globalThis)");
plant(banned(dsp + "\nconst t = currentFrame;"), "static ban detects a planted AudioWorklet currentFrame read");

// ---- 2. Dynamic ban: a planted Math.random inside the poisoned scope throws.
let threw = false;
try { build(subst("function mulberry32(seed) {", "function mulberry32(seed) { Math.random();")).echoRender(
  { sr: 8000, seconds: 0.05, params: {}, srcL: new Float32Array(400) }); } catch { threw = true; }
plant(threw, "poisoned Math throws on a planted Math.random in the engine");

// ---- 3. The lab implements exactly the spec's manifest, switch declarations included.
const FIELDS = ["key", "name", "unit", "type", "min", "max", "def", "morph", "rate", "law", "smoothing", "labels", "switch"];
function manifestDiff(spec, lab) {
  const out = [];
  if (spec.length !== lab.length) out.push(`count ${spec.length} (spec) vs ${lab.length} (lab)`);
  for (let i = 0; i < Math.max(spec.length, lab.length); i++) {
    const s = spec[i] || {}, l = lab[i] || {};
    for (const f of FIELDS) if (JSON.stringify(s[f]) !== JSON.stringify(l[f])) out.push(`${s.key || l.key}.${f}: ${JSON.stringify(s[f])} vs ${JSON.stringify(l[f])}`);
  }
  return out;
}
const specParams = JSON.parse(readFileSync(SPEC, "utf8")).params;
const diff = manifestDiff(specParams, X.ECHO_PARAMS);
check(diff.length === 0, `manifest: lab ECHO_PARAMS == spec/params.json (${specParams.length} keys, switch declarations included)` + (diff.length ? " — " + diff.slice(0, 4).join("; ") : ""));
{
  const m1 = JSON.parse(JSON.stringify(specParams)); m1[2].def += 0.5;          // never lands on an integer default
  const m2 = JSON.parse(JSON.stringify(specParams)); m2.find(p => p.switch).switch += " ";
  plant(manifestDiff(m1, X.ECHO_PARAMS).length > 0 && manifestDiff(m2, X.ECHO_PARAMS).length > 0, "manifest check detects a planted default change and a planted switch-declaration change");
}

// ---- 4. Determinism and instance isolation (admission row A15): two instances
// driven side by side, block by block, each equal to a solo render.
{
  const n = 2 * SR, sL = noise(n, 1), sR = noise(n, 2);
  const mk = Xm => { const e = engine(Xm, STRESS); return e; };
  function pair(Xm) {
    const a = mk(Xm), b = mk(Xm), solo = mk(Xm);
    const [aL] = drive(a, sL, sR, n, saw, (e, o) => { drive(b, sL.subarray(o, o + 128), sR.subarray(o, o + 128), 128, () => saw(o / SR)); });
    const [cL] = drive(solo, sL, sR, n, saw);
    return sameBits(aL, cL);
  }
  check(pair(X), "determinism: an instance driven beside a second one renders bit-identically to a solo instance");
  // Plant (A15's control): state shared between instances — one drift stream for all.
  const shared = build(subst("d.r = mulberry32(seed * 2 + 1 + k);", "d.r = (EchoLab.shared = EchoLab.shared || mulberry32(7));"));
  plant(!pair(shared), "isolation check detects a random stream shared between instances");
}

// ---- 5. reset() is bit-identical to a fresh instance (A2), from mid-ramp.
{
  const n = SR, src = noise(n, 9);
  // (A2's canonical control, "a reset that leaves a smoother unsnapped", cannot
  // fire here by design: reset() puts the instance at rest, and rest snaps every
  // smoother. The plant is the next thing reset must do: clear the lines.)
  function resetMatchesFresh(Xm) {
    const e = engine(Xm, STRESS), fresh = engine(Xm, STRESS);
    // Pass 0 ends with ramps in flight: gains, mix, feedback, depth, a filter switch.
    drive(e, src, src, n, saw, (eng, o) => { if (o === n - 256) { eng.setParam("mix", 90); eng.setParam("fb.amount", 40); eng.setParam("mod.depthMs", 15); eng.setParam("filt.loop.on", 0); eng.setParam("io.outGain", 6); } });
    e.reset();
    // The fresh instance must see the same final parameter values.
    for (const [k, v] of [["mix", 90], ["fb.amount", 40], ["mod.depthMs", 15], ["filt.loop.on", 0], ["io.outGain", 6]]) fresh.setParam(k, v);
    fresh.reset();
    return sameBits(drive(e, src, src, n, saw)[0], drive(fresh, src, src, n, saw)[0]);
  }
  check(resetMatchesFresh(X), "reset(): from mid-ramp, the output after reset is bit-identical to a fresh instance's");
  plant(!resetMatchesFresh(build(subst("    this._clearLoop();\n    this.w = 0; this.n = 0;", "    this.w = 0; this.n = 0;"))), "reset check detects a reset that leaves the old repeats in the line");
}

// ---- 6. Silence in -> exact silence out, everywhere the loop can hold energy.
let nz = 0, ctl = Infinity;
for (const route of [0, 1, 2]) for (const change of [0, 1, 2]) {
  const p = { "route.mode": route, "time.change": change, "fb.amount": 120, "mod.depthMs": 8, "mod.shape": 1, "filt.in.on": 1, "filt.out.on": 1 };
  nz += nonzero(stress(X, p, new Float32Array(2 * SR)).L);
  ctl = Math.min(ctl, peak(stress(X, p, impulse(2 * SR)).L));
}
check(nz === 0, `silence in -> exact silence out, 3 routings x 3 time-change modes at 120 % feedback (${nz} non-zero samples)`);
check(ctl > 1e-3, `control: an impulse at the same settings is audible in all 9 (min peak ${ctl.toExponential(2)})`);
{
  // Plant: a residue at rest — the engine outputs a denormal instead of exact zero.
  const resid = build(subst("outL[i] = 0; outR[i] = 0; continue; }", "outL[i] = 1e-30; outR[i] = 1e-30; continue; }"));
  plant(nonzero(stress(resid, {}, new Float32Array(2 * SR)).L) > 0, "silence detector counts a planted denormal residue at rest");
}

// ---- 7. The tail ends in EXACT zero within the declared tail (A5, RS-7.3).
{
  const p = { "time.sync": 0, "time.ms": 100, "fb.amount": 50, mix: 100, "filt.out.on": 1, "mod.depthMs": 4, "mod.shape": 1 };
  const e = engine(X, p), tailN = Math.ceil(e.tailSeconds() * SR), n = blocks(tailN + 2 * SR), src = burst(n, 800, 4);
  const [L, R] = drive(e, src, src, n);
  const after = nonzero(L.subarray(800 + tailN)) + nonzero(R.subarray(800 + tailN));
  check(after === 0 && e.stats[0].gates > 0, `tail: exact zero from the declared tail on (${e.tailSeconds().toFixed(2)} s at 50 %, 100 ms; gate fired ${e.stats[0].gates}x)`);
  check(peak(L.subarray(800, 800 + SR)) > 1e-3, "control: the tail is audible before it ends");
  const e2 = engine(X, p, { plant: "noGate" }), [L2] = drive(e2, src, src, n);
  plant(nonzero(L2.subarray(800 + tailN)) > 0, "tail check detects the gate removed (float decay outlives the declaration)");
  const held = engine(X, { "fb.amount": 100 });
  check(held.tailSeconds() === Infinity, "feedback >= 100 % declares an infinite tail (kInfiniteTail)");
}

// ---- 8. Finite under NaN/Inf, and every repair counted (RS-7.9, A9).
{
  const src = noise(2 * SR, 3); src[100] = NaN; src[200] = Infinity; src[300] = -Infinity;
  const r = stress(X, {}, src);
  check(r.L.every(Number.isFinite) && r.R.every(Number.isFinite), "finite: output stays finite with NaN/±Inf injected at the input");
  check(r.stats[0].nonfinite === 3, `the input guard counted each repair (${r.stats[0].nonfinite} of 3)`);
  plant(stress(X, {}, src, { plant: "silentRepair" }).stats[0].nonfinite === 0, "count check detects a guard that zeroes without counting");
  // NaN planted in the loop filter's state mid-render: all loop state is repaired.
  const poke = (lab, Xm) => drive(engine(Xm || X, { "fb.amount": 70, "time.sync": 0, "time.ms": 50 }, lab), noise(SR, 5), null, SR, null,
    (eng, o) => { if (o === 4096) eng.filters[1].s[0] = NaN; })[0];
  const pL = poke();
  check(pL.subarray(4096 + 2).every(Number.isFinite), "a NaN planted in the loop filter's state is repaired: the output is finite after it");
  // Both repair layers removed: the loop repair (plant switch) and the output guard (substitution).
  const bL = poke({ plant: "noLoopRepair" }, build(subst("if (!Number.isFinite(oL) || !Number.isFinite(oR)) { oL = oR = 0; this._clearLoop(); this.stats[1].nonfinite++; }", "")));
  plant(!bL.subarray(4096 + 2).every(Number.isFinite), "loop-repair check detects a repair that leaves the filter state poisoned");
}

// ---- 9. The read never reverses and never leaves the line (D-006; spec §3.6, §7).
{
  let worst = "", bad = 0, runs = 0;
  // Every run is fed a constant non-zero input: silence would put the engine AT
  // REST, where moves snap and nothing is measured — a vacuous pass. `gates === 0`
  // proves each run stayed awake.
  const live = n => new Float32Array(n).fill(1e-3);
  const ok = st => st.reverse === 0 && st.sMin >= X.ECHO_K.sMin - 1e-9 && st.sMax <= X.ECHO_K.sMax + 1e-9 && st.overrun === 0 && st.clamps === 0 && st.gates === 0;
  const note = (id, st) => `${id}: s in [${st.sMin.toFixed(3)}, ${st.sMax.toFixed(3)}], ${st.reverse} reverse, ${st.overrun} overrun, ${st.clamps} hard-bound hits`;
  for (let m = 0; m < X.ECHO_QA1_MOVES.length; m++) for (const [id, , lab, params] of X.ECHO_QA1_CANDS) for (const fallback of [true, false]) {
    if (id === "onepoleD") continue;                                  // the rejected law is the plant below
    if (m === 2 && !fallback && id !== "varispeed") continue;
    const spec = X.echoQA1Spec(m, "probe", 8000, { fb: 0, mix: 100, glide: 0.3, fallback, lab, params });
    spec.srcL = live(spec.srcL.length);                               // the speed statistic does not depend on the audio
    const st = X.echoRender(spec).stats[0]; runs++;
    if (!ok(st)) { bad++; worst = note(`${id} move ${m}`, st); }
  }
  // Modulation at its extremes in every time-change mode, at the 12 s line's end:
  // depth 20 ms of drift, a rate glide 10 -> 0.05 Hz, a spread ramp, a shape
  // switch and a seed change — each of which once moved the read past the bound.
  const modRun = (change, lab) => { const sp = { sr: 8000, seconds: 6, lab, srcL: live(blocks(6 * 8000)),
      params: { "time.sync": 0, "time.ms": 12000, "time.change": change, "mod.depthMs": 20, "mod.rateHz": 10, "mod.shape": 0, "fb.amount": 0 },
      events: [{ t: 0.5, key: "mod.rateHz", v: 0.05 }, { t: 1.5, key: "mod.spread", v: 100 }, { t: 2.5, key: "mod.shape", v: 1 }, { t: 3.5, key: "mod.seed", v: 99 },
               { t: 4, key: "mod.rateHz", v: 10 }, { t: 4.6, key: "time.ms", v: 11990 }] };
    const r = X.echoRender(sp); return [r.stats[0], r.stats[1]]; };
  for (const change of [0, 1, 2]) for (const st of modRun(change)) { runs++; if (!ok(st)) { bad++; worst = note(`modulation, time.change ${change}`, st); } }
  // A short time with a deep modulation: the depth is held to D - 3 samples, and
  // when a crossfade lands on a long time that limit lifts at once — the slew
  // limit is what keeps the offset from stepping.
  const shortRun = (change, lab) => X.echoRender({ sr: 8000, seconds: 2, lab, srcL: live(16000),
    params: { "time.sync": 0, "time.ms": 10, "time.change": change, "mod.depthMs": 20, "mod.rateHz": 1, "fb.amount": 0 },
    events: [{ t: 0.3, key: "time.ms", v: 500 }] }).stats;
  for (const change of [0, 1, 2]) for (const st of shortRun(change)) { runs++; if (!ok(st)) { bad++; worst = note(`short time, time.change ${change}`, st); } }
  // Drift at full depth for 30 s at 12 s: the Catmull-Rom curve overshoots [-1, 1].
  const driftRun = lab => X.echoRender({ sr: 8000, seconds: 30, lab, srcL: live(240000),
    params: { "time.sync": 0, "time.ms": 12000, "mod.depthMs": 20, "mod.rateHz": 3, "mod.shape": 1, "mod.spread": 100, "fb.amount": 0 } }).stats;
  for (const st of driftRun()) { runs++; if (!ok(st)) { bad++; worst = note("drift at 12 s", st); } }
  check(bad === 0, `read speed in [${X.ECHO_K.sMin}, ${X.ECHO_K.sMax}], no reverse read, no read past the line, no hard-bound hit — ${runs} law/move/modulation runs` + (bad ? " — " + worst : ""));
  plant(shortRun(1, { plant: "modUnlimited" }).some(st => !ok(st)), "speed check detects the modulation slew limit removed");
  plant(driftRun({ plant: "driftOvershoot" }).some(st => st.overrun > 0), "line-bound check detects the drift curve's 1.25x overshoot");
  const sp = X.echoQA1Spec(0, "probe", 8000, { fb: 0, mix: 100, glide: 0.05, fallback: false, lab: { law: "onepoleD", plant: "noClamp" }, params: { "time.ms": 250 } });
  sp.srcL = live(sp.srcL.length);
  sp.events = [{ t: 1, key: "time.ms", v: 3000 }];
  plant(X.echoRender(sp).stats[0].reverse > 0, "reverse-read counter fires on D-006's rejected fixed-tau one-pole, unclamped");
}

// ---- 10. Time accuracy and the generation ratio (filters off, integer delay).
// Each repeat lands on its sample. The DC blocker (D-005) is the one element
// left in the loop, so generation 2 is g * (1+R)/2 of generation 1 (the
// blocker's impulse gain); later generations also carry the blocker's small
// negative tail, O((1-R) R^D): "ratio = g" holds to 1e-3, not bit-exactly
// (spec §5.3; S-1 ruled this tolerance, D-017).
function generations(params, sr, lab) {
  const r = X.echoRender({ sr, seconds: 0.9, lab, params: Object.assign({ "time.sync": 0, "time.ms": 100, "fb.amount": 50, mix: 100, "filt.loop.on": 0 }, params), srcL: impulse(blocks(0.9 * sr)) });
  const D = Math.round(0.1 * sr), out = [];
  for (let k = 1; k <= 6; k++) out.push([r.L[k * D], peak(r.L.subarray(k * D - 3, k * D + 4))]);
  return out;
}
{
  const sr = 48000, Rdc = Math.exp(-2 * Math.PI * X.ECHO_K.dcHz / sr), g = 0.5, want2 = g * 0.5 * (1 + Rdc);
  const ok = gs => gs.every(([at, pk]) => at === pk) && gs[0][0] === 1 && Math.abs(gs[1][0] - want2) < 1e-6
    && gs.slice(1).every(([at], k) => Math.abs(at / gs[k][0] / g - 1) < 1e-3);
  check(ok(generations({}, sr)), `time accuracy: repeats land on k x 4800 samples; gen 2 = g(1+R)/2 = ${want2.toFixed(6)} (1e-6), every ratio = g within 1e-3`);
  plant(!ok(generations({ "filt.loop.on": 1 }, sr)), "ratio check detects the loop filter left in circuit");
  plant(!ok(generations({ "time.ms": 100.01 }, sr)), "time check detects a fractional-sample misplacement");
  // The first block lands on the HOST tempo, never gliding from 120 (spec §3.3).
  const at60 = lab => { const r = X.echoRender({ sr: 8000, seconds: 1.3, lab, bpmAt: () => 60, params: { "time.sync": 1, "time.div": 9, mix: 100, "fb.amount": 0 }, srcL: impulse(blocks(1.3 * 8000)) });
    return r.L.findIndex(x => x !== 0); };
  check(at60() === 8000, `synced 1/4 at 60 bpm lands at exactly 1 s from the first block (sample ${at60()})`);
  plant(at60({ plant: "tempoFrozen" }) !== 8000, "tempo check detects the host tempo ignored");
}

// ---- 11. Off means off (ADR-142), the dry path is clean, the mix ends are exact.
{
  const base = { "filt.in.on": 0, "filt.out.on": 0, "filt.loop.on": 0 };
  const a = stress(X, Object.assign({}, base, { "filt.in.hpHz": 20, "filt.out.lpHz": 20000 })), b = stress(X, Object.assign({}, base, { "filt.in.hpHz": 1800, "filt.out.lpHz": 600 }));
  check(sameBits(a.L, b.L), "true bypass: with every filter off, extreme cutoffs leave the output bit-identical");
  plant(!sameBits(a.L, stress(X, Object.assign({}, base, { "filt.out.on": 1, "filt.out.hpHz": 20, "filt.out.lpHz": 20000 })).L), "bypass check detects an 'open' filter left in circuit");
  // mix 0: the output IS the input (a full-scale 1.5 input at 120 % feedback) — no dry limiter.
  const src = Float32Array.from(noise(SR, 8), x => 1.5 * x);
  const dry = lab => X.echoRender({ sr: SR, seconds: 1, lab, params: { mix: 0, "fb.amount": 120 }, srcL: src }).L;
  check(sameBits(dry(), src), "dry path: at mix 0 the output is the input exactly (no limiter on the dry, ADR-142)");
  plant(!sameBits(dry({ plant: "dryLimiter" }), src), "dry-path check detects a limiter on the dry path");
  const wet = lab => X.echoRender({ sr: SR, seconds: 0.2, lab, params: { mix: 100, "time.sync": 0, "time.ms": 100 }, srcL: src }).L;
  check(nonzero(wet().subarray(0, 1500)) === 0, "mix 100 %: nothing before the first repeat — no dry leak");
  plant(nonzero(wet({ plant: "dryLeak" }).subarray(0, 1500)) > 0, "mix check detects a cos(pi/2) dry leak");
}

// ---- 12. I/O gain (B435): exact gain, Input before the loop, a linear amplitude ramp.
{
  const src = new Float32Array(SR).fill(0.25), g6 = Math.pow(10, 6 / 20), rampN = Math.round(X.ECHO_K.rampS * SR);
  const run = Xm => { const e = engine(Xm, { mix: 0, "fb.amount": 0 });
    return drive(e, src, src, SR, null, (eng, o) => { if (o === 4096) eng.setParam("io.outGain", 6); })[0]; };
  const ok = L => L[100] === Math.fround(0.25) && Math.abs(L[4096 + rampN + 10] - Math.fround(0.25 * g6)) < 1e-7
    && Math.abs(L[4096 + (rampN >> 1)] - 0.25 * (1 + (g6 - 1) * ((rampN >> 1) + 1) / rampN)) < 1e-6;
  check(ok(run(X)), "io.outGain: unity at 0 dB, +6 dB exact after a 20 ms ramp that is linear in amplitude");
  const dbRamp = build(subst("case 1: return Math.pow(10, p[EI[\"io.outGain\"]] / 20);", "case 1: return 1 + p[EI[\"io.outGain\"]] / 20;"));
  plant(!ok(run(dbRamp)), "gain check detects a wrong gain law");
  // Input Gain sits before the loop: +12 dB in drives the ceiling (B435 test 5).
  const loop = gIn => X.echoRender({ sr: SR, seconds: 1, params: { mix: 100, "fb.amount": 100, "time.sync": 0, "time.ms": 50, "filt.loop.on": 0, "io.inGain": gIn }, srcL: burst(SR, 400, 6) }).L;
  check(peak(loop(12).subarray(SR / 2)) < 4 * peak(loop(0).subarray(SR / 2)), "io.inGain is before the loop: +12 dB in is held by the ceiling, not passed through 4x");
}

// ---- 13. Switches click-free (A11): routing and filter switches crossfade.
{
  const probe = () => X.echoSource("probe", SR, blocks(2 * SR));
  const M2 = (key, v, extra, lab) => { const r = X.echoRender({ sr: SR, seconds: 2, lab, srcL: probe(), srcR: new Float32Array(blocks(2 * SR)),
      params: Object.assign({ mix: 100, "time.sync": 0, "time.ms": 120, "fb.amount": 0, "filt.loop.on": 0 }, extra), events: [{ t: 1, key, v }] });
    return X.echoClickM2(key === "route.mode" ? r.R : r.L, SR - 200, SR + 2000, 0.5, X.ECHO_PROBE_HZ, SR, 1); };
  const route = M2("route.mode", 2, {}), routeI = M2("route.mode", 2, {}, { plant: "instantSwitch" });
  const filt = M2("filt.out.on", 1, { "filt.out.lpHz": 500 }), filtI = M2("filt.out.on", 1, { "filt.out.lpHz": 500 }, { plant: "instantSwitch" });
  check(route < 1.5 && filt < 1.5, `switches: routing (M2 ${route.toFixed(2)}) and filter (M2 ${filt.toFixed(2)}) switch without a click`);
  plant(routeI > 2 * route && filtI > 2 * filt, `switch check detects instant switches (M2 ${routeI.toFixed(1)}, ${filtI.toFixed(1)})`);
  // The crossfade law is equal-GAIN (RS-3.6, D-007): two heads on a constant never exceed it.
  const xf = lab => peak(X.echoRender({ sr: SR, seconds: 1, lab, srcL: new Float32Array(SR).fill(0.5), events: [{ t: 0.5, key: "time.ms", v: 60 }],
      params: { mix: 100, "time.sync": 0, "time.ms": 100, "time.change": 1, "fb.amount": 0, "filt.loop.on": 0 } }).L.subarray(SR / 4));
  check(xf() <= 0.5 + 1e-9, `crossfade: equal-gain, two correlated heads never exceed the signal (peak ${xf().toFixed(4)})`);
  plant(xf({ plant: "equalPower" }) > 0.6, "crossfade check detects an equal-power law (1.41x on correlated heads)");
}

// ---- 14. Deferred flips and the seeded seed switch (A11 as amended, §12.2).
{
  const glide = (Xm, flip) => { const e = engine(Xm, { "time.sync": 0, "time.ms": 500, mix: 100, "fb.amount": 0 });
    return drive(e, X.echoSource("probe", SR, 3 * SR), null, 3 * SR, null, (eng, o) => {
      if (o === blocks(0.5 * SR)) eng.setParam("time.ms", 250);
      if (o === blocks(0.6 * SR) && flip) eng.setParam("time.change", 2); })[0]; };
  const ref = glide(X, false);
  check(sameBits(glide(X, true), ref), "time.change flipped mid-glide changes nothing until the next move (deferred)");
  const eager = build(subst("    // time.change, time.law, time.tempoMode: deferred — read when the next move starts.",
    "    if (key === \"time.change\") for (const h of this.heads) this._latch(h);"));
  plant(!sameBits(glide(eager, true), ref), "deferral check detects a time.change that acts at the flip");
  const seedJump = lab => { const e = engine(X, { "mod.depthMs": 5, "mod.shape": 1 }, lab); drive(e, noise(4096, 1), null, 4096);
    const before = e._driftVal(e.drift[0]); e.setParam("mod.seed", 77); return Math.abs(e._driftVal(e.drift[0]) - before); };
  check(seedJump() === 0, "mod.seed switch is seeded: the drift value does not step");
  plant(seedJump({ plant: "unseededSeed" }) > 1e-3, "seed check detects an unseeded reseed");
}

// ---- 15. A load ends the old repeats; Device keys are not part of a patch (D-015, RS-6.7, RS-4.5).
{
  const ring = lab => { const e = engine(X, { "fb.amount": 85, "time.sync": 0, "time.ms": 200, mix: 100 }, lab);
    const [L] = drive(e, burst(2 * SR, 600, 2), null, 2 * SR, null, (eng, o) => { if (o === LD) eng.load({ "time.ms": 300, "fb.amount": 40, "io.inGain": 12 }); });
    return [L, e]; };
  const LD = blocks(SR / 2), [L, e] = ring(), rampN = Math.round(X.ECHO_K.rampS * SR);
  check(nonzero(L.subarray(LD + rampN + 1)) === 0 && peak(L.subarray(LD - 2000, LD)) > 1e-3, "load: the ringing repeats fade out over 20 ms and end (horde's spill-over owns the old tail)");
  check(e.get("io.inGain") === 0, "load: Device keys (io gains) are left alone");
  plant(nonzero(ring({ plant: "loadKeeps" })[0].subarray(LD + rampN + 1)) > 0, "load check detects a load that keeps the line");
  plant(ring({ plant: "loadDevice" })[1].get("io.inGain") === 12, "Device-key check detects a load that writes io.inGain");
}

// ---- 16. Mid/side folds to its M line (Q-A4, by construction).
{
  const fold = r => Float64Array.from(r.L, (x, i) => 0.5 * (x + r.R[i]));
  const n = blocks(1.5 * SR), L = noise(n, 5), R = noise(n, 6), M = Float32Array.from(L, (x, i) => 0.5 * (x + R[i]));
  const run = (Xm, l, r) => Xm.echoRender({ sr: SR, seconds: 1.5, params: { "route.mode": 2, "time.sync": 0, "time.ms": 90, "time.offR": 37, "fb.amount": 70, mix: 100, "mod.depthMs": 3 }, srcL: l, srcR: r });
  const maxDiff = (a, b) => a.reduce((m, x, i) => Math.max(m, Math.abs(x - b[i])), 0);
  const e = maxDiff(fold(run(X, L, R)), fold(run(X, M, M)));
  check(e < 1e-6, `mid/side: fold(out(L,R)) == fold(out(M,M)) — the fold IS the M line (max diff ${e.toExponential(1)})`);
  const leak = build(subst("function echoFb(r, j, y0, y1) { return", "function echoFb(r, j, y0, y1) { if (r === 2 && j === 0) return y0 + 0.2 * y1; return"));
  plant(maxDiff(fold(run(leak, L, R)), fold(run(leak, M, M))) > 1e-3, "fold check detects a planted S -> M feedback leak");
}

// ---- 17. Past unity the loop parks (ADR-142; D-005 as amended). Once the input
// stops, the returning term is at most the ceiling x the DC blocker's L1 gain
// (1 + R < 2); while it plays, the wet is at most the input's peak + 2 (spec §5.1).
{
  const park = (src, lab) => X.echoRender({ sr: SR, seconds: 4, lab, params: { "time.sync": 0, "time.ms": 150, "fb.amount": 120, mix: 100, "filt.loop.on": 0 }, srcL: src });
  const n = 4 * SR, pulse = Float32Array.from({ length: n }, (_, i) => (i < 0.1 * SR ? 0.9 : 0));
  const sq = Float32Array.from({ length: n }, (_, i) => (Math.floor(i / (0.3 * SR)) % 2 ? -0.9 : 0.9));
  const a = park(impulse(n)), b = park(pulse), c = park(sq);
  const stopped = Math.max(peak(a.L.subarray(SR)), peak(b.L.subarray(SR))), playing = peak(c.L);
  check([a, b, c].every(r => r.L.every(Number.isFinite)) && stopped < 2 && playing <= 0.9 + 2 && peak(a.L.subarray(3 * SR)) > 0.5,
    `past unity (120 %) the loop parks: ${stopped.toFixed(3)} < 2 after the input stops, ${playing.toFixed(3)} <= 0.9 + 2 under a square, and it does not die`);
  plant(peak(park(pulse, { plant: "noCeiling" }).L) > 2, "ceiling check detects the ceiling removed");
}

// ---- 18. The click reading (D-009 M2) separates the ruled jump from a bare one (Q-A5).
{
  const m2 = jump => { const s = X.echoQA1Spec(0, "probe", 48000, { fb: 0, mix: 100, glide: 0.3, fallback: true, lab: { jump }, params: { "time.change": 2 } });
    s.seconds = 3; const r = X.echoRender(s); return X.echoClickM2(r.L, Math.round(1.5 * 48000), r.L.length, 0.5, X.ECHO_PROBE_HZ, 48000, 2); };
  const clean = m2("xfade"), raw = m2("raw");
  check(clean < 1.5, `M2: jump as a 3 ms crossfade reads clean (${clean.toFixed(2)})`);
  plant(raw > 5, `M2 flags a bare jump (${raw.toFixed(1)})`);
}

// ---- 19. Sleep and wake (RS-7.6, A16 as amended): an instance horde stopped
// calling at rest matches its never-slept twin from the wake on, with the
// parameters changed during sleep at their targets.
{
  const n = blocks(5 * SR), src = new Float32Array(n);
  src.set(noise(0.4 * SR, 3), 0); src.set(noise(0.6 * SR, 4), blocks(3.2 * SR));
  const p = { "time.sync": 1, "time.div": 15, "fb.amount": 20, mix: 60, "mod.depthMs": 8, "mod.shape": 0, "mod.rateHz": 1.3 };
  const tempo = t => (t < 2 ? 120 : 96);
  function pair(Xm) {
    const twin = engine(Xm, p), sleeper = engine(Xm, p);
    const changes = eng => { eng.setParam("fb.amount", 65); eng.setParam("mix", 80); eng.setParam("time.div", 9); };
    const [tL] = drive(twin, src, null, n, tempo, (e, o) => { if (o === blocks(2.5 * SR)) changes(e); });
    let slept = 0;
    const [sL] = drive(sleeper, src, null, n, tempo, (e, o) => {
      if (o >= blocks(2.2 * SR) && o < blocks(3.2 * SR)) { if (!e.idle) throw new Error("lab_smoke: not at rest before sleeping"); if (o === blocks(2.5 * SR)) changes(e); slept++; return "skip"; }
    });
    return slept > 0 && sameBits(tL.subarray(blocks(3.2 * SR)), sL.subarray(blocks(3.2 * SR)));
  }
  check(pair(X), "sleep/wake: from the wake on, a slept instance equals its never-slept twin (parameters changed while asleep land at their targets)");
  const runs = build(subst("if (xL === 0 && xR === 0) { this._atRest(bpmEnd); outL[i] = 0; outR[i] = 0; continue; }",
    "if (xL === 0 && xR === 0) { this._atRest(bpmEnd); this._modStep(1e9); outL[i] = 0; outR[i] = 0; continue; }"));
  plant(!pair(runs), "sleep check detects a modulation that keeps running at rest");
}

// ---- 20. time.scale (T-038, D-048): the synced snap and what counts as a move. The lab mirrors the C++
// (engine.h timeScale / applied): the octave snap is exact at the doubles nearest the boundaries, and a scale
// send is a control move only if it moves a resolved time.
{
  const nextDown = x => { const f = new Float64Array([x]); new BigUint64Array(f.buffer)[0] -= 1n; return f[0]; };
  // 1/4 at 120 bpm is 0.5 s; a synced scale s resolves to 0.5 x its octave.
  const syncedTime = (Xm, s) => { const e = new Xm.EchoLab(SR, {}); e.setParam("time.sync", 1); e.setParam("time.div", 9); e.setParam("time.scale", s); e._resolveBoth(120); return e.Tt[0] / SR; };
  function snapBad(Xm) {
    const bad = [];
    for (let k = -2; k <= 2; k++) { const t = syncedTime(Xm, Math.pow(2, k)); if (t !== 0.5 * Math.pow(2, k)) bad.push(`x2^${k} -> ${t}`); }
    for (let k = -2; k <= 1; k++) {
      const edge = Math.pow(2, k) * Math.SQRT2;   // the double nearest 2^(k + 1/2): just above it, so up
      for (const [what, s, want] of [["one ulp under", nextDown(edge), Math.pow(2, k)], ["the double nearest", edge, Math.pow(2, k + 1)]]) {
        const t = syncedTime(Xm, s); if (t !== 0.5 * want) bad.push(`${what} the 2^${k + 0.5} boundary (${s}) -> ${t} s, expected ${0.5 * want}`);
      }
    }
    return bad;
  }
  const bad = snapBad(X);
  check(bad.length === 0, "time.scale: synced it snaps to the nearest octave: every octave, the double nearest each boundary (up) and one ulp under it (down)" + (bad.length ? " — " + bad.slice(0, 3).join("; ") : ""));
  const rounds = build(subst("return Math.pow(2, s / Math.pow(2, e) >= Math.SQRT2 ? e + 1 : e);", "const l = Math.log2(s); return Math.pow(2, Math.sign(l) * Math.round(Math.abs(l)));"));
  plant(snapBad(rounds).length > 0, "time.scale snap check detects round(log2 s) with halves away from zero (C's lround), wrong at the doubles nearest a boundary");
  const free = (() => { const e = new X.EchoLab(SR, {}); e.setParam("time.sync", 0); e.setParam("time.ms", 100); e.setParam("time.scale", 3.1); e._resolveBoth(120); return e.Tt[0] / SR; })();
  check(Math.abs(free - 0.31) < 1e-12, `time.scale: free it is the value itself (100 ms x 3.1 = ${(free * 1000).toFixed(6)} ms)`);

  // What counts as a move: flags first, then a render.
  function moveFlags(Xm) {
    const e = new Xm.EchoLab(SR, {});
    e.setParam("time.sync", 1); e.setParam("time.div", 9); e._resolveBoth(120);
    const sent = v => {
      e.paramMoved = false; e.setParam("time.scale", v);
      const moved = e.paramMoved;
      if (e.timeDirty) { e._resolveBoth(120); e.timeDirty = false; }   // the next block resolves, and consumes the move
      return moved;
    };
    return [sent(1), sent(1.2), sent(0.8), sent(1.5), sent(1.6), sent(0.5)];   // resend, in band, in band, x2, still x2, x1/2
  }
  const flags = moveFlags(X).join();
  check(flags === "false,false,false,true,false,true", `time.scale: a re-sent or in-band synced scale moves nothing; an octave step moves (${flags})`);
  const always = build(subst("if (t[0] !== this.Tt[0] || t[1] !== this.Tt[1]) { this.timeDirty = true; this.paramMoved = true; }", "this.timeDirty = true; this.paramMoved = true;"));
  plant(moveFlags(always).join() !== flags, "time.scale move check detects every scale send counted as a move");
}

console.log(fail ? "lab_smoke: RED" : "lab_smoke: OK");
process.exit(fail);
