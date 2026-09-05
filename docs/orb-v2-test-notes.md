# Orb v2 validation notes

Run the focused test matrix locally with a current Node.js release:

```sh
node --test orb_v2.test.js
```

The harness evaluates Orb v2 in an isolated VM. It does not load Discord, reach the network, or execute `dc_cmd_script.js`.

| Scenario | Expected result |
| --- | --- |
| Mock run | Completes with a completed mock quest and observable LIFO cleanup trace |
| Cancellation mid-wait | `stop()` aborts the pending wait and the run becomes inactive |
| Duplicate start | Second run rejects while the first owns the context |
| Thrown error | The active-run guard resets; context cleanup is LIFO and idempotent |
| Malformed quest payload | Metadata is safely summarized without writes |
| Repeated HTTP 429 | Exactly nine attempts: initial call plus eight bounded retries |
| Missing webpack chunk or API module | A clear error is surfaced and no active run remains |

The test-only hook is injected by the isolated VM before load so the harness can unit-test lifecycle primitives. It is absent from the production Orb v2 API and does not expose any write or spoofing behavior.
