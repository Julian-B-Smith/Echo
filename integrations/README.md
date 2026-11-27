# integrations/ — Echo's inbound mailbox

**Briefs TO Echo land here**, one subdirectory per correspondent
(`integrations/<sender>/`). That subdirectory is the sender's designated intake
slot and the only path in this tree a visiting agent may write to
(autonomous `doctrine/INTEGRATIONS.md` §3, rule zero: writes stay home; the
mailbox is the one exception). Committing a filed brief is a resident act.

| artifact | lives in |
|---|---|
| a brief filed TO us | `integrations/<sender>/` **here** |
| our response to it | here, beside the brief |
| a brief WE file to someone | **their** tree, in `integrations/echo/` |
| their response to our brief | **their** tree — pull and read; it never arrives here |

Exchange files carry frontmatter: `id`, `from`, `to`, `status`, `ball`,
`filed`, `seq`, `respond-by`. `ball:` names the one side that owes the next
move; `status:` is lifecycle; neither does the other's job.

## Inbound

| from | file here | what |
|---|---|---|
| HYPERSAW (horde) | `hypersaw/notice-macro-modify-law.md` | seq 2, `ball: none`, filed 2026-10-24 (PR #22, merged 2026-10-25): horde's slot resolves macros under ADR-251's `modify` law; the optional fixed row (RS-5.8). Nothing owed. Read against our rulings: it matches Echo's factory bank on `modify` (D-035, ruling 3) and the four `fixedControls` (D-035 ruling 4; locked by D-041). |
| HYPERSAW (horde) | `hypersaw/notice-state-budgets.md`, answered by `hypersaw/response-state-budgets.md` | seq 3 (PR #34) and seq 4: horde re-derives every module's state budget (ADR-351). Answered 2026-11-02: 478 B largest factory preset, at most 910 B by calculation, no caps. `ball: HYPERSAW` for the ruled budgets (T-043). |
| HYPERSAW (horde) | `hypersaw/notice-state-budgets-ruled.md` | seq 5, filed 2026-11-15: horde's human ruled 4 KiB per corner (ADR-401); `ball: Echo` for the constant. Done by T-043 (D-051): `state::kMaxBytes` is 4,096, pinned by the suite. **Closed 2026-11-26** by `hypersaw/close-state-budgets.md` (seq 6, `status: closed`). The readiness notice (T-027) reports it. |

## Outbound threads

| provider | file in THEIR tree | what |
|---|---|---|
| HYPERSAW (repo `horde`) | `integrations/echo/notice-spinup.md` | we exist; horde is our consumer. **Filed 2026-10-23**, horde PR #1201 (merged), `ball: none` (T-002) |
| autonomous | `integrations/echo/brief.md` | register Echo in the ecosystem tracks. **Filed 2026-10-23**, autonomous PR #61, `ball: autonomous`, respond by 2026-11-06 (T-002) |
| FOUNDATIONS | `integrations/echo/brief.md` | `echo-002`: four ABI questions; listed as a module inside horde. **Filed 2026-10-23**, FOUNDATIONS PR #133 (merged), `ball: FOUNDATIONS`, respond by 2026-11-06 (T-012) |
| HYPERSAW (repo `horde`) | `integrations/echo/brief-fixed-row-modes.md` | `echo-003`: Echo offers no macros; a row entry that follows the mode, and a stepped entry as a selector (D-050, T-045). **Filed 2026-11-03**, horde PR #1359 (merged). **Answered 2026-11-15** (seq 3, ADR-399): one knob, two lanes; RC-7.4; §12.7 objects. Ruled by our human in D-050 A5 |
| HYPERSAW (repo `horde`) | `integrations/echo/brief-bank-import.md` | `echo-004`, same thread: does Echo's bank file import as it is (D-050 A1)? **Filed 2026-11-03**, horde PR #1363. **Answered 2026-11-15** with `echo-003` (seq 3, `response-fixed-row-modes.md` in horde's tree): yes, as it is. |
| HYPERSAW (repo `horde`) | `integrations/echo/brief-meter-switch-form.md` | `echo-005`, same thread, seq 4: accepts two lanes, RC-7.4 and the labels; asks for the meter beside the tempo and a §12.7 form for a switch another key chooses (D-050 A5, T-045). **Filed 2026-11-24**, horde PR #1521, `ball: HYPERSAW`, respond by 2026-12-01 |

Drafts are kept in `docs/filings/`; the copy in the correspondent's tree is the record.
