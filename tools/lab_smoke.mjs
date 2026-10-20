// lab_smoke.mjs — Echo's lab smoke, run by ./verify fast (ROADMAP Phase A gate).
//
// The lab is a listening twin, not a parity target (D-000 Q5), so this does NOT
// prove the DSP correct — that is the C++ invariant suite's job (M1). It proves
// the lab is fit to listen to: deterministic, silent when fed silence, finite,
// in agreement with the spec's manifest, and honest about the invariants the
// human's A/B choices rest on (the read never reverses; time lands where it
// says; mid/side folds to its M line). Every detector runs against a planted
// fault first and must read red on it (charter: "every detector has a
// must-fail planted control before it gates anything").
//
// Harness: the DSP + ANALYSIS sections are sliced at their banners and built
// with `new Function`, with Math, Date and performance SHADOWED by poisoned
// stand-ins, so a clock read or Math.random in the engine throws at run time
// (the static regex below is the second, independent layer). Not `node:vm`:
// top-level consts in a vm context run ~10x slower, which made this gate slow.
// A missing banner throws: a silent mis-slice would make every check vacuous.
// Plants are built by source substitution into a COPY; the lab is never edited.
import { readFileSync } from "node:fs";

const LAB = new URL("../lab/echo-lab.html", import.meta.url);
const SPEC = new URL("../spec/params.json", import.meta.url);
const html = readFileSync(LAB, "utf8");
const A = html.indexOf("/* ================= DSP"), B = html.indexOf("/* ================= UI");
if (A < 0 || B < 0 || B <= A) throw new Error("lab_smoke: DSP/UI banners not found — slice anchor lost");
const dsp = html.slice(A, B);

