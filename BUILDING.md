# Building 24510_0rb

Build/test commands require a source checkout of the reviewed native revision.
The runtime release ZIP intentionally excludes source, build scripts and tests;
obtain the corresponding repository source before following these instructions.

## Toolchain

Use Windows x64 and MSYS2 **UCRT64** GCC, binutils, headers and CRT. Install the compiler toolchain through your approved MSYS2 installation; in its UCRT64 terminal:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain
```

From a Windows command prompt at the repository root:

```bat
scripts\build.bat
```

The script selects ORB_GCC first, then MSYS2_LOCATION\ucrt64\bin\gcc.exe, known installation locations, then PATH. An invalid explicit compiler fails. It verifies the x64 MinGW target and UCRT headers. C11, -Wall/-Wextra/-Wpedantic/-Werror, -O2, source prefix mapping and a zero PE linker timestamp are enabled.

## Explicit compiler and independent output

```bat
set "MSYS2_LOCATION=C:\msys64"
set "ORB_OUT_DIR=C:\orb-build"
scripts\build.bat
```

ORB_GCC may instead name the exact UCRT64 gcc.exe. ORB_OUT_DIR defaults to bin; set another directory while your app is open to avoid overwriting its running image. Both required production executables and harmless payloads are generated. Test/mock binaries are development outputs and are excluded from packages. The production app has no mock override.

## Tests

Tests require an already approved PowerShell environment that permits the repository scripts. No script changes execution policy or adds bypass flags. A policy refusal is a separate blocked result; do not retry it through an alternative launch route. The current development VM's public Windows PowerShell wrapper was refused; approved direct component evidence is documented separately.

Native component driver and mock fixture commands, using default bin output:

```powershell
.\bin\process-test.exe
& .\tests\foundation.ps1 -EvidenceDirectory C:\orb-evidence\foundation
& .\tests\gui.ps1 -EvidenceDirectory C:\orb-evidence\gui
```

No real account is needed for these fixtures. The GUI harness scopes interactions to its launched app PID/class and confirms restart only in its separately labeled mock build. Real Discord integration and real restart require separate permission; automation must respect execution guards. An independent ORB_OUT_DIR is for compilation/packaging; scripts/test.bat refuses a non-default output directory before building so it cannot test stale bin executables. Current component scripts expect bin unless supplied equivalent test paths.

## Packaging and reproducibility

Use an approved PowerShell environment with .NET ZipArchive support:

```powershell
& .\scripts\package.ps1 -BuildDirectory C:\orb-build -OutputDirectory C:\orb-package
```

Release packaging requires a clean source checkout. For private review of uncommitted changes only, add **-Development**; the manifest explicitly marks the dirty development source. Do not publish that package as a clean tagged release.

The package has an explicit file whitelist, ordinal entry ordering, fixed ZIP timestamp 2000-01-01T00:00:00Z and stable metadata. It includes both production executables, harmless payloads, usable README/supporting docs/images, scoped license and build-manifest.json. Mocks, tests, legacy files, preferences and private evidence are excluded. SHA256SUMS.txt checks the named ZIP.

Reproducibility is demonstrated only for identical source, compiler/SDK/binutils and PowerShell/.NET compression runtime. Their actual versions and binary hashes are recorded in the manifest. Different compiler, headers, future runner image or compression runtime can change bytes. A clean tagged source build is required for the eventual release.

## Hosted workflows

The source includes native Windows build and tag-release workflows with immutable action commit pins. The first verified [PR run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005830743) and [feature push run](https://github.com/0x440-1me/24510_0rb/actions/runs/38005826992) passed on Windows Server 2025. The windows-2025 runner installs UCRT64 GCC/binutils/headers/CRT, uses the setup action's actual installation location and builds with the same batch script. The shared ci.ps1 accepts only the Windows x64 Actions environment and default bin output; do not invoke it against a user-owned local running app.

Pull requests, main/native feature pushes and manual dispatch build and run the native mocked process, foundation and GUI fixtures in the hosted runner's approved pwsh environment. No Discord/account is used and restart affirmation occurs only in the compile-time mock app. This separate host does not retry the development VM's blocked WindowsPS wrapper.

Successful jobs package clean source outside the checkout, verify every asset, import dependencies, source/version/dirty flags and usable README links, repeat the ZIP hash, and upload only the release ZIP/checksum as a wrapped build artifact. Mock/build evidence is a separate artifact available on failures. Both first successful runs and their downloaded artifacts were verified: native process 52/52, Foundation 37/37 and mock GUI 27/27 with 310 assertions, all 14 manifest asset hashes and the exact 15-entry package whitelist.

Those runs recorded GCC 16.2.0 Rev5 (package 16.2.0-5), binutils 2.47-4, UCRT headers/CRT 14.0.0.r426.g4564ee4b5-1, PowerShell 7.6.6 and .NET 10.0.12. The PR package records its synthetic merge checkout SHA; the push package records the feature SHA. Equal source content and toolchain produced equal executable hashes, while the ZIP hashes differ because their source-revision metadata differs. Prior Windows 11 live checks used a local Rev4 compiler build. No binary identity across different hosts/toolchains, or real-CDP test of the hosted binaries, is claimed. See [test results](docs/test-results.md); actual versions are always recorded in each manifest.

Tag release accepts only an exact vMAJOR.MINOR.PATCH matching VERSION and the checked-out event SHA; that commit must belong to current origin/main history. A separate contents:write job downloads the verified two-file artifact with digest mismatch treated as an error, repeats checksum/provenance checks and creates a new release with controlled notes. Existing releases/tags/assets are never overwritten. After publication, download the public ZIP/checksum and verify its checksum, manifest source SHA and assets independently. Build-artifact verification does not establish that this publication/download procedure completed. Consult [current Actions](https://github.com/0x440-1me/24510_0rb/actions), [releases](https://github.com/0x440-1me/24510_0rb/releases) and [Issue #2](https://github.com/0x440-1me/24510_0rb/issues/2) for the corresponding run and verification records.

## Unreleased v0.3 additions

The build creates bin/mock/orb-cdp.exe and bin/mock/24510_0rb-ui-mock.exe
for account-free tests, separately from production images. The original
script is copied unchanged into bin/legacy with its permission/risk notice.
Only the production executables and allowlisted payload/notices are packaged.

Run bin/payload-test.exe, tests/v03.ps1, tests/foundation.ps1 and tests/gui.ps1
in an already permitted PowerShell environment. Fixtures support Windows
PowerShell 5.1 and PowerShell 7 using explicit synthetic WebSocket frames.
Never supply the original as an execution test input.

For private dirty-source review, package.ps1 -Development may write under
bin/review-packages/<new-directory>. verify-package.ps1 -AllowDevelopment
checks the same archive hashes/notices while accepting its explicit dirty
source/development flags. Default verification still requires clean-source
release provenance. VERSION remains 0.2.0; such a ZIP is not a v0.3 release.
