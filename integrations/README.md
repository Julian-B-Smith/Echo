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
| — | — | none yet |

## Outbound threads

| provider | file in THEIR tree | what |
|---|---|---|
| HYPERSAW (repo `horde`) | `integrations/echo/notice-spinup.md` | we exist; horde is our consumer — draft in `docs/filings/`, **not yet filed** (ROADMAP T-002) |
| autonomous | `integrations/echo/brief.md` | register Echo in the ecosystem tracks — draft in `docs/filings/`, **not yet filed** (T-002) |
| FOUNDATIONS | — | deferred until the C++ first touches the FX-operator ABI (survey Q6) |

Drafts are kept in `docs/filings/`; the copy in the correspondent's tree is the record.
