# specs/

Task specifications and acceptance criteria the **coding agent** implements against. Claude
authors these; the coding agent reads them and writes code. Design rationale lives in
[`../docs/`](../docs/); `specs/` is the *what to build next and how we'll know it's done* layer.

## Contents

- `reference/workflow.md` — the planning ↔ coding-agent loop, git rules, build/test commands.
- `reference/task-template.md` — the template every task spec follows.
- `phase0-*.md`, `phase1-*.md`, … — one spec per bounded unit of work (added as we go).

## Relationship to other layers

| Layer | Where | Owner | Lifetime |
|-------|-------|-------|----------|
| Design & architecture | `docs/` | Claude | durable, committed |
| Task specs + acceptance criteria | `specs/` | Claude | durable, committed |
| Live task handoff | `HANDOFF.md` (root) | Claude | ephemeral, git-ignored |
| Live implementation report | `REPORT.md` (root) | coding agent | ephemeral, git-ignored |
| Production code | `src/`, `include/`, `tools/`, `CMakeLists.txt` | coding agent | durable, committed |

See [`reference/workflow.md`](reference/workflow.md) for the loop and [`../CLAUDE.md`](../CLAUDE.md)
for the permanent rules.
