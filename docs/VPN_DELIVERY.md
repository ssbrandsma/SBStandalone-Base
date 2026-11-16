# Standalone VPN delivery and validation report

Secure portal update (16 November 2026): normal profile provisioning now uses paired HTTPS; SSH is advanced recovery only. See [HTTPS delivery and remaining physical acceptance](TLS_DELIVERY.md) and the [secure portal guide](SECURE_WEB_PORTAL.md). Historical SSH-only notes below are superseded by that update.


Current status: the latest [public IP/location/traffic follow-up](#public-ip-location-and-traffic-follow-up--15-november-2026) supersedes historical acceptance notes below. Playback and startup gates remain open.


Date: 2026-11-15. Status: integrated development implementation; **not production-ready**. Mandatory target hardware/network/audio acceptance remains open. Final build and candidate verification results are recorded below; the [readiness review](VPN_READINESS_REVIEW.md) supersedes the earlier WSL-blocked assessment. No release version, public config.json, extensions.xml, published archive, commit or push was performed for this assignment.

## A. Implementation summary

Implemented a real native OpenVPN manager, secure profile/credential storage, runtime capability checks, owned TUN/routes/DNS/firewall/recovery, reconnect/startup policy, authenticated asynchronous web API, VPN browser page and Jive menu. Both frontends use the same manager. Actual host OpenVPN TLS/service-password authentication and network changes are exercised in isolated kernel namespaces; this is not a mocked connection backend.

The existing service supervisor, pairing, loopback native capability, process identity helpers, HTTPS proxy, public-address policy, build and packaging paths are reused. Existing music/repository/storage functionality is preserved. Kill-switch ownership and partial-failure behavior, strict import policy and accurate unavailable status were prioritized. Optional unsupported VPN does not prevent Base from running.

Deliberate scope limits: inline certificates only; IPv4 TUN client transport; public IPv6 blocking rather than tunneling; hostname endpoints unavailable with kill switch; country labels are not geolocation; country lookup is explicitly optional; no early-boot protection; no new playback restart/event consumer. These avoid silently promising behavior not implemented or physically validated. See [implementation](VPN_IMPLEMENTATION.md) for details.

## B. Important files

| Files | Purpose |
| --- | --- |
| native/sbvpn/main.c, vpn.h | State machine, IPC, OpenVPN supervision/hooks, capability/status/API and retry policy |
| native/sbvpn/profile.c, json.c | Allowlisted import and strict bounded JSON validation |
| native/sbvpn/store.c | Private atomic storage and redacted metadata |
| native/sbvpn/process.c, ownership.c | Bounded argv execution and exact process recovery |
| native/sbvpn/network.c | Native interface/routes, resolver ownership, isolated firewall and recovery |
| native/sbvpn/CMakeLists.txt, root CMakeLists.txt | Native build integration |
| native/sbwebserver/src/vpn_http.c/.h | Nonblocking authenticated manager bridge |
| native/sbwebserver/src/http_server.c, main.c, CMakeLists.txt | API dispatch/tick integration and core-dump suppression |
| native/sbwebserver/web/js/vpn.js, app.js, index.html | VPN dashboard, import/edit, location/settings and sanitized diagnostics |
| applet/Vpn.lua, StandaloneBaseApplet.lua, strings.txt | Shared-backend native menu, optional supervised service and EN/NL strings |
| scripts/build-vpn-arm.sh, build-arm.sh, verify-arm.sh | Baby/Fab4 static ARM builds and ABI checks |
| packaging/build-package.sh, packaging/target-metadata.py, scripts/verify-vpn-candidate.py, scripts/deploy_dist.py | Ship manager, Lua/web assets; executable modes and owned deployment stop path |
| tests/vpn/*, tests/CMakeLists.txt | Parser/storage/API/Jive/Radio recovery fixtures and privileged real-OpenVPN integration |
| tests/comet/test_backpressure.py, tests/music/test_playback_http.py, test_backlog_drain.py | Fix asynchronous cleanup, physical-stop proof and complete HTTP-frame fixture handling |
| docs/VPN.md, VPN_IMPLEMENTATION.md, VPN_DELIVERY.md, VPN_READINESS_REVIEW.md, README.md | User guide, architecture, validation and release boundary |

Fault injection is a host-test-only LD_PRELOAD library. No fault environment hook or synthetic credential is shipped in the production manager. Temporary device credential script was removed from ignored local work files.

## C. Build, installation and recovery

Host dependencies: Linux C compiler, pthreads, CMake, Python3, Lua and Node for applicable frontend tests, and the existing project build dependencies. Set BUILD_SBPROXY=OFF only when using the existing separate proxy build. Privileged integration additionally needs OpenVPN 2.5+ (tested host 2.6.19), OpenSSL, iproute2, iptables/ip6tables, util-linux unshare/nsenter and mount privileges. These host tools are test infrastructure, not new device dependencies.

Normal host commands:

```sh
cmake -S . -B build-vpn -DBUILD_SBPROXY=ON
cmake --build build-vpn -j4
sudo unshare --net sh -c 'ip link set lo up; ctest --test-dir build-vpn --output-on-failure'
sudo env SB_VPN_FAULT_LIBRARY="$PWD/build-vpn/tests/libvpn_exec_fault.so" \
  python3 tests/vpn/test_netns.py "$PWD/build-vpn/native/sbvpn/sbvpn"
```

The actual host run used a LF-normalized source mirror `/tmp/sb-vpn-source`, build directory `/tmp/sb-vpn-build` and system CMake/CTest in SBVPNValidation. A private venv supplies Lupa for the Radio/adapter fixtures; see the readiness review for the full configure command. Exact successful VPN test commands:

```sh
ctest --test-dir /tmp/sb-vpn-build -R '^vpn_' --output-on-failure
SB_VPN_FAULT_LIBRARY=/tmp/sb-vpn-build/tests/libvpn_exec_fault.so \
  /tmp/sb-vpn-python/bin/python3 /tmp/sb-vpn-source/tests/vpn/test_netns.py /tmp/sb-vpn-build/native/sbvpn/sbvpn
```

Run the second command as root inside a Linux environment; the script creates private mount/network namespaces and never targets a Radio. It builds a private /etc tree and ephemeral synthetic certificates. The test intentionally kills its own OpenVPN/manager and injects firewall command failures. Do not run network test fragments outside that script's namespaces.

ARM dependencies are the existing static musl SDK, curl/wolfSSL prefixes and static image libraries. The VPN-only builder needs just the SDK and existing vendored Mongoose source. Commands used from WSL:

```sh
sh /tmp/sb-vpn-source/scripts/build-vpn-arm.sh all
ARM_WOLFSSL_PREFIX=/mnt/c/Projects/SBStandaloneBase/StandaloneBase/build-proxy-tls/prefix \
ARM_IMAGE_PREFIX=/mnt/c/Projects/SBStandaloneBase/StandaloneBase/build-image-arm/prefix \
  sh /tmp/sb-vpn-source/scripts/build-arm.sh all
```

The SDK default is `/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2`; override ARM_TOOLCHAIN_DIR when appropriate. ARM_TLS_PREFIX/SBHTTPSPROXY_ROOT, ARM_WOLFSSL_PREFIX and ARM_IMAGE_PREFIX retain existing meanings. Baby is ARMv5TE soft-float; Fab4 is ARMv6 soft-float. No OpenVPN executable is bundled; compatible firmware provides it.

After the final builds and acceptance, create candidate packages using the ordinary packaging script with **private output**:

```sh
SB_PACKAGE_OUTPUT="$PWD/.local/vpn/packages" \
SB_PACKAGE_STAGE="$PWD/.local/vpn/stage/baby/StandaloneBase" \
  sh packaging/build-package.sh baby
SB_PACKAGE_OUTPUT="$PWD/.local/vpn/packages" \
SB_PACKAGE_STAGE="$PWD/.local/vpn/stage/fab4/StandaloneBase" \
  sh packaging/build-package.sh fab4
```

The current package filenames still contain 0.7.3. These development candidates must not overwrite published 0.7.3 artifacts. Before a release, assign a new version through the existing release workflow, build both targets, verify executable ZIP modes and asset integrity, and update catalog hashes/URLs together. Both private candidate packages have now been built and verified; they remain unpublished. Their names still carry the baseline version and must never replace published 0.7.3 archives.

Install/upgrade using the existing authenticated applet installer for the correct target after release qualification. Persistent `/mnt/storage/standalonebase/vpn` must remain intact; no storage initialization or firmware flash is needed for applet update. The optional sbvpn service starts under the existing applet supervisor. Compatible firmware is a prerequisite; an unavailable message explains missing capabilities.

Enable through the [user guide](VPN.md): SSH forward, paired browser, inline profile and service credentials, then Connect. Disable using Disconnect and startup off. Root recovery:

```sh
/path/to/StandaloneBase/sbvpn --ctl status
/path/to/StandaloneBase/sbvpn --recover
```

Recovery works through live IPC or a locked offline journal, disables startup and removes only verified owned state. Corrupt ownership records are retained rather than triggering a broad reset. Preserve runtime records until recovery succeeds.

## D. Executed validation

The previous WSL-interrupted run is historical and is not counted as successful regression evidence. Validation was resumed in an independent SBVPNValidation WSL2 distro without repairing or replacing the original distro. Full details, exact current commands, fixtures and research: [VPN_READINESS_REVIEW.md](VPN_READINESS_REVIEW.md).

| Check | Actual result |
| --- | --- |
| Complete host build including proxy | Passed |
| Full host regression, serial/private network namespace | **138/138 passed, 0 failed, 0 skipped**; 316.48 seconds |
| Optional production Music configuration | Build passed; **4/4 additional tests passed**, 0 failed |
| Final VPN CTest group | **4/4 passed**, including actual Radio watchdog-method fixture; part of the full suite |
| Parser/storage/manager cases | **18 unittest methods passed**, including hostile-option subcases and retained invalid journals/settings |
| Real OpenVPN namespace integration | **35 scenarios passed, 0 failed**, rerun after final manager fixes |
| Existing Radio fixtures | **29 Lua suites passed** using actual sibling Radio source |
| Baby and Fab4 complete cross-builds | Passed; final manager rebuilt for both after last source fix |
| ABI and private packages | Both passed: ARM EABI/CPU/soft-float, no dynamic dependencies, all eight executable modes, exact final binary/asset bytes, CRC and target contents |
| ARM CPU emulation smoke checks | Both manager --version checks passed (ARM926 / ARM1136); not hardware acceptance |
| JS/Lua/Python syntax and source whitespace | Passed |
| Physical VPN/provider/Touch acceptance | Pending; no compatible target/profile supplied |

The complete host suite passed before the final narrow down-hook state correction; the affected VPN group and all 35 real OpenVPN scenarios were rerun successfully on the final manager afterward. Optional production Music targets were subsequently built and their four additional bootstrap/resource-IPC/pairing tests passed. Earlier parallel/non-isolated failures and corrected fixture races are not accepted full-suite passes.

Private candidates are under `.local/vpn/candidates/`. These use ordinary core packaging; optional Music/UPnP deployment overlays were not enabled or physically accepted. Optional production Music has separate host build/test evidence.

| Target | Bytes | Files | SHA-256 |
| --- | ---: | ---: | --- |
| baby | 2,879,821 | 58 | `534e4721e4ea80618d8157645959913615a306ae32c08f94d1ac8579d589d13c` |
| fab4 | 2,787,142 | 56 | `6e425049ea8321e1699a6c3c3012b0daa2c62e67665855d33b876d82dc586c5c` |

The 35 real OpenVPN host scenarios cover all original TLS/authentication, actual tunnel egress, LAN, DNS, IPv6, guarded switching, firewall fault injection, route/process/manager crash recovery and startup/recovery cases. Added cases reproduce the trusted firmware configuration alias, reject another alias target, recover a new connected IPv6 prefix, refuse nonstandard policy routing, fail safely without pushed DNS, use explicit fallback DNS, verify country opt-in/cache/disable and reject a country result for another IP. The public IP and country services remain proxy-contract fixtures; real public Internet/target acceptance is not implied.

At the initial review, device 192.168.1.78 was inspected read-only on **7.7.3 r16676**, Linux 2.6.26.8-rt16, ARMv5TEJ. OpenVPN, IPv4/IPv6 firewall tools, /dev/net/tun and a TUN module under /lib/modules were not found. Existing Jive/Base services run, SSH works, and a forwarded portal GET returns HTTP 200. Its measured configuration-root alias led to the compatibility fix above. No module activation, profile provision, device deployment, network mutation, flash write or reboot occurred. The Community Firmware 9.0.2 device and profiles were subsequently supplied; see the follow-up below. Touch and reboot approval remain pending.

A read-only five-second observation of the unchanged stock Radio workload measured system CPU 47.39%, sbbase 0.12% / 604 KiB RSS, sbproxy 0% / 988 KiB and sbwebserver 2.48% / 1,000 KiB. It is not a controlled idle/audio workload or a VPN overhead benchmark. See the review for Jive/memory/SSH HTTP observations and the full pending workload matrix. No VPN throughput, audio-underrun/recovery, switching or UI-input performance is claimed.

### Later Baby hardware follow-up

The user subsequently made 192.168.1.78 available on **Community Firmware 9.0.2 r17160**, not r17146. Read-only inspection confirmed ARMv5TEJ / Logitech MX25 Baby Board, OpenVPN **2.5.11**, OpenSSL **1.1.1w**, compiled management and AEAD support, firmware `CONFIG_TUN=m`, and `/lib/modules/2.6.26.8-rt16/tun.ko`. The module alias maps char-major-10-200 to tun; firmware udev rules create net/tun. Running the supported `/sbin/modprobe tun` loaded it and created the 10:200 device. No unrelated module or kernel was installed.

The actual kernel reports **CONFIG_NETFILTER is not set**. iptables/ip6tables and their proc tables are absent; IPv6 is not active. Installing firewall executables alone cannot add the missing kernel facility. The manager correctly reports ordinary VPN available, kill_switch_available=false, boot_time_leak_protection=false and IPv6 unavailable. An attempt to enable the kill switch is rejected with HTTP 409 / VPN_FIREWALL_ERROR, leaving it disabled. A netfilter-capable matching kernel/firmware would require separately approved firmware work; no substitute weaker kill switch was added.

Two user-provided Proton `.ovpn` files were found locally and excluded from Git using the repository's local exclude file. Originals are unchanged. Private copies remove only provider up/down scripts, script-security and setenv directives; the manager replaces script-based routing/DNS with its own controlled implementation. Both copies pass the native allowlist parser, preserving CA, tls-crypt, endpoint and certificate checks. They were imported through the paired HTTP API carried over an encrypted SSH forward. No service username/password was supplied, printed or stored in source; both profiles currently report credentials not configured.

A private overlay containing sbvpn, sbwebserver, the Base/VPN Lua modules, strings and web assets was byte-verified and installed only on the approved Baby. Original files and their mode/hash manifest are backed up under ignored `.local/vpn/device-backup-902/`. Persistent Base configuration and public repository catalogs were preserved. No firmware flash/upgrade, explicit reboot command, commit, push or publication occurred.

**Unexpected lifecycle event:** the device rebooted during the attempted Jive software restart. New uptime and loss of the temporary staging directory confirm the reboot; its precise cause is not established (watchdog interaction is a possibility). Only a Jive restart was requested; no reboot command was issued. Do not repeat this restart path during acceptance. Investigate the existing watchdog-aware maintenance coordinator and obtain explicit reboot approval before startup testing. After recovery the TUN module was absent again; it was reloaded using modprobe, and a capability refresh returned available=true / DISCONNECTED. Startup remains disabled. The boot-time OpenVPN version probe also initially failed its short deadline under startup load; a later refresh succeeded. This requires further startup validation rather than a readiness claim.

Windows initially selected an unsuitable source address for access to the Radio while its VPN adapter was present. Binding the acceptance SSH connection to the computer's Wi-Fi address restored access, without changing Radio or Windows routes. A loopback-only encrypted forward is running at **http://127.0.0.1:8088**. The user was asked to pair that browser and enter Proton OpenVPN service credentials directly into each profile; credentials must not be pasted into the development conversation.

Pending: credential provisioning and first actual provider connection, public egress/DNS/route checks, Virtual Radio/native menu operation during VPN, two-profile switching, reconnect/recovery, controlled playback/performance, and approved startup reboot. Physical kill-switch/IPv6 tests cannot pass on this kernel and remain unsupported, not successful. Touch testing remains deferred at the user's request.

## E. User operation

1. Obtain a modern provider OpenVPN TUN profile and its service credentials; inline certificate/key material.
2. Open the loopback SSH forward, pair that browser, and use VPN ? Add profile. Password replacement is explicit.
3. Choose a saved profile and Connect; inspect backend state and sanitized errors.
4. On the device, use Standalone Base ? Standalone VPN for enable/disable, location, status and startup.
5. Choose a country/profile to switch. Configured country is a label, not verified location.
6. Enable startup only after manual validation; automatic reconnect is independently configurable.
7. Enable kill switch only with literal IPv4 endpoints and VPN DNS, and test local recovery. Disconnect intentionally removes protection; --recover also disables startup.

Detailed instructions and limits: [VPN.md](VPN.md).

## F. Remaining release work

- Build/regression and private package results above establish host/cross-build evidence only; preserve their release gates and assign a new version before publication.
- Validate Radio and Touch on compatible firmware with real OpenVPN/TUN/firewall/resolver facilities. The inspected Radio is currently unsupported; do not treat its 7.7.3 behavior as a 9.x result.
- Validate real public egress and HTTPS proxy path, DNS leakage including DHCP renewal, all kill-switch transition windows and IPv6 behavior on each target kernel/firewall implementation.
- Exercise Wi-Fi/Ethernet changes, DHCP address and IPv6-prefix changes, standby/wake, reboot/startup/shutdown and corrupt/power-loss journals. Host route deletion, DHCP resolver replacement and process crash recovery passed; not all physical transitions were reproduced.
- Confirm SSH/web management, mDNS/SSDP, local LMS/app communication, existing Internet radio/music/alarms and deliberate pause/stop behavior during connect/switch/reconnect. No automatic audio start should be added. Existing watchdog behavior requires physical acceptance.
- Measure CPU/RSS, throughput, handshake and switching time on both devices during audio playback; choose production retry/polling limits from these results.
- Provider compatibility remains limited to the strict importer subset: no external certificate files, encrypted key prompts, interactive MFA, proxies, plugins, scripts, compression, TAP, IPv6 transport or arbitrary routes.
- No boot-before-Jive leak guarantee, IPv6 tunneling, encrypted-at-rest secret store, profile backup/export or cross-applet network-ready event consumer is implemented. Optional country verification needs real target/proxy acceptance and future provider-terms review.
- File scanning is bounded; repeated power-loss temporary-file accumulation and explicit corrupt-profile repair UX should be reviewed before release. Corrupt settings/journals fail closed and require inspection rather than automatic destructive repair.
- Complete OpenVPN build-feature probing beyond version/TUN/firewall/resolver checks, and independent security review on the actual firmware binaries. Runtime startup reports unsupported management/TLS options without lowering cryptography.

Implementation and private candidates are available for review. Supported Radio/Touch security, networking, playback and performance acceptance remains unfinished; production release is not recommended.

### Credential-editor usability correction

The user reported that Edit / replace credentials appeared unresponsive at the correct SSH-forwarded URL. The inline form was inserted below the profile list without being revealed. It now scrolls into view and focuses the name field, and unexpected opening errors are displayed. Direct LAN HTTP editing remains disabled, with a clickable SSH-portal link. Only vpn.js and index.html were updated and byte-verified on the Baby; no process restart or reboot was performed. The script cache key is now vpn.js?v=2; reload the portal to receive it.

A real-DOM test using the development-only node-jsdom package covers opening/reveal/focus, credential-only PUT submission, preserving omitted credentials, Cancel and LAN restrictions. The affected VPN group passed **5/5** after this change. This supplements the earlier full regression result; no new full-suite pass is claimed. Both private packages were refreshed and passed CRC, assets, executable modes and ABI verification again. No credentials were entered on the user's behalf.


## Public IP, location and traffic follow-up ? 15 November 2026

This follow-up supersedes the earlier pending-credential, country-only dashboard and older test/package results. Implementation and measurement details are in the [telemetry implementation follow-up](VPN_IMPLEMENTATION.md#public-ip-location-and-traffic-follow-up--15-november-2026).

- Implemented automatic/manual verified IPv4, IP-bound optional country/region/city, bounded location caching, native two-second TUN traffic accounting, and focus-preserving dashboard updates.
- **145/145 host tests passed** (318.58 seconds), plus **39 real OpenVPN namespace checks**. Both static ARM targets and private packages pass integrity, asset, mode and ABI checks. The final dashboard wording also passed the DOM test after the full run.
- Baby actual US: **149.22.84.138**, United States / California / San Jose. NL switch: **185.132.133.153**, NL / Provincie Zuid-Holland / Gemeente Westland. Old IP/location were invalidated immediately and the session reset. NL disconnect froze **22,129 RX / 14,713 TX bytes**, unchanged over five seconds, with zero rates. Final sysfs collector reports 32-bit kernel counters with uint64 totals. Geolocation opt-out was restored; VPN was left disconnected, matching initial state.
- Only the VPN manager and dashboard assets were replaced. No Jive restart or reboot occurred in this follow-up. No commit, push or publication occurred. NETFILTER remains unavailable; kill switch is disabled. TUN boot activation and startup validation remain unresolved; Fab4 is build-only.
- A 22.523-second KINK observation measured sbvpn **1.199% CPU / 488 KiB RSS**, webserver **3.818% / 768 KiB**. The earlier 11.26-second sample measured **1.043% / 576 KiB** and **3.304% / 1064 KiB**, but workloads were not controlled, so these are not a reliable overhead comparison.
- KINK's existing connection did not exercise the tunnel RX counters. The remote returned HTTP 423, requiring physical firmware authorization; it was not bypassed. Manual KINK restart and an HLS station were requested. Through-tunnel ordinary/HLS bitrate, audio continuity/watchdog recovery, and comparable playback performance acceptance remain incomplete.

Private candidates remain under ignored `.local/vpn/candidates/` with baseline 0.7.3 filenames, for internal acceptance only. They must receive an approved release version before publication. Current SHA-256 values:

- StandaloneBase-0.7.3-baby.zip: `534e4721e4ea80618d8157645959913615a306ae32c08f94d1ac8579d589d13c`
- StandaloneBase-0.7.3-fab4.zip: `6e425049ea8321e1699a6c3c3012b0daa2c62e67665855d33b876d82dc586c5c`


### Acceptance scope update

The user waived further playback and performance measurements and requested UI display verification only. The real-DOM dashboard test passed again, covering IP/location rendering, decimal totals/rates, two-second updates, editor/focus preservation, disconnected and new-session states. Installed Baby vpn.js, index.html and style.css were read back and match the source byte-for-byte; the live status API exposes traffic. VPN remains disconnected. No further station operation, measurement, Jive restart or reboot was performed. The previously documented playback measurements remain unverified rather than successful; they are no longer pending work for this assignment.


## Secure HTTPS provisioning follow-up ? 16 November 2026

The existing web server now has a generic HTTPS listener and persistent per-device identity. Secure PINs/cookies are separated from HTTP; direct LAN HTTP credential writes remain blocked. Existing Proton profiles and credentials are preserved. The native fingerprint/pairing menu is installed but needs an approved Jive restart to activate; interactive browser trust acceptance remains open. Full implementation, tests, Baby resource measurements, private deployment/rollback and release gates are recorded in [TLS_DELIVERY.md](TLS_DELIVERY.md).
