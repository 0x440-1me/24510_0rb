# Validation status

## Local v1.0.0 pre-release verification (2026-10-10)

Saved local evidence records native C state 50/50, payload 11/11, process 52/52, v0.3 synthetic dispatch/privacy/ownership 23/23, and Foundation 37/37 passing entries. The Foundation JSON records all passes; the outer command's exit code was not captured.

The native mock GUI script reports 8/8 modes passed in `bin/evidence-v1-eightstates-final-20261010/summary.json`. Its Ready, Running, and All completed screenshots are genuine Win32 mock-mode window captures; they are not live Discord validation.

The adapter suite recorded 80/80 synthetic checks against the exact SHA-256-verified adapter source in a separate isolated Node environment, not Windows Edge. The Windows Edge fixture was blocked by a GPU crash.

The local native executable reports FileVersion and ProductVersion 1.0.0. The original JavaScript remains unchanged and SHA-256-pinned. A private development ZIP was verified before these documentation changes; these edits change package inputs, so the post-documentation ZIP has not yet been verified.

No real Discord execution or real-account validation was performed for this v1.0.0 pre-release evidence.

Hosted account-free mock results and prior permitted local real-CDP results have
different scopes. Windows 10 version 1809+ and Windows 11 x64 are declared targets;
only the Windows 11 development VM has real Discord integration evidence.

## Verified hosted build and mock CI

The first verified [PR run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005830743)
and [feature push run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005826992)
completed successfully on Windows Server 2025 at feature revision
`ba028bc06076d3784d3e940c5fc1360eb7a26c24`. Each run built without compiler warnings,
ran native process 52/52, Foundation 37/37 and mock GUI 27/27 with 310 assertions,
and verified imports, clean-source provenance, all 14 manifest asset hashes,
the exact 15-entry ZIP whitelist, README assets and repeated ZIP hashes.
Both package/evidence artifacts were downloaded and verified independently.
No Discord installation, account or real restart was used on the hosted runner.

The recorded environment was Windows Server 2025 build 26100, GCC 16.2.0 Rev5
(package 16.2.0-5), binutils 2.47-4, UCRT headers/CRT
14.0.0.r426.g4564ee4b5-1, PowerShell 7.6.6 and .NET 10.0.12.
The push manifest identifies the feature revision; the PR manifest identifies
the actual synthetic merge checkout `ea9c5b35758bc057a06657ab79f84c164d1ae070`.
Their source-content snapshot and executable hashes matched under that toolchain.
Their ZIP hashes differ because the recorded source revisions differ. This does
not claim identity with local builds or future compiler/runner/runtime versions.
Every later source or documentation commit requires its own current-head CI.
See [current Actions](https://github.com/0x440-1me/24510_0rb/actions).

## Historical local development evidence

| Class | Recorded result |
| --- | --- |
| Native helper/controller | Foundation 34/34; harmless real Discord 14/14; bounded cancel/timeout and native handle plateau checks passed |
| Discovery/shared restart foundation | Native mocked-state/socket ownership 52/52; real read-only 5/5; unchanged process/sentinel evidence |
| Mock/local GUI | 27/27 cases with 280 native assertions; Unicode picker/persistence, failure/cancel stages, responsive stalled Stop/close |
| Production GUI harmless live/local | 14/14 cases with 130 assertions; handshake, hello, same-window repeats, exception, Unicode file edits, Cancel/Escape/X and cleanup |
| Final wording/snapshot correction | Affected mock cancellation/snapshot 17/17; production mock-exclusion audit 14/14 |

The hosted Foundation total includes three named ancillary checks beyond the
historical 34-case result: invalid filename, normal-exit handle cleanup and
launch-failure handle cleanup. The hosted GUI retains 27 cases and adds 30 snapshot
label assertions to the historical 280: 23 startup and seven completed-operation
checks. These totals describe the actual saved runs rather than interchangeable
counts of one unchanged suite.

The saved Windows 11 production GUI suite used GCC 16.2.0 Rev4. It predates the
final snapshot-label wording change; that change was checked with the affected
mock case without repeating live runs. The hosted Rev5 executables have not been
tested against real Discord. Initial failures were retained, diagnosed and
corrected before the passing counts. No Quest/account-affecting JavaScript was
tested.

## User-reported manual production result

The user reported a successful manual test in the Windows 11 development VM:
they confirmed Restart Discord, Discord reopened, Connect succeeded and hello.js
ran. This is a user report for the locally available production app, not an
independently observed test or validation of hosted/release binaries against real
Discord. No automated production confirmation, restart, shutdown or launch was
performed. No timing, current process/endpoint state or wider workload behavior
is inferred from that report.

## Blocked or not verified

- Automated actual Discord restart/shutdown/launch: blocked by the execution guard;
  mocked success does not prove live restart. The manual result above is user-reported.
- Public Windows PowerShell test wrapper in the development VM: policy refused it.
  No bypass or alternate launch retry was used. Approved direct component evidence
  and the separate hosted runner's approved pwsh execution are distinct results.
- Physical high-DPI monitor migration: not verified; synthetic 144-DPI handling was.
- Allocation/CreateWindow/PostMessage failure injection and OS query/ACL faults:
  not runtime-injected; corresponding ownership/fail-closed source was reviewed.

The tag workflow builds and verifies tagged source before publication. After a
release is published, verify its publicly downloaded ZIP/checksum and manifest
against the tagged revision. Build-artifact checks do not prove public-release
download verification. See [Issue #2](https://github.com/0x440-1me/24510_0rb/issues/2)
and the [release page](https://github.com/0x440-1me/24510_0rb/releases) for those
publication and verification records.

Private development-VM evidence, personal paths, Discord process IDs and account
data are excluded from public artifacts. Hosted CI evidence contains account-free
mock/build logs, including its own runner paths and fixture process IDs.
## Historical local v0.3 checkpoint (2026-10-10)

JijiPC, Windows 11, MSYS2 UCRT64 GCC 16.2.0 Rev4; warnings treated as errors.
Native process tests 52/52, locked-payload/SHA-256 tests 11/11, Foundation
37/37, v0.3 synthetic dispatch/privacy/ownership checks 23/23, native mock GUI
39/39. The first runs found picker timing, DPI harness timeout, relative
evidence path and Windows PowerShell fixture compatibility issues; these were
fixed and the affected suites rerun successfully. No execution guard was
bypassed. Separate ports avoid test-fixture collisions.

The owner-authorized development ZIP verified 17 manifest asset hashes,
18 exact entries, original SHA-256, attribution and the separate MIT exclusion.
The original is only copied/hashed. No original execution, account Quest
request, Discord restart, push, PR or release was performed. Live behavior,
cooperative stop cleanup and Windows 10 remain unverified. The existing
screenshots elsewhere in this document are historical v0.2.0 evidence.

