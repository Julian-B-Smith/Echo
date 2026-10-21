// plan_map_smoke.mjs — the plan map still reads ROADMAP.md correctly (docs/design/echo-plan-map.html).
//
// The plan map has no data of its own: it parses ROADMAP.md on load (the page's header says why). So the
// way it can go wrong is not drift between two copies but the PARSER falling behind the ROADMAP's shape —
// a renamed section, a new heading style, a status word it does not know — and then quietly drawing a
// wrong picture. This runs the page's own MODEL section (sliced at its banners, the same code the browser
// runs) against the real ROADMAP.md and asserts:
//   1. the page's four self-checks pass on the ROADMAP, and each must-fail control fires on its plant;
//   2. Echo's seven phases are all found (control: the M2 heading demoted, so M2 vanishes);
//   3. the MODEL is pure — no DOM, fetch, storage or clock — by a static scan (control: a `Date.now()`
//      planted into a copy of the slice) and at run time, with Date and performance poisoned.
// It pins no phase STATE: the ROADMAP moves, and this must stay green when it does. A missing banner
// throws, because a mis-slice would make every check vacuous (same rule as tools/lab_smoke.mjs).
import { readFileSync } from "node:fs";

const PAGE = new URL("../docs/design/echo-plan-map.html", import.meta.url);
const ROADMAP = new URL("../ROADMAP.md", import.meta.url);
const html = readFileSync(PAGE, "utf8"), roadmap = readFileSync(ROADMAP, "utf8");
const A = html.indexOf("/* ================= MODEL"), B = html.indexOf("/* ================= UI");
if (A < 0 || B < 0 || B <= A) throw new Error("plan_map_smoke: MODEL/UI banners not found — slice anchor lost");
const model = html.slice(A, B);

let fail = 0;
const check = (ok, msg) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${msg}`); if (!ok) fail = 1; };

const EXPORTS = "return { parseRoadmap, runSelfChecks };";
const clock = () => { throw new Error("clock read inside the plan map's MODEL"); };
// `new Date()`, `Date.now()` and `Date.UTC()` all throw; a bare function would let `Date.now` be a TypeError
// instead, and the run-time plant would then "fire" for the wrong reason.
const poisonedDate = Object.assign(function Date() { clock(); }, { now: clock, UTC: clock, parse: clock });
const build = src => new Function("Date", "performance", "document", "window", "fetch", "localStorage", src + "\n;" + EXPORTS)(
  poisonedDate, { now: clock }, undefined, undefined, undefined, undefined);
const X = build(model);

// ---- 1. the page's self-checks, with their controls ------------------------------------------------
console.log("plan map: self-checks on ROADMAP.md");
for (const { name, real, control } of X.runSelfChecks(roadmap)) {
  check(real.ok, `${name}: ${real.msg}`);
  check(control.fired, `${name}: control fires (${control.msg})`);
}

// ---- 2. every phase found ----------------------------------------------------------------------------
const PHASES = ["Phase 0", "Phase A", "M1", "M2", "M3", "M4", "Phase N"];
const missing = md => PHASES.filter(id => !md.phases.some(p => p.id === id));
const md = X.parseRoadmap(roadmap);
check(missing(md).length === 0, `all ${PHASES.length} phases found (${md.phases.map(p => p.id + "=" + p.state).join(", ")})`);
const demoted = roadmap.replace(/^### M2 — /m, "#### M2 — ");
check(demoted !== roadmap && missing(X.parseRoadmap(demoted)).includes("M2"), "plant fires: the M2 heading demoted reads as M2 missing");

// ---- 3. the MODEL is pure ----------------------------------------------------------------------------
// Static layer: the slice's code (comments stripped) names no DOM, network, storage or clock API.
const IMPURE = /\b(document|window|fetch|localStorage|sessionStorage|Date|performance|Math\.random|setTimeout|XMLHttpRequest)\b/;
const code = s => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:"'`])\/\/.*$/gm, "$1");
check(!IMPURE.test(code(model)), "MODEL names no DOM, fetch, storage or clock API");
const planted = model.replace("function parseRoadmap(text) {", "function parseRoadmap(text) { const _t = Date.now();");
if (planted === model) throw new Error("plan_map_smoke: purity plant anchor not found");
check(IMPURE.test(code(planted)), "plant fires: a Date.now() planted in the MODEL is seen by the static scan");
let threw = false;
try { build(planted).parseRoadmap(roadmap); } catch (e) { threw = /clock read/.test(e.message); }
check(threw, "plant fires: the same Date.now() throws at run time (Date poisoned)");

console.log(fail ? "plan map smoke: RED" : "plan map smoke: green");
process.exit(fail);
