# Validation status for native 0.2.0

These are recorded local development results, not hosted CI/release claims.
Only a Windows11 x64 VM was live-tested. Windows10 1809+ is the declared minimum.

| Class | Recorded result |
| --- | --- |
| Native helper/controller | 34/34 foundation cases; harmless real Discord14/14; bounded cancel/timeout and native handle plateau checks passed |
| Discovery/shared restart foundation | 52/52 native mocked-state/socket ownership; real read-only5/5; unchanged process/sentinel evidence |
| Mock/local GUI | 27/27 cases,280 native assertions; Unicode picker/persistence, failure/cancel stages, responsive stalled Stop/close |
| Production GUI harmless live/local | 14/14 cases,130 assertions; handshake, hello, same-window repeats, exception, Unicode file edits, Cancel/Escape/X and cleanup |
| Final wording/snapshot correction | Affected mock cancellation/snapshot17/17; production mock-exclusion audit14/14 |

The saved production GUI suite predates the final snapshot-label wording change;
that change was checked with the affected mock case without repeating live runs.
Initial failures were retained, diagnosed and corrected before these final counts.
No Quest/account-affecting JavaScript was tested.

## Blocked or not verified

- Automated actual Discord restart/shutdown/launch: blocked by the execution guard;
  mocked success does not prove live restart. User manual outcome is pending.
- Public Windows PowerShell test wrapper: policy refused it. No bypass or alternate
  launch retry was used. Approved direct component evidence is separate.
- Physical high-DPI monitor migration: not verified; synthetic144-DPI handling was.
- Allocation/CreateWindow/PostMessage failure injection and OS query/ACL faults:
  not runtime-injected; corresponding ownership/fail-closed source was reviewed.
- Native CI/tag-release workflows are present in source and statically reviewed;
  hosted build/GUI fixtures, artifact transfer, release publication and downloaded
  asset verification remain pending. The account-free PID comparison correction
  in the GUI test driver follows the recorded local GUI pass; hosted CI will
  validate that environment without repeating the saved live suite.

Private development-VM evidence, personal paths, Discord process IDs and account
data are excluded from public artifacts. Hosted CI evidence contains account-free
mock/build logs, including its own runner paths and fixture process IDs.
