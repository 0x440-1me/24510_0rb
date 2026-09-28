# 24510_0rb — Discord quest research lab

This repository contains two deliberately separate lines of work:

- **Legacy V1 (`dc_cmd_script.js`)** — the original Discord quest research/automation artifact on `main`.
- **Orb v2 (`orb_v2.js`)** — a lifecycle-safe, read-only research harness for studying cancellation, cleanup ownership, defensive parsing, retries, and unstable client integration boundaries.

> **For educational research only.** Discord internals can change without notice; treat both implementations as experiments rather than stable APIs.

## Orb v2

Orb v2 does **not** write quest progress, alter local Discord state, fabricate events, or provide bypass mechanisms. Its live mode makes one read-only request for quest metadata; its mock mode does not access Discord at all.

Run the deterministic test suite with a current Node.js release:

```sh
node --test orb_v2.test.js
```

For manual read-only research in a Discord client console, load `orb_v2.js`, then use:

```js
orbV2.inspect(); // Read-only quest metadata summary
orbV2.mock();    // Local lifecycle simulation; no Discord access
orbV2.status();  // Active-run state and cleanup status
orbV2.stop();    // Abort the active V2 run
```

Only one V2 run may own the process at a time. A second start rejects until the first finishes or is stopped. Re-running after a stopped or failed run is supported.

### What V2 changes architecturally

The legacy script centralizes work in one long control flow and shares long-lived state. V2 instead assigns every run a `RunnerContext` with an `AbortController`, abortable waits, LIFO cleanup registration, bounded 429 retries, a single-active-run guard, and defensive parsing with diagnostics.

Discord-specific discovery is contained behind a read adapter. See [the architecture note](docs/orb-v2-architecture.md) for the side-by-side design comparison.

## Project layout

| Path | Purpose |
| --- | --- |
| `orb_v2.js` | V2 browser entry point: lifecycle, read-only Discord adapter, parsing, mock, and small public API |
| `orb_v2.test.js` | Isolated Node test harness with mocked Discord, webpack, HTTP, and timers |
| `docs/orb-v2-architecture.md` | V1-versus-V2 design comparison and boundaries |
| `docs/orb-v2-test-notes.md` | Test matrix and local validation instructions |
| `docs/orb-v2-benchmark.md` | Fair comparison method for another implementation |
| `dc_cmd_script.js` | Legacy V1 artifact; maintained separately from V2 |

## Legacy V1 status

The V1 artifact remains available for historical/research comparison and is intentionally kept separate from Orb v2.

The September 26, 2026 update to V1 added:

- automatic enrollment for one available quest before a runner cycle,
- recurring runner cycles with a 60-second minimum pause,
- Discord Retry-After handling,
- exit when no unaccepted available quests remain,
- `window.stopQuestProgram()` for stopping the full recurring loop while retaining `window.stopQuestRunner()` for the active runner.

These V1 changes are preserved in the Orb v2 PR branch so the branch stays current with `main`; they are not part of the V2 API.

## Comparison method

To compare Orb v2 against another implementation, run both through the same isolated lifecycle and parsing cases, record behavior and diagnostics, and compare observable reliability rather than platform-altering behavior.

The exact matrix and scorecard are in [the benchmark guide](docs/orb-v2-benchmark.md).

## Known boundary

Discord internal exports and payload shapes can change without notice. V2 detects and describes missing webpack/API capabilities and malformed payloads, but no local test can guarantee compatibility with every live Discord build. Treat `inspect()` output as research data, not a stable contract.

## Author

- `0x440_1me`
