# Baby and Fab4 build support

## Audit and scope

The existing `scripts/build-arm.sh` compiles six static executables directly with the ARMv5 Buildroot musl SDK. CMake provides host regression tests. Native dependencies are libcurl, wolfSSL 5.8.2, Mongoose (vendored), zlib, libpng and libjpeg; NanoSVG is vendored. Existing TLS configuration and protocol implementations are retained. No new daemon or polling loop was added.

Native services, Lua integrations, configuration schema, API ports and time synchronization share one source tree. Configuration actually lives in `/mnt/storage/standalonebase`; `/mnt/sbdata` is optional Baby extended applet storage, not a prerequisite for configuration. The Baby kernel module, UBI image, recovery and firmware updater are hardware-specific. Fab4 packages omit the module and image; the applet skips automatic storage recovery and firmware update registration on Fab4. Removable media is never selected automatically.

Virtual Radio already queries Linux framebuffer ioctls, but its UI/input model assumes Radio hardware. Fab4 capture returns a useful unsupported error while other HTTP APIs remain available. Full Touch display/input support remains separate work.

## Build

Run from this directory in WSL/Linux or Git Bash (Git Bash automatically forwards these entry points to WSL):

```sh
sh scripts/build-arm.sh baby
sh scripts/build-arm.sh fab4
sh scripts/build-arm.sh all
sh packaging/build-package.sh all
```

The default SDK remains `../SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2` (or `/mnt/c/Projects/SBHttpsProxy` when present). Existing `ARM_TOOLCHAIN_DIR`, `ARM_TLS_PREFIX`, `ARM_WOLFSSL_PREFIX`, `ARM_IMAGE_PREFIX` overrides remain supported. Prepare dependencies using the existing image/TLS build scripts. Python 3 and ELF binutils are required for packaging. PowerShell `packaging/build-package.ps1 -Target all` delegates to the same WSL packager.

Both targets use `arm-buildroot-linux-musleabi-gcc`, static linking, ARM mode and soft-float ABI. Baby preserves `-march=armv5te -mtune=arm926ej-s`. Fab4 uses `-march=armv6 -mtune=arm1136jf-s`. Shared static dependencies retain their proven ARMv5 baseline; these instructions are compatible with ARMv6. Neither hard-float nor NEON is enabled. CMake's `SB_TARGET` controls feature compilation only; use the shell entry point for cross-compilation flags.

Outputs are `build-arm/baby/` and `build-arm/fab4/`. Each has all six executables and `build-manifest.txt` recording compiler version, flags and compiler/library hashes. Dependency sources and versions follow the existing preparation scripts; the external SDK and prebuilt libraries must be supplied. The manifest records the exact inputs used, rather than claiming a hermetic dependency bootstrap.

Packages are `build-package/StandaloneBase-0.7.0-{baby,fab4}.zip`, with sibling XML and JSON manifests. Archives use the existing flat Applet Installer layout, Unix executable permissions, board marker and exact target metadata. A mismatched package refuses applet startup. Set `SB_RELEASE_BASE_URL` to generate absolute release URLs; without it XML uses relative archive names for local review. Generated XML fragments do not replace the existing catalog or other applet entries. Merge entries into the deployment catalog when publishing is separately authorized. Packaging never deploys or publishes. Build outputs are isolated and no clean operation removes other builds.

Install later through the firmware Applet Installer with the matching target entry. No installation was performed during this task. Preserve the existing Baby extended-storage install procedure. Fab4 uses internal `/mnt/storage`; USB/SD attachment is unnecessary.

## Physical-device evidence (2026-10-31)

Read-only SSH inspection of the supplied Touch confirmed:

- Hardware: `Logitech Fab4 Board`, ARM1136 CPU part `0xb36`, architecture `6TEJ`, VFP.
- Kernel: `2.6.26.8-rt16-332-g5849bfa`; firmware `7.7.3 r16676`.
- RAM: 125848 kB; writable UBIFS `/mnt/storage`; no `/mnt/sbdata` or removable mount present.
- Both framebuffer devices report 480x544 virtual pixels and 32 bpp. Visible geometry/stride/bitfields require ioctl verification before enabling capture.
- No TinySC process or listeners on TCP 9000, TCP/UDP 3483 at inspection time.
- `/etc/init.d/squeezecenter` manages TinySC, scanner and artwork resizer. Its `stop` and `start` actions are reversible firmware controls; they were inspected, never executed. Prefer the device's server settings to stop TinySC if needed. Never disable its init hooks globally.

The startup guard checks IPv4/IPv6 TCP listeners and UDP bindings before any Fab4 applet initialization side effects. Conflicts log protocol/port and advise checking TinySC. `scripts/device-diagnostics.sh` lists matching processes and socket descriptors to correlate socket inodes with `/proc/net/*` when `netstat -p` is unavailable. No service is killed, and ports remain unchanged. A bind race remains handled by the native service's existing bind failure.

Read-only diagnostics can be streamed over SSH (`ssh root@DEVICE sh -s < scripts/device-diagnostics.sh`). This old firmware requires explicitly scoped legacy SSH negotiation settings in modern clients. Do not store its password in repository scripts. Writable storage is checked through permissions and mount flags without creating a probe file.

