# Dopamine2-roothide

> **An experimental Dopamine fork for the RootHide architecture, maintained by LLOS Lord.** This project integrates ClearSword, DarkSword, Titan, and momentarius across device and iOS-version ranges while preserving the legacy iOS 15–16 execution path.

[![Build](https://github.com/LLOS-Lord/Dopamine2-roothide/actions/workflows/roothide.yml/badge.svg)](https://github.com/LLOS-Lord/Dopamine2-roothide/actions/workflows/roothide.yml)

Dopamine2-roothide is **not an official release of Dopamine or RootHide**. It is an experimental research and integration branch built from open-source Dopamine and RootHide components. Compatibility metadata, a successful compilation, or a completed application flow does not prove that every listed device can be jailbroken successfully in practice.

The active development branch for the high-iOS work is `ui/relaxin-terminal-redesign`. The legacy `2.x` branch is kept separate and is not modified by this work.

## Contents

- [Overview](#overview)
- [How Dopamine works](#how-dopamine-works)
- [RootHide architecture](#roothide-architecture)
- [Current support matrix](#current-support-matrix)
- [Exploit selection](#exploit-selection)
- [Recent compatibility work](#recent-compatibility-work)
- [Repository layout](#repository-layout)
- [Building on macOS](#building-on-macos)
- [Building with GitHub Actions](#building-with-github-actions)
- [Installation and testing](#installation-and-testing)
- [Diagnostics](#diagnostics)
- [Limitations and warnings](#limitations-and-warnings)
- [Contributing](#contributing)
- [License and references](#license-and-references)

## Overview

Dopamine is a semi-untethered jailbreak. After a device reboots, the user must open the application and run the jailbreak flow again to restore the jailbroken state. The project does not replace the device firmware with a custom iOS build. Instead, it selects an exploit appropriate for the device, establishes the required kernel primitives, applies the relevant patches, and starts the RootHide bootstrap in userspace.

This fork uses RootHide to keep the jailbreak bootstrap and its applications outside the real system root. Paths, frameworks, daemons, and package-manager resources follow the RootHide/rootless model rather than writing directly into protected system locations. This reduces the scope of persistent system changes, but it does not eliminate the risk of kernel corruption, userspace crashes, boot loops, or data loss.

| Project objective | Implementation direction |
|---|---|
| Preserve the established iOS 15–16 path | Keep the legacy allocator and exploit routing; do not enable the iOS 17 IOSurface/Titan path on iOS 16. |
| Add high-iOS kernel routing | Use device- and version-specific ClearSword/DarkSword flavors instead of treating every iOS version as one exploit target. |
| Experiment with A12/A13 iOS 17–18 | Use the `a12-a13-ios17` and `ios-18` metadata routes together with the corresponding momentarius/PPL path. This remains experimental until confirmed on the exact device and build. |
| Keep exploit selection explicit | Settings can expose Kernel, PAC, and PPL choices when compatible metadata is available. |
| Avoid privileged actions before jailbreak | Respring, userspace reboot, and device reboot are enabled only after the environment manager confirms a jailbroken state. |

## How Dopamine works

The application divides the jailbreak procedure into layers. Each layer has a separate responsibility and should run only after the previous layer has produced the primitives or state required by the next one.

### 1. Environment detection

`DOEnvironmentManager` reads the iOS version, CPU architecture, jailbreak state, bootstrap state, and compatibility conditions. The selection process does not rely only on the marketing device name. Each exploit has its own metadata, including exploit type, flavor, supported versions, supported devices, and optional build-inclusion or build-exclusion rules.

`DOExploitManager` scans the exploit frameworks packaged in `Dopamine.app/Frameworks`, reads their metadata, and filters the available choices for the current device. A single application may therefore contain several frameworks while selecting only one compatible route at runtime.

### 2. Kernel exploit

`DOJailbreaker` loads the selected kernel exploit through the native framework interface and invokes its entry point. The exploit establishes the kernel read/write or kernel-call primitives needed by the subsequent stages.

The high-iOS routes are intentionally separated. The `default` flavor is used for the established iOS 15–16 path. The `high-ios-17` flavor is intended for the high-iOS A14–A17 route where its metadata matches. The `a12-a13-ios17` flavor is used for the experimental A12/A13 iOS 17 route, while `ios-18` is used for the experimental A12/A13 iOS 18 route.

### 3. Kernel information and allocators

After the kernel exploit succeeds, Dopamine initializes kernel information, translation data, and the relevant primitive allocator. This stage is highly sensitive to the exact Darwin/XNU build.

| iOS range | Allocator direction in this branch |
|---|---|
| iOS 15 | Use the legacy primitive path selected by the compatible exploit metadata. |
| iOS 16 | Preserve the established `kalloc_pt`/page-table path where required; do not enable the iOS 17-only IOSurface allocator route. |
| iOS 17 | Use the IOSurface allocator only on the high-iOS route that requires it, with the matching IOSurface offsets. |
| iOS 18 | Use the A12/A13 high-iOS route and matching offsets; this remains device/build dependent and is not a universal support guarantee. |

Allocator separation is mandatory. Enabling an iOS 17 allocator on iOS 16, or using an offset from a different Darwin build, can produce invalid kernel addresses and an early reboot.

### 4. PAC and PPL bypass

PAC and PPL are different protection layers. PAC concerns pointer authentication on arm64e systems. PPL is a privileged kernel protection layer that must be bypassed by the appropriate PPL exploit on routes that require it. They should not be treated as interchangeable exploit types.

The Settings interface may expose the following groups:

| Group | Purpose |
|---|---|
| Kernel exploit | Select ClearSword, DarkSword, or another compatible kernel exploit. |
| PAC bypass | Select a compatible PAC bypass when required by the architecture and iOS build. |
| PPL bypass | Select Titan, dmaFail, momentarius, or another compatible PPL exploit. |

Displaying an exploit in Settings does not make it compatible. Compatibility still depends on metadata, device model, Darwin build, offsets, and the runtime primitive state.

### 5. Physical read/write and environment patches

Once the kernel and PPL/PAC primitives are ready, Dopamine initializes physical read/write support, applies environment patches, and prepares the userspace jailbreak components. The main implementation is distributed across `BaseBin/libjailbreak`, the boomerang handoff, `jbctl`, launchdhook, and the application bootstrap code.

### 6. RootHide bootstrap

The bootstrap follows the RootHide model. Dopamine starts the required daemons and services, registers or injects the necessary trust information, and refreshes the jailbreak applications and package-manager state. Sileo, Zebra, and RootHide applications should be considered usable only after bootstrap and userspace handoff complete successfully.

A final message in the application is not sufficient evidence of success. The practical test is whether the package manager opens, the jailbreak services remain alive after respring or userspace reboot, and the environment can be checked again after a fresh application launch.

### 7. Post-jailbreak actions

The action menu contains Settings, Respring, Reboot Userspace, Reboot Device, and Credits. Privileged reboot actions are intentionally disabled until the environment manager confirms that the device is jailbroken and the required userspace state is available.

## RootHide architecture

The repository is divided into the following major layers:

```text
Dopamine2-roothide/
├── Application/
│   ├── Dopamine.xcodeproj/          # Xcode project and framework targets
│   ├── Dopamine/
│   │   ├── Jailbreak/               # Environment and jailbreak orchestration
│   │   ├── UI/                      # Main menu, Settings, and selectors
│   │   ├── Exploits/                # ClearSword, DarkSword, Titan, momentarius
│   │   ├── Prebuilt/Frameworks/     # Packaged framework resources
│   │   └── Resources/               # Bootstrap and package resources
│   └── Makefile                     # Application and framework build wiring
├── BaseBin/
│   ├── libjailbreak/                # Kernel primitives and patch layer
│   ├── opainject/                   # launchd/library injection helper
│   ├── jbctl/                       # Jailbreak and userspace control utility
│   ├── launchdhook/                 # launchd-side RootHide service
│   └── XPF/                         # Patchfinding and kernel information
├── Packages/                        # Bootstrap packages
├── .github/workflows/roothide.yml   # GitHub Actions build workflow
├── BUILD.md                         # Additional build notes
├── README.md                        # Vietnamese project documentation
└── README.en.md                     # This English translation
```

The project builds the momentarius framework from the repository source through the application build wiring rather than relying only on an unrelated stale prebuilt binary. Titan remains restricted by its exploit metadata and should not be selected for iOS 16.

## Current support matrix

The following table describes the current metadata and code-path scope. It is not a guarantee that every listed device and build can be jailbroken successfully. Always record the exact model identifier, iOS build number, Darwin version, selected exploit, and commit under test.

| iOS range | Device scope | Kernel route | PAC/PPL route | Status |
|---|---|---|---|---|
| iOS 15.0–15.8.7 | Devices matching legacy exploit metadata | ClearSword/DarkSword and other compatible legacy routes | Compatible legacy PAC/PPL route where available | Established project path; verify exact build |
| iOS 16.0–16.7.16 | Devices matching legacy metadata | ClearSword/DarkSword legacy route | `dmaFail` only where metadata allows; manual PPL selection is experimental | Preserve the iOS 16 allocator path |
| iOS 17.0–17.3.1 | A14–A17 route | ClearSword/DarkSword `high-ios-17` | Titan | CI/build support; real-device confirmation required |
| iOS 17.0–17.7.1 | A12–A13 | ClearSword/DarkSword `a12-a13-ios17` | momentarius | Experimental routing and offsets; real-device confirmation required |
| iOS 18.0–18.7.1 | A12–A13 | ClearSword/DarkSword `ios-18` | momentarius | Experimental RootHide port; not universal confirmation |
| iOS 26.0–26.0.1 | A12–A13 metadata route | Reference `ios-18` metadata | momentarius | Experimental metadata only; not verified here |

There is no evidence in this repository that A14–A17 are supported on iOS 17.4 or later, or on iOS 18+, by merely selecting a metadata flavor. A new version range requires matching exploit behavior, kernel layout, allocator, patchfinding, PAC/PPL/SPTM handling, physical read/write, and bootstrap validation.

## Exploit selection

Settings exposes exploit groups only when the device is detected as compatible and the corresponding framework is available. A practical test sequence is:

1. Open **Settings → Kernel Exploit** and select the compatible ClearSword or DarkSword route.
2. Select a compatible PAC bypass when the device and build require one, or keep the default when no manual choice is available.
3. Open **PPL Bypass**. On iOS 16, the selector may be shown for controlled testing; do not interpret visibility as proof of compatibility.
4. Return to the main screen and start Jailbreak.
5. Record the actual logs for the kernel exploit, PAC/PPL bypass, physical read/write, trust injection, bootstrap, and userspace handoff.

If an exploit does not appear, check the framework packaging, `Info.plist` metadata, device identifier, iOS version, and build exclusions. Do not select an exploit only because its name exists in the source tree.

## Recent compatibility work

The current high-iOS branch contains the following categories of compatibility work:

| Area | Current direction |
|---|---|
| Trustcache safety | Same-sized file replacement writes only the file body; newly allocated trustcache headers are explicitly zeroed; SPTM/TXM insertion uses a tail path with null-head guards. |
| IOSurface mapping | iOS 17+ avoids the legacy `vm_remap` of the physical IOSurface mapping; iOS 15/16 retain the legacy behavior. |
| IOSurface offsets | `ranges`/`rangeCount` offsets are selected for newer Darwin versions, including the iOS 17.1+ RootHide reference values. |
| launchd handoff | `stock_fixes` is included in the relevant callers; launchd stash failure stops before `opainject`; the remote launchd init-port array is not read and mutated on the newer route. |
| Crash reporting | iOS 17+ keeps signal/Objective-C reporting but skips the `EXCEPTION_DEFAULT` Mach exception-port path. |
| Framework build | The momentarius framework is built from source through the Xcode/Makefile target before application packaging. |

These changes reduce known failure modes but do not prove a complete jailbreak on every device. A panic that still reports `kalloc.type1.48` or another zone-bound failure requires a new panic-full from the exact test run.

## Building on macOS

A local build requires macOS with a compatible Xcode version and the project tools used by the workflow, including `xcodebuild`, `make`, `ldid`, `zip`, and the iOS SDK.

```bash
git clone https://github.com/LLOS-Lord/Dopamine2-roothide.git
cd Dopamine2-roothide
git checkout ui/relaxin-terminal-redesign
cd Application
make clean
make
```

The Makefile builds the Dopamine application and the configured framework targets, including the source-built momentarius target on the current branch. Review the build output and fail-fast checks before using any resulting package.

For a nightly-style local build with an explicit commit label:

```bash
cd Application
NIGHTLY=1 COMMIT_HASH="$(git rev-parse --short HEAD)" make
```

## Building with GitHub Actions

The workflow is located at [`.github/workflows/roothide.yml`](.github/workflows/roothide.yml). A fork can run the workflow from the **Actions** tab on the desired branch. The workflow builds the application and publishes a TIPA artifact for the run.

For reproducibility, record the commit SHA, workflow run ID, Xcode/SDK version shown in the log, and the device/iOS combination used for testing. Do not assume that an artifact built successfully has been validated on real hardware.

## Installation and testing

Dopamine2-roothide is packaged as a TIPA for an installation method that the user understands and trusts. A normal webpage or Safari page cannot turn Objective-C, C, and assembly source into a jailbreak without a separate WebKit exploit chain.

A cautious test procedure is:

1. Back up the device and confirm the exact model, iOS version, build number, and commit under test.
2. Install the TIPA through a trusted signing or installation method.
3. Open Dopamine and verify which exploit frameworks are detected in Settings.
4. Select only a route that matches the device metadata and test matrix.
5. Preserve the full log and record the last completed stage before any reboot.
6. After apparent success, verify Sileo, Zebra, or RootHide, then test respring and userspace reboot separately.

The message “Bootstrap Successful” alone is not sufficient. A usable installation must survive the relevant userspace transition and retain the expected bootstrap state.

## Diagnostics

When testing a new build, do not change the kernel exploit, allocator, metadata, and UI configuration at the same time. Record one controlled variable at a time.

| Symptom | First areas to inspect |
|---|---|
| Exploit is missing from Settings | Framework packaging, `Info.plist`, supported-device filter, iOS/build range, or stale application contents |
| Reboot during Trust Cache Injection | `panicString`, zone name, IOSurface offsets, allocator path, trustcache body size, and whether `roots_installed` is zero |
| `kalloc.type1.48` or `kalloc.type4.48` zone-bound panic | IOSurface physical mapping and kalloc primitive before changing trustcache list offsets again |
| `invalid kaddr` on iOS 16 | Accidental activation of the iOS 17 allocator or a mismatched translation/offset table |
| `recover primitives (error -2)` | launchd registered-port stash, `stock_fixes`, remote init-port lookup, and whether `opainject` ran after stash failure |
| `exec_cmd_trusted` or `prep_bootstrap.sh` failure | bootstrap trust path, local trust fallback, trust-file attach schema, and first-pass launchd check-in |
| Crashreporter or launchd `EXC_GUARD` | iOS 17+ Mach exception-port registration and `EXCEPTION_DEFAULT` behavior |
| Sileo/Zebra does not appear | bootstrap completion, trust injection, uicache, userspace handoff, and package-manager state |
| Respring/reboot action is disabled | The environment manager has not confirmed a jailbroken state; this is intentional UI behavior |

A useful panic report must come from the same test that failed. Include the first header line, `os_version`, `product`, `socId`, Darwin/XNU version, `panicString`, `Panicked task`, backtrace, `roots_installed`, and the exact commit. A filename reused from an earlier panic can lead to an incorrect diagnosis.

## Limitations and warnings

This is experimental system software. A jailbreak can cause a boot loop, loss of jailbreak state, userspace crashes, daemon failures, touch/input problems, or a restore requirement. Back up important data before testing and avoid using a single production device as the only test device.

Metadata is a filter, not a guarantee. In particular, A12/A13 iOS 17–18 support in this branch is an experimental port that still depends on the exact Darwin build, exploit behavior, IOSurface mapping, physical read/write, PPL bypass, and bootstrap state. Titan must not be used as the iOS 16 allocator/PPL substitute unless the metadata and runtime path explicitly support it.

The project cannot jailbreak a device directly from an ordinary webpage. Safari/WebKit does not grant JavaScript direct access to kernel, IOKit, or private Mach APIs. A real browser-based jailbreak would require an independent WebKit exploit chain.

Do not commit GitHub tokens, signing certificates, private keys, or other credentials. If a token was ever exposed in a command, URL, log, or chat, revoke it and create a replacement with the minimum required scope.

## Contributing

Keep exploit, allocator, trustcache, launchd, and UI changes in separate commits whenever possible. Every port from an upstream repository should record its source repository, source commit, and the adaptation made for RootHide.

Do not expand an iOS range by editing metadata alone. A new range needs the corresponding kernel layout, patchfinding, allocator, PAC/PPL/SPTM handling, physical read/write, launchd handoff, trust injection, and bootstrap validation. CI compilation is necessary but not sufficient; the exact device and build must be tested separately.

The `2.x` branch is intentionally kept separate from the experimental high-iOS UI branch. Contributors should verify the current branch before committing and should not force-push over the stable branch.

## License and references

The project follows the licenses and conditions of its upstream components. Read [`LICENSE.md`](LICENSE.md) before distributing a package or creating a derivative fork.

- [Official Dopamine](https://github.com/opa334/Dopamine)
- [P013onEr RootHide reference](https://github.com/P013onEr/RootHide)
- [RootHide fork by 154328106](https://github.com/154328106/RootHide)
- [RootHide fork by lishaowen1314](https://github.com/lishaowen1314/RootHide)
- [RootHide fork by 7xj1998-cell](https://github.com/7xj1998-cell/RootHide)
- [RootHide fork by riboly](https://github.com/riboly/RootHide)
- [ClearSword](https://github.com/TheRealClarity/ClearSword)
- [ClearSword technical write-up](https://therealclarity.github.io/blog/clearsword/)
- [Dopamine2-roothide](https://github.com/LLOS-Lord/Dopamine2-roothide)
