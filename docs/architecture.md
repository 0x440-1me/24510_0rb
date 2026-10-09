# Native controller architecture

The native Win32 interface runs connection, execution and restart work on a worker thread. It uses `cdp_run` in `src/cdp.c` to launch the packaged native C helper `orb-cdp.exe` directly with an exact path. Both components use Windows APIs; neither needs Node, Python, Electron, or Visual Studio at runtime.

The helper reuses the verified C11 WinHTTP injector. It discovers only approved HTTPS Discord page origins through the configured loopback HTTP endpoint. Redirects, automatic authentication, cookies, and proxy use are disabled. Renderer WebSocket URLs must use `ws://127.0.0.1:<configured-port>/devtools/page/<identifier>`. The native handshake sends no browser Origin header and requires no weakened Chromium origin setting.

The approved canonical origins are `https://discord.com`, `https://discordapp.com`, `https://ptb.discord.com`, and `https://canary.discord.com`, with an implicit default HTTPS port. Titles, lookalike domains, user information, explicit alternate ports, and non-HTTPS renderer URLs cannot select a target. WebSocket identifiers contain only letters, digits, underscores, or hyphens; query strings and fragments are rejected.

`--connect` discovers the renderer, verifies its WebSocket handshake, then closes the connection without evaluating JavaScript. Status is an on-demand verification with a timestamp. It does not describe a persistent WebSocket after the helper exits.

The controller creates an exclusively owned Job Object, spawns the helper suspended, assigns it to that job before resuming, and captures bounded diagnostics through pipes. An explicit inherited-handle list prevents unrelated handle inheritance. Cancel and operation deadlines terminate only this owned helper job. Discord is never attached to the job. Blocking WinHTTP handles stay within the helper process; cancellation does not close them from another thread.

Stop cancels the managed controller operation. It cannot roll back JavaScript already transmitted or evaluated, and closing a WebSocket cannot guarantee that an arbitrary asynchronous script stops in the renderer.

The original Quest automation script remains a separate historical artifact. It is never a default payload or an automated test input. Included native payloads only return a marker or display a removable banner.

The application must not be used as a workaround for a rejected launch path.

## Consent and graceful Discord restart

The reusable restart runner in `discord_process.c` uses the same operation table for production and mocked tests. Without confirmation it performs no discovery, shutdown, or launch. Cancellation is checked before and after every stage and immediately before production window-close/process-create calls.

Discovery considers stable, PTB, and canary installations under the Windows Local AppData Discord roots. Running processes are selected by their exact image path; retained query/synchronize handles prevent accepting a reused PID. A single running installation takes priority; multiple running installations or multiple installed channels without a running selection require an explicit executable selection. The explicit executable must resolve to an approved installation. Version directories are compared numerically, without a hardcoded installed version. Inaccessible or incomplete process/directory inventories fail closed.

After confirmation the runner checks IPv4 and IPv6 listener ownership. The port may be free, or occupied only by loopback listeners owned by the exact selected installation. Foreign and wildcard listeners stop the flow before shutdown. Ordinary `WM_CLOSE` is sent only to windows whose current PID and exact image still match retained handles. No-window/tray conditions, incomplete enumeration, failed shutdown, or exit timeout stop the flow. Discord is never force-terminated.

After exit the port must be free. A fresh exact-installation inventory immediately before launch refuses remaining or respawned Discord processes. Direct `CreateProcessW` uses the selected executable and only `--remote-debugging-address=127.0.0.1` plus the configured port. No wrapper, origin-security change, or alternate launch fallback is used. As with other process inventory checks, external changes can occur after a check; failures stop subsequent stages, and the application does not retry destructive operations automatically.

Bounded managed helper probes wait for the renderer. Fresh exact-image process and loopback listener ownership are checked before each probe and again before reporting handshake success. A foreign process winning the port race cannot produce a successful restart result.

Cancellation or failure after shutdown was attempted may leave Discord stopped or partially closed. `shutdown_attempted` records this conservatively even if a later window-close fails; it is false for no-consent and pre-cancel paths. Cancellation after launch reports that Discord was launched and asks the user to check its process state; Discord can independently exit. Closing retained process handles never terminates Discord.

