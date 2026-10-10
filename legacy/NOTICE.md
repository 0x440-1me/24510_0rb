# Original Orb: separate attribution and permission

Original file: `dc_cmd_script.js` (unchanged). Historical author: **0x440_1me**,
GitHub account **0x440-1me**.

SHA-256: `ef4b9fd9146ddc4da80b4037158d12e12641d91e6aee4babfa9b976ef96205b5`

On 2026-10-10 the repository owner stated that they personally authored this
script and explicitly authorized including this unchanged file in the
24510_0rb downloadable Windows application. The authorization is recorded in
the local implementation request and
[shipping acceptance issue #4](https://github.com/0x440-1me/24510_0rb/issues/4).
Contribution history includes 6211d7d, 82dc57c, 35bf34c and f51031f under the
same GitHub account. This is owner attestation and contribution provenance,
not an independent audit of any third-party code or rights.

**The native application's MIT license does not cover this original script.**
This notice records permission to bundle it unchanged in this application's
package; it does not grant a new blanket license for modification, reuse or
other redistribution. Preserve attribution and original notices. Verify the
scope of any other distribution or third-party rights with the author before
expanding it. The original historical readme is also excluded from MIT and
is not included in the portable package.

## Experimental, explicit opt-in

The script is self-starting: evaluating it automatically enrolls/updates
Discord Quests and fakes activity using the authenticated client's internal
APIs. It may violate Discord rules and affect the account. Bundling is not
an endorsement, Discord authorization, or a claim of account safety.

The native app opens an import/review dialog and requires confirmation for
every run, bound to the exact file bytes and SHA-256. Startup, loading,
Connect, restart and canceled dialogs never execute it. Tests use harmless
synthetic payloads; the original is only hashed/copied, never executed in CI.

Request Orb stop calls the existing `window.stopQuestProgram()` hook if
available. This is cooperative: **Stop requested / state unknown** does not
guarantee that work stopped or that changes were undone. Cancel helper only
terminates the app's native helper, not renderer work.
