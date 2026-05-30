# <task id> — <short title>

> Template for a task spec. Copy to `specs/<phaseN>-<slug>.md`, fill in, delete this line and the
> guidance in angle brackets. See [`workflow.md`](workflow.md).

## Goal

<One sentence: what this task produces.>

## Scope

**In:**
- <thing this task must do>

**Out (non-goals):**
- <explicitly excluded; deferred to a later task/spec>

## Interfaces

<Exact signatures, headers, file paths, CLI flags, or struct/class shapes this task must produce
or conform to. Quote from the relevant `docs/` section. Be precise enough that two implementers
would produce compatible code.>

```cpp
// e.g. the public surface this task adds to include/cts/...
```

## Acceptance criteria

<Checkable, binary statements. Each should be something Claude can verify by running a command or
inspecting output.>

- [ ] <criterion 1, e.g. "`cmake --build build --target cts` succeeds">
- [ ] <criterion 2, e.g. "`build/cts_unittests` exits 0">
- [ ] <criterion 3, e.g. "`build/cts --list '<query>'` prints exactly N lines">

## Verification

<How to prove the criteria: build targets, unit tests to add/run, sample commands and expected
output.>

## References

- `docs/<file>.md` §<section> — <why relevant>
