# Orb v2 — lifecycle-safe research harness

Orb v2 is a small, read-only JavaScript research harness for studying resilient client integration patterns: cancellation, cleanup ownership, defensive parsing, and testing against unstable internal boundaries.

It is deliberately not a quest-completion tool. V2 does not write quest progress, alter local Discord state, fabricate events, or provide bypass mechanisms. Its live mode makes one read-only request for quest metadata; its mock mode does not access Discord at all.

## Quick start

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

## Project layout

| Path | Purpose |
| --- | --- |
| `orb_v2.js` | Browser entry point: lifecycle, read-only Discord adapter, parsing, mock, and small public API |
| `orb_v2.test.js` | Isolated Node test harness with mocked Discord, webpack, HTTP, and timers |
| `docs/orb-v2-architecture.md` | V1-versus-V2 design comparison and boundaries |
| `docs/orb-v2-test-notes.md` | Test matrix and local validation instructions |
| `docs/orb-v2-benchmark.md` | Fair comparison method for another implementation |
| `dc_cmd_script.js` | Legacy V1 artifact; not part of V2 and intentionally not extended |

## What V2 fixes from V1

The legacy script centralizes work in one long control flow, relies on a global stop flag, retries rate limits without a budget, and restores resources in task-specific branches. That makes cancellation, failures, and client-version drift difficult to reason about.

V2 assigns every run a `RunnerContext` with an `AbortController`, abortable waits, LIFO cleanup registration, bounded 429 retries, and a single-active-run guard. Discord-specific discovery is contained in a read adapter; quest parsing emits diagnostics instead of assuming a fixed payload shape.

See [the architecture note](docs/orb-v2-architecture.md) for the concise side-by-side.

## Comparison method

To compare Orb v2 fairly against another implementation, run both through the same isolated lifecycle and parsing cases, record behavior and diagnostics, then compare only the observable results. Do not compare or reward capabilities that alter quest progress or evade platform controls.

The exact matrix and scorecard are in [the benchmark guide](docs/orb-v2-benchmark.md).

## Known boundary

Discord internal exports and payload shapes can change without notice. V2 detects and describes missing webpack/API capabilities and malformed payloads, but no local test can guarantee compatibility with every live Discord build. Treat `inspect()` output as research data, not a stable contract.
