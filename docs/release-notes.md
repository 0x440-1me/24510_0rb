Native C11 / Win32 controller with a packaged WinHTTP helper, external Unicode
JavaScript files, on-demand renderer verification, settings and explicit restart
consent. Extract the ZIP and keep both production executables together.

Requires Windows10 1809+ or Windows11 x64. No Node.js, Python, Electron, Visual
Studio or GCC runtime installation is needed by the native application.

The tagged workflow builds with MSYS2 UCRT64, runs account-free mock foundation,
native process and GUI fixtures, verifies every package asset and repeats ZIP
hashes under the same toolchain/runtime. See the build manifest for exact versions.
Different future toolchains/runner/compression runtimes may change bytes.

Prior permitted Windows11 real renderer checks used only harmless handshake,
evaluation, exceptions and Unicode file edits. Restart sequencing/affirmative UI
were mock-tested. Automated real Discord restart and the development VM's public
WindowsPS test wrapper remain blocked without bypass; user manual restart outcome
is pending. Physical DPI migration and injected OS/allocation faults are unverified.

Stop cannot undo JavaScript already evaluated. Restart may interrupt calls or
streams; graceful close can fail or time out. Cancel after shutdown may leave
Discord stopped. Process/renderer status records a last check, not continuous
monitoring. Use trusted payloads and an authorized loopback debugging setup only.

MIT covers the new native application/supporting files; original legacy material
has unchanged rights and is excluded from release packages. Unofficial project,
unaffiliated with Discord. SHA256SUMS.txt verifies the attached ZIP.
