# Fix — CRLF robustness in the machine RESULT protocol (Windows `--isolate`/`--shard-results`)

> Bug fix. On Windows the child processes that emit the machine `RESULT\t…\n` protocol write through a
> **text-mode** `stdout`, so `\n` is translated to `\r\n`; the parent then parses lines that carry a
> trailing `\r`, corrupting the `status`/`query` fields. Make the protocol byte-clean and the parsers
> CRLF-tolerant. Implementation by the coding agent.

## Symptom / root cause

- A `--run-case` (isolate child) and `--shard-results` (worker child) write
  `std::cout << "RESULT\t" << … << "\n"` (`src/common/runner.cpp:329`, `emitShardResults`). On Windows,
  `std::cout` is **text mode**, so every `\n` becomes `\r\n` over the pipe.
- The parent splits on `\n` (`std::getline` / buffer `find('\n')`), leaving a trailing `\r` on each line:
  - **`parseIsolatedResultLine`** (`runner.cpp:333`, the `--isolate` parent parser) does **not** strip
    `\r`. With no message field, `status = line.substr(statusStart)` becomes e.g. `"pass\r"` and
    `statusFromName("pass\r")` fails → every isolated case is misreported. **This is the main bug.**
  - **`parseResultLine`** (`runner.cpp:1219`, the `--workers` parser): when `query` is the trailing
    field it becomes `"webgpu:…:*\r"`, which then fails the parent's query-match. (In practice
    `drainWorkerLines` at `runner.cpp:960` already strips the `\r` before calling it, so `--workers`
    works — but the function is not self-robust.)
- **Not affected:** `loadExpectations` / the crash-list loader already `trim()` each line
  (`runner.cpp:104`, strips ` \t\r\n`), so CRLF **expectations/crash-list files parse fine**. (Confirm
  with a test; do not regress.) POSIX is unaffected (no `\n`→`\r\n` translation).

## Scope

**In:**
- Make every machine-`RESULT` line parser tolerant of a trailing `\r` (defense-in-depth, also covers
  hand-edited/mixed-ending input).
- Emit the `RESULT` protocol in **binary** `stdout` on Windows in the child modes (root-cause: no
  `\r\n` injection in the first place).
- Unit tests for CRLF in `RESULT`-line parsing and in expectations loading.

**Out:**
- The **human** summary/`pass…fail…` output stays text-mode (native `\r\n` on a Windows terminal is
  correct there — only the inter-process machine protocol must be byte-clean).
- No behavior change on POSIX; no change to query semantics.

## Interfaces

### 1. Essential — strip a trailing `\r` in every RESULT-line parser

Add one small helper (e.g. in the anonymous namespace of `runner.cpp`):
```cpp
inline std::string_view dropTrailingCR(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}
```
Apply it at the line boundary in:
- **`parseIsolatedResultLine`** (`runner.cpp:333`) — strip `\r` from each `line` read by `getline`
  before splitting into status/message. (Fixes `--isolate` on Windows.)
- **`parseResultLine`** (`runner.cpp:1219`) — strip a trailing `\r` from `line` on entry, so the
  function is self-robust regardless of caller.
- **`finishWorker`** leftover (`runner.cpp:975`) — the no-trailing-`\n` remainder passed to
  `recordWorkerLine` should go through the same strip (today only `drainWorkerLines` strips it).

(`drainWorkerLines` at `runner.cpp:960` already strips `\r` — keep it; the helper can replace the inline
`pop_back` for consistency.)

### 2. Root-cause — binary `stdout` for the RESULT-emitting child modes (Windows)

At the start of the **`--run-case`** (isolate child) and **`--shard-results`** (worker child) code paths
— before any `RESULT` line is written — set `stdout` to binary on Windows so `\n` is not translated:
```cpp
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);   // <io.h>, <fcntl.h>
#endif
```
Do this **only** in those two child modes (their `stdout` is exclusively the machine protocol), not in
the orchestrator / normal-run paths (which print human output). This makes the protocol byte-identical
to POSIX.

### 3. Expectations / crash-list

Already CRLF-safe via `trim` — no code change; add a regression test (below).

## Acceptance criteria

GPU-free (coding agent — **you verify**):
- [ ] `cmake --build build-yawgpu --target cts cts_unittests` succeeds (+ wgpu/dawn); `cts_unittests`
      exit 0.
- [ ] New `cts_unittests` cases:
  - `parseResultLine("RESULT\tpass\twebgpu:…:*\r")` → status `Pass`, query `webgpu:…:*` (**no** trailing
    `\r`), empty message; and a 4-field `…\tmessage\r` variant parses the message without `\r`.
  - `parseIsolatedResultLine(query, "RESULT\tpass\r\n")` and `"RESULT\tfail\tmsg\r\n"` → correct
    `TestStatus` (not `Crash`/"unknown RESULT status"), message without `\r`.
  - `loadExpectations` equivalent: a buffer with CRLF (`webgpu:…:*\r\n`) yields the same prefixes/exact
    entries as the LF version (lock in the existing `trim` behavior).
- [ ] `git status --porcelain expectations/` empty; `git diff --check` clean.

Claude verifies on real GPU (sandbox off, Metal): a `--isolate` run is unchanged on POSIX (no
regression). The **Windows** `--isolate` fix is confirmed by the unit tests + the binary-mode change and
by the Windows maintainer on a real CRLF/Windows run.

## Verification

1. Build `cts cts_unittests`; `cts_unittests` → 0 (incl. the new CRLF cases).
2. `git status --porcelain expectations/` empty; `git diff --check` clean.

## References

- `src/common/runner.cpp` — `trim` (78), `loadExpectations` (87), RESULT child emit (329) +
  `emitShardResults`, `parseIsolatedResultLine` (333), `drainWorkerLines` (947, already strips `\r`),
  `finishWorker` (968), `parseResultLine` (1219); the `--run-case` and `--shard-results` dispatch in
  `runQueries`.
- `include/cts/test.h` — `parseResultLine` decl (276), `RunOptions` (`shardResults`, isolate).
- `src/unittests/main.cpp` — existing `parseResultLine` tests to extend with CRLF cases.
- `src/common/runtime/main.cpp` — flag parsing for `--run-case` / `--shard-results` (where to set the
  binary-mode for the child).