let fail = 0;
const check = (ok, msg) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${msg}`); if (!ok) fail = 1; };
// A planted control: the detector must FIRE on the plant. If it does not, the
// detector is blind and its green elsewhere means nothing.
const plant = (fired, msg) => check(fired, `plant fires: ${msg}`);

const EXPORTS = "return { EchoLab, ECHO_PARAMS, ECHO_K, echoRender, echoSource, echoQA1Spec, ECHO_QA1_MOVES, ECHO_QA1_CANDS, echoClickM2, ECHO_PROBE_HZ };";
const poisonedMath = Object.freeze(Object.assign(Object.create(null),
  Object.fromEntries(Object.getOwnPropertyNames(Math).map(k => [k, Math[k]])),
  { random() { throw new Error("Math.random called inside the lab DSP"); } }));
const clock = () => { throw new Error("wall-clock read inside the lab DSP"); };
const build = src => new Function("Math", "Date", "performance", src + "\n;" + EXPORTS)(poisonedMath, clock, { now: clock });
const X = build(dsp);

// ---- 1. Static ban (comments stripped first, so prose that NAMES a banned
// call does not trip it).
const strip = s => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
const banned = s => /Math\.random|Date\.now|new Date|performance\.now/.test(strip(s));
check(!banned(dsp), "static: DSP + ANALYSIS hold no Math.random / wall-clock read");
plant(banned(dsp + "\nconst t = performance.now();"), "static ban detects a planted performance.now()");

// ---- 2. Dynamic ban: a planted Math.random inside the poisoned scope throws.
let threw = false;
try { build(dsp.replace("function mulberry32(seed) {", "function mulberry32(seed) { Math.random();")).echoRender(
  { sr: 8000, seconds: 0.05, params: {}, srcL: new Float32Array(400) }); } catch { threw = true; }
plant(threw, "poisoned Math throws on a planted Math.random in the engine");

// ---- 3. The lab implements exactly the spec's manifest.
const FIELDS = ["key", "name", "unit", "type", "min", "max", "def", "morph", "rate", "law", "smoothing", "labels"];
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
check(diff.length === 0, `manifest: lab ECHO_PARAMS == spec/params.json (${specParams.length} keys)` + (diff.length ? " — " + diff.slice(0, 4).join("; ") : ""));
const mutated = JSON.parse(JSON.stringify(specParams)); mutated[2].def += 0.5;   // never lands on an integer default
plant(manifestDiff(mutated, X.ECHO_PARAMS).length > 0, "manifest check detects a planted default change");

// ---- shared renders ------------------------------------------------------------
const SR = 16000;   // the lab's invariants are rate-free (times in s, rates per s); 16 kHz keeps the gate fast
const noise = (n, seed) => { let a = seed >>> 0; const L = new Float32Array(n); for (let i = 0; i < n; i++) { a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; L[i] = (((t ^ (t >>> 14)) >>> 0) / 4294967296) * 2 - 1; } return L; };
const impulse = n => { const L = new Float32Array(n); L[0] = 1; return L; };
const sameBits = (a, b) => a.length === b.length && a.every((x, i) => Object.is(x, b[i]));
const peak = a => a.reduce((m, x) => Math.max(m, Math.abs(x)), 0);
const nonzero = a => a.reduce((c, x) => c + (x !== 0), 0);

// The stress patch: every stateful path live (drift modulation with spread, all
// filters, past-unity feedback, a routing switch, a repitch move, tempo saw).
const STRESS = { "time.sync": 1, "time.div": 12, "time.offR": 20, "fb.amount": 110, mix: 60, "route.mode": 2,
  "filt.in.on": 1, "filt.in.hpHz": 120, "filt.out.on": 1, "filt.out.lpHz": 6000, "mod.depthMs": 6, "mod.shape": 1, "mod.spread": 70, "mod.seed": 4242 };
const STRESS_EV = [{ t: 0.6, key: "time.div", v: 9 }, { t: 1.1, key: "route.mode", v: 1 }, { t: 1.4, key: "filt.loop.on", v: 0 }];
const saw = t => 100 + 40 * ((t * 1.7) % 1);
const stress = (extra, src) => X.echoRender({ sr: SR, seconds: 2, params: Object.assign({}, STRESS, extra || {}), events: STRESS_EV,
  bpmAt: saw, srcL: src || noise(2 * SR, 1), srcR: noise(2 * SR, 2) });

// ---- 4. Determinism and reset identity (admission rows A15, A2).
const d1 = stress(), d2 = stress();
check(sameBits(d1.L, d2.L) && sameBits(d1.R, d2.R), "determinism: two fresh instances render the stress patch bit-identically");
const d3 = stress({ "mod.seed": 4243 });
plant(!sameBits(d1.L, d3.L), "determinism check detects a different seed");
{
  // reset() must equal a fresh instance: render, reset, render again.
  const e = new X.EchoLab(SR); for (const [k, v] of Object.entries(STRESS)) e.setParam(k, v); e.reset();
  const n = SR, src = noise(n, 9), oL = new Float64Array(128), oR = new Float64Array(128), out = [new Float32Array(n), new Float32Array(n)];
  for (let pass = 0; pass < 2; pass++) {
    if (pass) e.reset();
    for (let o = 0; o < n; o += 128) { e.process(src.subarray(o, o + 128), src.subarray(o, o + 128), oL, oR, 128, saw(o / SR)); for (let i = 0; i < 128; i++) out[pass][o + i] = oL[i]; }
  }
  check(sameBits(out[0], out[1]), "reset(): output after reset is bit-identical to a fresh instance's");
}

// ---- 5. Silence in -> exact silence out, everywhere the loop can hold energy.
let nz = 0, ctl = Infinity;
for (const route of [0, 1, 2]) for (const change of [0, 1, 2]) {
  const p = { "route.mode": route, "time.change": change, "fb.amount": 120, "mod.depthMs": 8, "mod.shape": 1, "filt.in.on": 1, "filt.out.on": 1 };
  const sR = X.echoRender({ sr: SR, seconds: 2, params: Object.assign({}, STRESS, p), events: STRESS_EV, bpmAt: saw, srcL: new Float32Array(2 * SR), srcR: new Float32Array(2 * SR) });
  nz += nonzero(sR.L) + nonzero(sR.R);
  ctl = Math.min(ctl, peak(stress(p, impulse(2 * SR)).L));
}
check(nz === 0, `silence in -> exact silence out, 3 routings x 3 time-change modes at 120 % feedback (${nz} non-zero samples)`);
check(ctl > 1e-3, `control: an impulse at the same settings is audible in all 9 (min peak ${ctl.toExponential(2)})`);
const denorm = Float32Array.from(d1.L, (x, i) => (i === 7 ? x + 1e-38 : 0));
plant(nonzero(denorm) > 0, "silence detector counts a planted single denormal sample");

// ---- 6. Finite under NaN / Inf injection, and the guard counts every repair (RS-7.9).
{
  const src = noise(2 * SR, 3); src[100] = NaN; src[200] = Infinity; src[300] = -Infinity;
  const r = stress({}, src);
  check(r.L.every(Number.isFinite) && r.R.every(Number.isFinite), "finite: output stays finite with NaN/±Inf injected at the input");
  check(r.stats[0].nonfinite === 3, `the guard counted each repair (${r.stats[0].nonfinite} of 3)`);
  const bad = Float32Array.from(r.L); bad[5] = NaN;
  plant(!bad.every(Number.isFinite), "finite check detects a planted NaN");
}

// ---- 7. The read never reverses (D-006), for every Q-A1 candidate on every move.
{
  let worst = "", bad = 0, runs = 0;
  for (let m = 0; m < X.ECHO_QA1_MOVES.length; m++) for (const [id, , lab, params] of X.ECHO_QA1_CANDS) for (const fallback of [true, false]) {
    if (m === 2 && !fallback && id !== "crit2") continue;          // the 12 s move without fallback: one law is enough
    const spec = X.echoQA1Spec(m, "probe", 8000, { fb: 0, mix: 100, glide: 0.3, capSt: 12, fallback, lab, params });
    spec.srcL = new Float32Array(spec.srcL.length);                 // the speed statistic does not depend on the audio
    const st = X.echoRender(spec).stats[0]; runs++;
    if (st.reverse > 0 || st.sMin < X.ECHO_K.sMin - 1e-9 || st.sMax > X.ECHO_K.sMax + 1e-9) { bad++; worst = `${id} move ${m}: s in [${st.sMin}, ${st.sMax}], ${st.reverse} reverse`; }
  }
  check(bad === 0, `read speed in [${X.ECHO_K.sMin}, ${X.ECHO_K.sMax}], zero reverse reads, ${runs} candidate x move runs` + (bad ? " — " + worst : ""));
  // Plant: D-006's rejected fixed-tau one-pole, with the hard clamp OFF, on a
  // lengthening move faster than 1 s/s — it must read backward.
  const sp = X.echoQA1Spec(0, "probe", 8000, { fb: 0, mix: 100, glide: 0.05, capSt: 12, fallback: false,
    lab: { law: "onepoleD", clampSpeed: false }, params: { "time.ms": 250 } });
  sp.events = [{ t: 1, key: "time.ms", v: 3000 }];
  plant(X.echoRender(sp).stats[0].reverse > 0, "reverse-read counter fires on the rejected fixed-tau one-pole, unclamped");
}

// ---- 8. Time accuracy and the generation ratio (filters off, integer delay).
// Each repeat lands on its sample. The DC blocker (D-005) is the one element
// left in the loop, so generation 2 is g * (1+R)/2 of generation 1 (the
// blocker's impulse gain; float32 storage bounds the error near 1e-7), and
// later generations also carry the blocker's small negative tail from earlier
// passes, O((1-R) R^D): 3e-5 relative at 100 ms (spec §5.3). So "ratio = g
// exactly" (D-000 Q4) holds to 1e-3, not bit-exactly, while D-005 stands.
function generations(params, sr) {
  const r = X.echoRender({ sr, seconds: 0.9, params: Object.assign({ "time.sync": 0, "time.ms": 100, "fb.amount": 50, mix: 100, "filt.loop.on": 0 }, params), srcL: impulse(Math.ceil(0.9 * sr / 128) * 128) });
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
}

// ---- 9. Off means off (ADR-142): a settled-off filter's cutoffs change nothing.
{
  const base = { "filt.in.on": 0, "filt.out.on": 0, "filt.loop.on": 0 };
  const a = stress(Object.assign({}, base, { "filt.in.hpHz": 20, "filt.out.lpHz": 20000 })), b = stress(Object.assign({}, base, { "filt.in.hpHz": 1800, "filt.out.lpHz": 600 }));
  check(sameBits(a.L, b.L), "true bypass: with every filter off, extreme cutoffs leave the output bit-identical");
  const c = stress(Object.assign({}, base, { "filt.out.on": 1, "filt.out.hpHz": 20, "filt.out.lpHz": 20000 }));
  plant(!sameBits(a.L, c.L), "bypass check detects an 'open' filter left in circuit (20 Hz - 20 kHz, on)");
}

// ---- 10. Mid/side folds to its M line (Q-A4, by construction): the fold of the
// output for an input (L, R) equals the fold for (M, M), M = (L+R)/2.
{
  const fold = r => Float64Array.from(r.L, (x, i) => 0.5 * (x + r.R[i]));
  const n = Math.ceil(1.5 * SR / 128) * 128, L = noise(n, 5), R = noise(n, 6), M = Float32Array.from(L, (x, i) => 0.5 * (x + R[i]));
  const run = (Xm, l, r) => Xm.echoRender({ sr: SR, seconds: 1.5, params: { "route.mode": 2, "time.sync": 0, "time.ms": 90, "time.offR": 37, "fb.amount": 70, mix: 100, "mod.depthMs": 3 }, srcL: l, srcR: r });
  const maxDiff = (a, b) => a.reduce((m, x, i) => Math.max(m, Math.abs(x - b[i])), 0);
  const e = maxDiff(fold(run(X, L, R)), fold(run(X, M, M)));
  check(e < 1e-6, `mid/side: fold(out(L,R)) == fold(out(M,M)) — the fold IS the M line (max diff ${e.toExponential(1)})`);
  const leak = build(dsp.replace("function echoFb(r, j, y0, y1) { return", "function echoFb(r, j, y0, y1) { if (r === 2 && j === 0) return y0 + 0.2 * y1; return"));
  plant(maxDiff(fold(run(leak, L, R)), fold(run(leak, M, M))) > 1e-3, "fold check detects a planted S -> M feedback leak");
}

// ---- 11. Past unity the loop parks at the ceiling (ADR-142; D-005 as amended).
{
  const park = Xm => Xm.echoRender({ sr: SR, seconds: 3, params: { "time.sync": 0, "time.ms": 50, "fb.amount": 120, mix: 100, "filt.loop.on": 0 }, srcL: impulse(3 * SR) });
  const r = park(X), late = peak(r.L.subarray(2 * SR));
  check(r.L.every(Number.isFinite) && peak(r.L) < 1.3 && late > 0.5, `past unity (120 %) the loop parks below 1.3 and does not die (late peak ${late.toFixed(3)})`);
  const unlimited = build(dsp.replace("if (a > K.knee)", "if (a > 1e30)"));
  plant(peak(park(unlimited).L) > 1.3, "ceiling check detects the ceiling removed");
}

// ---- 12. The click reading (D-009 M2) separates a jump from a crossfade.
{
  // At 48 kHz: a jump's reading grows with the rate (a fixed step against a
  // smaller per-sample curvature), and the gate should test the common case.
  const m2 = jump => { const s = X.echoQA1Spec(0, "probe", 48000, { fb: 0, mix: 100, glide: 0.3, capSt: 12, fallback: true, lab: { jump }, params: { "time.change": 2 } });
    s.seconds = 3; const r = X.echoRender(s); return X.echoClickM2(r.L, Math.round(1.5 * 48000), r.L.length, 0.5, X.ECHO_PROBE_HZ, 48000, 2); };
  const clean = m2("xfade"), raw = m2("raw");
  check(clean < 1.5, `M2: a 3 ms crossfade jump reads clean (${clean.toFixed(2)})`);
  plant(raw > 5, `M2 flags a bare jump (${raw.toFixed(1)})`);
}

console.log(fail ? "lab_smoke: RED" : "lab_smoke: OK");
process.exit(fail);
