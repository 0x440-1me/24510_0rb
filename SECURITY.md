# Security and trust boundaries

24510_0rb is an unofficial local Discord renderer controller. CDP and JavaScript evaluation are privileged capabilities. Use only trusted external files and an authorized local setup.

- HTTP/renderer discovery is direct loopback with no proxy, redirects, automatic authentication or cookies. No browser Origin header or weakened origin setting is required.
- Approved exact HTTPS Discord origins and ws://127.0.0.1 on the configured port are required; lookalikes, foreign WebSockets, alternate ports, user information and query/fragment surprises are rejected.
- Restart requires explicit confirmation, exact approved installation identity and retained process handles. Foreign/wildcard port listeners, incomplete inventories and identity errors fail closed.
- Only ordinary window-close requests are used for Discord. There is no force-kill fallback. Stop terminates only the application's exclusively owned native helper job.
- Stop does not reverse JavaScript already evaluated, and cannot guarantee cancellation of its asynchronous renderer work. A cancelled/failed restart after shutdown can leave Discord stopped.
- No legacy Quest script is selected automatically, run in tests or included in release payloads. Legacy rights remain separate from the scoped MIT native license.

Preferences are saved under Local AppData and may contain your selected file paths. Logs may contain script results or errors. Do not publish private logs/preferences, tokens, account contents or private channel URLs. Public tests use controlled harmless fixtures; screenshots show only the app/dialog.

Report a security concern through the repository's GitHub security reporting feature if available. Otherwise open an issue with a minimal sanitized description and no credentials/private Discord data. There is no promised response SLA. Never circumvent a denied execution path while reproducing a report.
