# Orb V1 vs V2: research architecture

V2 keeps a pasteable single-file browser entry point, but gives the major responsibilities explicit boundaries: lifecycle ownership, the Discord read adapter, parsing/summarization, the local mock, and the public API.

| Concern | V1 legacy artifact | V2 research harness |
| --- | --- | --- |
| Scope | Broad legacy automation experiment | Read-only inspection and local mock only |
| Run ownership | Shared flags and long-lived state | One `RunnerContext` owns each run |
| Cancellation | A polled boolean | `AbortController` plus abortable waits |
| Cleanup | Task-branch restoration | Registered LIFO cleanup with one idempotent cleanup promise |
| Retry behavior | Potentially unbounded | Eight retries with clamped, abortable backoff |
| Discord coupling | Discovery and task behavior interwoven | Capability discovery and GET-only reads behind one adapter |
| Payload drift | Assumes familiar shapes | Safe summaries with retained diagnostics |
| Verification | Manual console observation | Isolated Node tests with mocked timers, webpack, and API |

## Lifecycle invariant

At most one context can be active. `run` creates it, `stop` aborts it, and the run wrapper clears the global reference in `finally` on success, failure, or cancellation. Cleanup functions are consumed once in LIFO order. Cleanup failures are retained and reported; they are not silently discarded or allowed to leave the active-run guard stuck.

An abortable wait removes its listener and clears its timer whether the timer wins or the abort wins. Repeated cleanup and repeated `stop()` calls are safe. A later run may begin once the previous wrapper has released ownership.

## Discord boundary

The adapter discovers a candidate API by capability (`get`), rather than an exact export key. It creates a detailed error when webpack capture, the cache, or an API capability is unavailable. It accepts only well-formed HTTP response objects and quest arrays; malformed integration responses fail visibly instead of being mistaken for an empty result.

The adapter is intentionally limited to `GET /quests/@me`. No V2 module sends quest progress, changes client state, fabricates activity, or adds a bypass.

## Parser boundary

`summarizeQuest` never mutates a quest. It accepts missing or malformed entries, marks invalid dates/configs/types as diagnostics, and returns a stable summary shape. Unexpected task versions and task names are visible in those diagnostics, rather than being treated as supported behavior.
