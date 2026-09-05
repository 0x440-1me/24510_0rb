# Orb v2 validation notes

Run the isolated suite with a current Node.js release:

```sh
node --test orb_v2.test.js
```

The test harness evaluates `orb_v2.js` in a VM. It never loads Discord, contacts a network, or executes the legacy V1 script. Webpack, the API client, and timers are all controlled test doubles.

| Scenario | Assertion |
| --- | --- |
| Mock lifecycle | Completion returns a completed mock and its LIFO cleanup trace |
| Duplicate start / repeated stop | Second start rejects; concurrent stops and later restart are safe |
| Cleanup error | Remaining cleanups run, error is recorded, and cleanup stays idempotent |
| Abort during retry or client read | Retry timer/listener are removed; cancellation wins over a late client failure |
| 429 exhaustion | Initial request plus exactly eight retries, then visible failure |
| Fetch dedupe and cache | Concurrent readers share one request; successful result is cached |
| Failed-run release | Invalid API data releases global ownership and allows a new run |
| Empty list | Empty quest array is a valid read result |
| Malformed quest entries | Input stays untouched and summaries retain diagnostics |
| Config/task drift | Unknown task types and config versions are reported, not supported |
| Expired/completed state | Neither is summarized as active |
| Missing Discord capability | Missing webpack or API module fails descriptively and releases ownership |

## What is not unit-testable here

The suite tests contracts under controlled inputs. It cannot prove that a current Discord build exposes the expected webpack array or API shape, nor can it validate Discord server behavior. Those are intentionally treated as integration boundaries: V2 reports the mismatch clearly, while remaining read-only.
