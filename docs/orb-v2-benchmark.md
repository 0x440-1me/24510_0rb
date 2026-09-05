# Orb v2 comparison methodology

Use this procedure to compare Orb v2 with another engineering implementation fairly. Both candidates must run entirely against the same mocked inputs. Live Discord output, reward/progress behavior, and bypass techniques are out of scope.

## Cases

1. Normal mock completion and cleanup order.
2. Cancellation during a pending wait.
3. Duplicate start and repeated stop.
4. A cleanup function that throws.
5. Retry cancellation and retry-budget exhaustion.
6. Concurrent read deduplication and cache reuse.
7. Empty, malformed, expired, completed, and unknown-version quest payloads.
8. Missing webpack cache and missing API capability.

## Scorecard

For each case, record pass/fail and a short note. Score the same five dimensions from 0 to 2:

| Dimension | 0 | 1 | 2 |
| --- | --- | --- | --- |
| Correctness | Fails the expected outcome | Partial or flaky | Deterministic expected behavior |
| Cleanup | Leaks or masks errors | Partial cleanup | LIFO, idempotent, diagnosable |
| Cancellation | Cannot stop safely | Stops only in common path | Stops waits/retries and releases ownership |
| Diagnostics | Generic or silent | Some context | Actionable boundary and payload messages |
| Maintainability | Responsibilities intertwined | Some boundaries | Small, documented, testable boundaries |

Keep the evidence: Node version, test command, pass/fail count, and any manual read-only integration observation. The goal is a technical comparison of reliability and design, not a contest to alter platform behavior.
