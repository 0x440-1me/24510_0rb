# Changelog

## [1.0.2] — Unreleased

- Post-launch verification now discovers the current Discord image after Squirrel updates and accepts a version change only when the canonical Update.exe path confirms the same installation root and channel.
- The CDP helper receives the fresh executable path; listener ownership is rechecked against the same process across the renderer handshake.
- Added pure Stable/PTB/Canary migration and rejection tests. The observed direct updater recovery succeeded, but the Orb GUI restart button and native renderer handshake still need live acceptance; no Quest or JavaScript payload was run.

## [1.0.1] — Released 2026-10-11

- Restart now uses the approved Squirrel Update.exe --processStart path and forwards loopback CDP startup arguments instead of launching versioned Discord.exe directly.
- Updater validation runs before Discord shutdown, and failed CreateProcessW startup reports the numeric Windows error; no forced quit is added, exact ownership checks and explicit consent remain. The suite is synthetic; a later direct Update.exe recovery and its limits are recorded under v1.0.2 validation, with no Orb GUI restart or renderer handshake claimed.

## [1.0.0] — Released 2026-10-11

- Native Win32 ribbon icon and four-state primary UI: restart/connect, start, stop, and completed/retry.
- Quest Home (`/quest-home`) is a required preflight before the approved original dispatch.
- Read-only eligible-quest status aggregation; explicit per-run consent and no automatic start.
- Safer stop messaging that does not claim cleanup, plus a read-only Retry scan and fail-closed unknown/rate-limit handling.
- Original JavaScript remains unchanged and source-pinned by SHA-256, with separate attribution and notice.
- Synthetic state, payload, process, dispatch/privacy, foundation, adapter, and mock GUI coverage; no real Discord execution.

## Historical v0.3 development checkpoint (VERSION was 0.2.0 at that checkpoint)

- Native Original Orb import/review and per-run consent, locked byte snapshot,
  SHA-256-verified outgoing dispatch, session duplicate guard and singleton UI.
- Separate bounded renderer stop hook; honest unknown-state reporting.
- Privacy-safe helper diagnostics and fail-closed normal endpoint ownership.
- About/version and missing-helper preflight; account-free consent/mock tests.
- Owner-authorized bundle of unchanged original, separately attributed and
  excluded from MIT; explicit package/verifier hash and notice checks.
- No original execution, real-account test, public push, merge or release.

## 0.2.0

- Native C11 Win32 interface and packaged native WinHTTP helper.
- On-demand renderer handshake/status, external Unicode payload selection, execution results and exceptions.
- Owned helper cancellation/deadlines, responsive workers and deferred busy close.
- Explicit restart confirmation, approved installation discovery, loopback ownership checks and graceful failure handling.
- Scoped MIT native license; original script/readme moved unchanged into legacy/.
- Public build/security/test documentation and deterministic package tooling.
- Hosted Windows Server 2025 build/mock CI: native process 52/52, Foundation 37/37 and GUI 27/27 with 310 assertions; downloaded build artifacts and repeatable packages verified.

The first verified [PR run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005830743) and [feature run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005826992) used account-free mocks. Prior harmless real-CDP checks used a separate Windows 11 local build. The user reports a successful manual Windows 11 production restart, followed by Connect and hello.js; this was not independently observed and is not a hosted-binary live test. Automated live restart remains blocked. Publication and downloaded-release verification records are maintained in [Issue #2](https://github.com/0x440-1me/24510_0rb/issues/2) and on the [release page](https://github.com/0x440-1me/24510_0rb/releases); this source entry is not itself a publication claim.

## Historical source

Original Git history and attribution are retained. See legacy/original-readme.md for the original author's notes; those artifacts have separate rights. The unchanged script is now an explicitly confirmed experimental payload under owner bundling permission; the historical readme remains repository-only.