Real shutdown/launch/restart tests remain blocked pending an explicitly permitted execution path. Mocked restart success proves the shared state sequencing and failure handling; it does not prove a normal live Discord restart.

## GUI ownership and status

`24510_0rb.exe` provides Connect, Restart Discord, Load Script, Run Script, Stop, Logs and Settings. Startup selects module-relative `payloads/hello.js` without executing it. The original Quest script is never selected automatically. Load Script uses the native Unicode file picker, and execution rereads the external file on every Run Script action.

Dispatch copies the selected script, port, timeouts and executable selection into an immutable worker request. The worker calls the managed controller or shared restart runner and performs read-only process discovery after the operation. It never reads or writes window controls. It posts a heap-owned result to the main thread, which updates controls and frees it. A failed PostMessage frees the result and sets a failure flag; the main thread detects worker completion before cleanup. gui_run alone owns the GUI allocation, including creation failure; WM_NCDESTROY only clears the window association.

Discord process state is a **last-check snapshot**, refreshed at startup and after an operation. It is not continuous process monitoring. Endpoint/renderer status describes the most recent on-demand handshake, with a local success timestamp; the helper closes that socket. A JavaScript exception can still establish a successful renderer connection. Changing the port or executable selection invalidates prior endpoint verification and its associated success timestamp.

Conflicting controls are disabled while a worker runs. Stop signals its cancellation event; the controller terminates only its own helper job when required. The UI thread continues processing messages. Closing while busy requests cancellation and defers window destruction until the worker result or failed-result completion is handled. Stop and closing the application cannot undo a script already evaluated, and they cannot guarantee cancellation of asynchronous work already running in the renderer.

Restart opens a native confirmation dialog warning about interrupted calls, streams and other activities. Cancel is the default button; Escape and the window close action cancel. Only an explicit Restart Discord action in that dialog grants consent. Cancel starts no restart worker. Production GUI tests exercise only cancellation and harmless CDP integration; they do not automate affirmative production restart.

Settings use bounded port/timeouts and an approved explicit Discord image selection. Preferences live under Local AppData and are saved via a UTF-16LE BOM temporary INI followed by atomic replacement, preserving paths outside the ANSI code page. Numeric corruption or missing required numeric fields resets the application to defaults. Missing optional fields use their documented defaults; a malformed selected path is not automatically repaired and produces a visible execution/discovery error when used. Preferences and private evidence are not release assets.

The manifest declares DPI awareness, and WM_DPICHANGED rescales fonts, controls and the minimum window size. The native harness checks a synthetic 144-DPI notification and minimum layout; physical monitor migration has not been verified. The GUI uses GetDpiForWindow and targets modern Windows x64; only the current Windows 11 VM has live test evidence.

## Separate GUI test build

`24510_0rb-ui-mock.exe` is a separate build with an explicit MOCK RESTART TEST title and subtitle. ORB_UI_TEST_MOCK replaces only the restart operation table with harmless callbacks; the GUI and restart orchestrator are shared with production. Test-only ORB_UI_MOCK_FAIL_STAGE and ORB_UI_MOCK_HOLD_STAGE inputs select simulated failure/cancellation stages. Production has neither those inputs nor a hidden consent/restart test argument. Its exact-path helper boundary still handles ordinary connection/script operations.

The purpose-built harness scopes every window/control interaction to its freshly launched PID and exact product window class. It permits affirmative restart only for the distinct mock class. Captures include only the app or its own consent/settings dialog; tests never capture Discord or the desktop. Public fixtures require no credentials or real accounts. Mocked affirmative success, stage failures and Stop demonstrate UI wiring and diagnostics without live process-control claims.

## Component tests

`scripts/test.bat` builds the native components and runs isolated PowerShell fixtures. Tests require an approved PowerShell environment whose existing policy permits the local test scripts. The script never changes execution policy or adds a policy bypass. A policy refusal must be reported separately from component test results; do not work around it through another VM launch path.