## Verification and remaining work

`verify-arm.sh` checks all six files for ARM EABI5, target CPU attributes, executable permissions, absence of interpreter/shared dependencies and unresolved global symbols. Packaging validates ELF files, permissions and board marker. Lua tests cover exact board identifiers, unknown boards, IPv4/IPv6 port conflicts and ignoring established TCP connections. Existing host tests cover protocols, CometD, Now Playing, framebuffer conversion, configuration, storage, HTTP and applet integrations.

| Component | Build Baby/Fab4 | Static ELF/ABI Baby/Fab4 | Baby hardware this task | Fab4 hardware this task |
|---|---|---|---|---|
| sbbase | verified | verified | not tested | not executed |
| sbproxy | verified | verified | not tested | not executed |
| sbwebserver | verified | verified | not tested | not executed |
| svgpngrenderer | verified | verified | not tested | not executed |
| sb-storage-helper/updater | verified | verified | not tested | not executed; flash operations unsupported |
| Lua applet | common source, Lua syntax/unit checks | not applicable | not tested | not loaded |
| Device diagnostics | shell | not applicable | not tested | executed read-only |
| Virtual display | Baby retained; Fab4 disabled | covered by native build | not tested | geometry inspected only |

Cross-compilation does not prove old-kernel syscall compatibility. The pre-existing musl compatibility configuration is retained, but neither ARMv5 baseline binaries nor new ARMv6 binaries were uploaded/executed on this Touch because the task forbids deployment and service changes. Later authorized device validation must cover startup/shutdown, HTTP/HTTPS streams and artwork, SlimProto/discovery, CometD sessions, Now Playing, logs, memory/idle CPU, time synchronization, atomic config persistence and Jive lifecycle. Reboot persistence and TinySC coexistence need a separate authorized hardware session. Baby's prior reported compatibility is not counted as a new hardware test here.

## Recorded results

Both target builds and both ZIP validations passed with GCC 8.4.0 (Buildroot 2020.02-00011-g7ea8a52). Host configure/build used bundled CMake 3.31.8, system GCC and `-DBUILD_SBPROXY=OFF`; proxy itself was cross-built for both targets. All 57 CTest tests passed (129.80 seconds). The expanded platform/socket fixture test was rerun and passed after its final edit. Lua 5.1 syntax checks passed. Existing compiler warnings about format truncation and third-party signedness remain; builds succeeded.

Modified files: `CMakeLists.txt`, `scripts/build-arm.sh`, `scripts/verify-arm.sh` (new), `scripts/device-diagnostics.sh` (new), `packaging/build-package.sh`, `packaging/build-package.ps1`, `packaging/target-metadata.py` (new), `applet/Platform.lua` (new), `applet/StandaloneBaseApplet.lua`, `native/sbwebserver/src/remote_framebuffer.c`, `tests/CMakeLists.txt`, `tests/test_platform.lua` (new), and this document. Existing unrelated working-tree content was preserved. No commit, push, publication or device deployment occurred.

Git Bash cannot execute the Linux SDK directly. The build and package entry points detect Git Bash and invoke the same scripts through `wsl.exe`, converting the script path with `cygpath` and `wslpath`. Custom Linux toolchain/dependency overrides should be set inside WSL.

## Authorized Fab4 deployment (2026-10-31)

Following explicit deployment authorization, installed `StandaloneBase-0.6.7-fab4.zip` on the supplied Touch at `192.168.1.244`. This was a fresh installation. Saved affected Jive settings and artwork Lua sources locally in `.local/fab4-before-20261031-184255.tar.gz` before installation. Used the merged `/usr/share/jive/applets/StandaloneBase` destination, applied the existing artwork helper, and restarted SqueezePlay through the firmware init script. No firmware, flash layout, bootloader, TinySC state or VPS changes were made.

Physical Fab4 checks passed: applet registration/loading, all three native services running, sbbase `/health` response, successful Jive CometD handshake/subscriptions, management homepage HTTP 200, protected API HTTP 401 without authentication, proxy HTTPS catalog fetch HTTP 200, and time synchronization from the initial January 1 device time to the current date. Installed native/app Lua checksums match the archive. Temporary upload staging was removed. Web access: `http://192.168.1.244/`.

This updates the earlier matrix: sbbase, sbproxy, sbwebserver and the Lua applet now have basic physical Fab4 runtime verification. Storage helper/updater and renderer execution, actual audio playback, complete Now Playing integration, UI interactions and reboot persistence remain unverified. The server's existing player metadata still labels the device `baby`/`Squeezebox Radio`; the observed CometD handshake correctly reports `machine:fab4`, and correcting that descriptive metadata remains follow-up work. Fab4 virtual framebuffer capture remains disabled.

## Virtual Touch update

Fab4 virtual capture is now implemented; the earlier disabled-capture statements describe the initial port only. See [Virtual Touch](VIRTUAL_TOUCH.md) for framebuffer details, touch input, image-generation prompt, validation and deployment results. Basic capture and coordinate targeting have been verified on physical Fab4 hardware. Baby builds and regression tests still pass.
