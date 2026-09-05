# Orb V1 vs V2: research architecture

Orb v2 is a deliberately narrow, read-only research harness. It does not run the automation paths in `dc_cmd_script.js`, change local Discord state, or send quest-progress requests.

| Concern | V1 (`dc_cmd_script.js`) | V2 (`orb_v2.js`) |
| --- | --- | --- |
| Scope | Legacy exploration and automation artifact | Quest inspection plus a local deterministic mock only |
| Run ownership | Global flags and scattered state | One `RunnerContext` per run |
| Cancellation | Polling a boolean | `AbortController`, including abortable waits |
| Cleanup | Branch-specific restoration | Registered LIFO cleanups, idempotent by design |
| Rate limiting | Unbounded retry loop | Eight-retry budget with abortable backoff |
| Discord integration | Eager store discovery and mutation paths | Lazy discovery; only `GET /quests/@me` in inspection |
| Verification | Manual console observation | Node test harness with all Discord APIs mocked |

The V2 public surface is intentionally small:

```js
orbV2.inspect(); // Read-only inspection of current quest metadata
orbV2.mock();    // Local lifecycle simulation; no Discord request
orbV2.status();  // Current run state
orbV2.stop();    // Abort an active V2 run
```

Future V2 work must preserve that boundary. In particular, it must not add reward/progress writes, local state spoofing, event fabrication, or bypass logic.
