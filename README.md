# 24510_0rb

**Native Windows controller for local Discord JavaScript experiments.**

24510_0rb is a C11 / Win32 application that connects to a local Discord renderer through CDP, loads an external JavaScript file and shows its result. It packages the proven native WinHTTP injector as a small companion executable. No Node.js, Python, Electron or Visual Studio runtime is required.

## Features

- Native interface with Connect, Restart Discord, Load Script, Run Script, Stop, Logs and Settings.
- Unicode payload selection; each run reads the current file contents.
- Configurable loopback port, bounded operation timeouts and readable errors.
- Renderer handshake verification with a timestamp, and Discord process state from the last check.
- Explicit restart consent, exact installed-image selection and graceful shutdown failure handling.
- Responsive workers, bounded logs and cancellation of the application's own helper.

![Native controller](docs/images/app.png)

*Validation screenshot. It predates the refined “Discord (last check)” label; process state is a snapshot, not continuous monitoring.*

## Download and run

**v0.2.0 is being prepared; CI and tag-release workflows are present in source but have not run on GitHub yet, and no native release is published.** See the [release page](https://github.com/0x440-1me/24510_0rb/releases). After the first verified release is published, the [latest release](https://github.com/0x440-1me/24510_0rb/releases/latest) will provide:

- `24510_0rb-v0.2.0-windows-x64.zip`
- `SHA256SUMS.txt`

Check the ZIP's SHA-256 against SHA256SUMS.txt, extract it into a writable folder, and run **24510_0rb.exe**. Keep **orb-cdp.exe** beside it and retain the payloads folder. The application selects hello.js at startup and does not run it automatically.

### Requirements

- Windows 10 version 1809 or later, or Windows 11, **x64**. Only the current Windows 11 development VM has live test evidence.
- An installed Discord stable/PTB/canary client and an authorized local debugging endpoint.
- No elevated permission is normally needed. The app requests asInvoker execution.

Build from source with MSYS2 UCRT64 GCC: see [BUILDING.md](BUILDING.md). Hosted CI runs account-free fixtures; a matching version tag builds and verifies a clean package before its separate publish job. These workflows still require their first hosted execution and review.

## Connect

1. Set the local debugging port in Settings (default **9223**).
2. Ensure Discord is exposing an endpoint through an authorized setup or use the consent-based restart feature where permitted.
3. Choose **Connect**. This discovers an approved Discord page, verifies its WebSocket handshake and closes the socket.

“Verified on demand” records a successful check; it does not mean the app holds an ongoing WebSocket. Changing the port or Discord selection clears old verification. Discord's process display is also a last-check snapshot.

## Restart with consent

![Restart consent dialog](docs/images/restart.png)

**Restart Discord** opens a warning about interrupted calls, streams and other activities. **Cancel is the default**; Cancel, Escape and the dialog close button leave Discord untouched. Only the affirmative button starts the restart worker.

The app selects an approved installation, checks IPv4/IPv6 port ownership, requests ordinary window close, waits for exit, rechecks the port and launches the selected executable with loopback-only debugging arguments. It never force-kills Discord. Multiple installed/running channels may require an explicit approved executable in Settings.

Tray-only clients, a close-to-tray setting, incomplete discovery, port conflicts and shutdown timeouts can stop the operation. A failure after shutdown may leave Discord stopped; cancellation after launch reports that launch occurred and asks you to check its process state. **Automated live restart is blocked in the development environment; the shared flow and GUI are mock-tested, and a user manual result is pending.** Do not use the application to circumvent an execution guard.

## External payloads

Choose **Load Script**, select a trusted `.js` file, then choose **Run Script**. Edit that file and run again to evaluate its current contents. A harmless example is:

```javascript
(() => {
  console.log("Hello from 24510_0rb");
  return "HELLO_OK";
})()
```

Included hello.js returns a marker; banner.js displays a removable banner. Scripts are evaluated in the Discord renderer and can affect its state. Run only code you understand and have permission to execute. **Stop cannot undo code already evaluated or reliably stop asynchronous work already running inside Discord.**

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Endpoint unavailable | Check the configured port and authorized Discord debugging setup. |
| Renderer not found | Confirm the endpoint belongs to a supported Discord page; lookalike titles/domains are rejected. |
| Native controller could not start | Keep orb-cdp.exe in the extracted app folder. |
| JavaScript exception | Read Logs, correct the external file, then run it again. |
| Port conflict | Choose a free local port; foreign or wildcard listeners cannot be reused for restart. |
| Graceful exit timeout / no window | Quit Discord manually through its own controls where authorized, then retry an authorized setup. No force-kill fallback exists. |
| Installation ambiguous | Select an approved installed Discord executable in Settings. |
| Invalid preferences | Invalid/missing numeric fields fall back to defaults. Repair invalid optional paths in Settings. |
| Test script refused by policy | Use an already approved PowerShell environment or report the refusal. Test scripts never change or bypass policy. |

## Security, status and limitations

This is an **unofficial** Discord integration, unaffiliated with Discord. Internal renderer/CDP behavior can change. Local debugging exposes privileged script access; keep it on loopback and do not run untrusted payloads. This project does not test Quest spoofing, account automation or anti-abuse evasion. See [SECURITY.md](SECURITY.md), [test results](docs/test-results.md) and [architecture](docs/architecture.md).

Validated behavior includes harmless real renderer handshake/evaluation/exceptions/Unicode file edits, mock restart sequencing/UI, consent cancellation, responsive Stop and cleanup. Physical DPI monitor migration, injected allocation/window/result-post failures and normal live restart remain unverified. Stop may leave already-evaluated effects in place. A worker busy during window close is cancelled before the app finishes closing.

## Legacy and license

The original author's script and documentation are preserved byte-for-byte in the repository's **legacy/** directory, with historical attribution **0x440_1me**. They are reference artifacts, excluded from release payloads and never automatically executed. The legacy script contains unofficial Quest spoofing; its presence does not make it a supported app feature.

[LICENSE](LICENSE) grants MIT terms only for the new native application and supporting files, including the supplied reused C core. Original legacy files and material with unresolved upstream rights are explicitly excluded. See [CHANGELOG.md](CHANGELOG.md).
