## v1.0.2 development status — not released

The latest public release remains v1.0.1. The confirmed v1.0.1 defect was a post-update path mismatch: Squirrel selected a newer app-* executable, while the controller kept probing the pre-update image. v1.0.2 development permits only same-channel/same-root version migration and binds the CDP helper and post-handshake ownership check to the newly discovered executable.

A direct, approved Stable Update.exe recovery was observed to exit 0, start app-1.0.9261\Discord.exe with loopback CDP flags, and expose /json/version with HTTP 200. This is a command-line updater recovery, not live acceptance of the Orb GUI restart button or its native renderer handshake. No Quest/JavaScript payload was run.

Native C11 / Win32 controller with a packaged WinHTTP helper, external Unicode
JavaScript files, on-demand renderer verification, settings and explicit restart
consent. Extract the ZIP and keep both production executables together.

Requires Windows 10 version 1809+ or Windows 11 x64. No Node.js, Python, Electron, Visual
Studio or GCC runtime installation is needed by the native application.

The first verified account-free Windows Server 2025 CI runs passed native process
52/52, Foundation 37/37 and mock GUI 27/27 with 310 assertions:
https://github.com/0x440-1me/24510_0rb/actions/runs/38005830743
https://github.com/0x440-1me/24510_0rb/actions/runs/38005826992
Their downloaded build artifacts, all manifest assets and repeated ZIP hashes
were verified. Those builds used GCC 16.2.0 Rev5; they did not use real Discord.

The tagged workflow builds with MSYS2 UCRT64, repeats account-free tests and
verifies every package asset before its separate publish job. The release notes
identify the tagged source revision; build-manifest.json records the actual
source, compiler/SDK/binutils and PowerShell/.NET versions and binary hashes.
Different future toolchains/runner/compression runtimes may change bytes.

Prior permitted Windows 11 real renderer checks used a separate local GCC Rev4
build and only harmless handshake, evaluation, exceptions and Unicode file edits.
These are not real-CDP tests of the hosted Rev5 binaries. Restart sequencing/affirmative UI
were mock-tested. The user reports that a manual Windows 11 production restart
reopened Discord, then Connect succeeded and hello.js ran. This was not independently
observed and is not a live test of hosted/release binaries. Automated real Discord
restart and the development VM's public WindowsPS test wrapper remain blocked
without bypass. Physical DPI migration and injected OS/allocation faults are unverified.

Stop cannot undo JavaScript already evaluated. Restart may interrupt calls or
streams; graceful close can fail or time out. Cancel after shutdown may leave
Discord stopped. Process/renderer status records a last check, not continuous
monitoring. Use trusted payloads and an authorized loopback debugging setup only.

MIT covers the new native application/supporting files; original legacy material
has unchanged rights and is excluded from release packages. Unofficial project,
unaffiliated with Discord. Check the public ZIP against SHA256SUMS.txt after
download, then verify its manifest identifies the release's tagged source.
Publication/download-verification records are maintained at:
https://github.com/0x440-1me/24510_0rb/issues/2
https://github.com/0x440-1me/24510_0rb/releases
