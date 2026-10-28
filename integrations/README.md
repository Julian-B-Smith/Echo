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

## Outbound threads

| provider | file in THEIR tree | what |
|---|---|---|
| HYPERSAW (repo `horde`) | `integrations/echo/notice-spinup.md` | we exist; horde is our consumer. **Filed 2026-10-23**, horde PR #1201 (merged), `ball: none` (T-002) |
| autonomous | `integrations/echo/brief.md` | register Echo in the ecosystem tracks. **Filed 2026-10-23**, autonomous PR #61, `ball: autonomous`, respond by 2026-11-06 (T-002) |
| FOUNDATIONS | `integrations/echo/brief.md` | `echo-002`: four ABI questions; listed as a module inside horde. **Filed 2026-10-23**, FOUNDATIONS PR #133 (merged), `ball: FOUNDATIONS`, respond by 2026-11-06 (T-012) |

Drafts are kept in `docs/filings/`; the copy in the correspondent's tree is the record.
