# VPN validation and production-readiness review

Secure portal update (16 November 2026): normal profile provisioning now uses paired HTTPS; SSH is advanced recovery only. See [HTTPS delivery and remaining physical acceptance](TLS_DELIVERY.md) and the [secure portal guide](SECURE_WEB_PORTAL.md). Historical SSH-only notes below are superseded by that update.


Current status: the latest [public IP/location/traffic follow-up](#public-ip-location-and-traffic-follow-up--15-november-2026) supersedes historical acceptance notes below. Playback and startup gates remain open.


Date: 2026-11-15. Recommendation: **hold production release pending supported-device acceptance**. Build and host-test evidence does not establish Baby/Fab4 networking or audio performance. No commit, push, public catalog update, publication or firmware upgrade was performed. A private Baby overlay was subsequently installed; see the acceptance follow-up below.

## Baby acceptance follow-up: Community Firmware 9.0.2

The user subsequently made 192.168.1.78 available on **Community Firmware 9.0.2 r17160**, not r17146. Read-only inspection confirmed ARMv5TEJ / Logitech MX25 Baby Board, OpenVPN **2.5.11**, OpenSSL **1.1.1w**, compiled management and AEAD support, firmware `CONFIG_TUN=m`, and `/lib/modules/2.6.26.8-rt16/tun.ko`. The module alias maps char-major-10-200 to tun; firmware udev rules create net/tun. Running the supported `/sbin/modprobe tun` loaded it and created the 10:200 device. No unrelated module or kernel was installed.

The actual kernel reports **CONFIG_NETFILTER is not set**. iptables/ip6tables and their proc tables are absent; IPv6 is not active. Installing firewall executables alone cannot add the missing kernel facility. The manager correctly reports ordinary VPN available, kill_switch_available=false, boot_time_leak_protection=false and IPv6 unavailable. An attempt to enable the kill switch is rejected with HTTP 409 / VPN_FIREWALL_ERROR, leaving it disabled. A netfilter-capable matching kernel/firmware would require separately approved firmware work; no substitute weaker kill switch was added.

Two user-provided Proton `.ovpn` files were found locally and excluded from Git using the repository's local exclude file. Originals are unchanged. Private copies remove only provider up/down scripts, script-security and setenv directives; the manager replaces script-based routing/DNS with its own controlled implementation. Both copies pass the native allowlist parser, preserving CA, tls-crypt, endpoint and certificate checks. They were imported through the paired HTTP API carried over an encrypted SSH forward. No service username/password was supplied, printed or stored in source; both profiles currently report credentials not configured.

A private overlay containing sbvpn, sbwebserver, the Base/VPN Lua modules, strings and web assets was byte-verified and installed only on the approved Baby. Original files and their mode/hash manifest are backed up under ignored `.local/vpn/device-backup-902/`. Persistent Base configuration and public repository catalogs were preserved. No firmware flash/upgrade, explicit reboot command, commit, push or publication occurred.

**Unexpected lifecycle event:** the device rebooted during the attempted Jive software restart. New uptime and loss of the temporary staging directory confirm the reboot; its precise cause is not established (watchdog interaction is a possibility). Only a Jive restart was requested; no reboot command was issued. Do not repeat this restart path during acceptance. Investigate the existing watchdog-aware maintenance coordinator and obtain explicit reboot approval before startup testing. After recovery the TUN module was absent again; it was reloaded using modprobe, and a capability refresh returned available=true / DISCONNECTED. Startup remains disabled. The boot-time OpenVPN version probe also initially failed its short deadline under startup load; a later refresh succeeded. This requires further startup validation rather than a readiness claim.

Windows initially selected an unsuitable source address for access to the Radio while its VPN adapter was present. Binding the acceptance SSH connection to the computer's Wi-Fi address restored access, without changing Radio or Windows routes. A loopback-only encrypted forward is running at **http://127.0.0.1:8088**. The user was asked to pair that browser and enter Proton OpenVPN service credentials directly into each profile; credentials must not be pasted into the development conversation.

Pending: credential provisioning and first actual provider connection, public egress/DNS/route checks, Virtual Radio/native menu operation during VPN, two-profile switching, reconnect/recovery, controlled playback/performance, and approved startup reboot. Physical kill-switch/IPv6 tests cannot pass on this kernel and remain unsupported, not successful. Touch testing remains deferred at the user's request.

## Environment and reproducibility

The original Ubuntu-24.04 distro still fails with filesystem EIO. Rather than repair or reinstall its filesystem, a separate **SBVPNValidation** WSL2 distro was imported from the official Ubuntu Minimal 24.04 rootfs, verified against Canonical's SHA256SUMS. The original registration and Windows default distro were preserved. The new distro and private artifacts live under ignored `.local/vpn/`; retain them for follow-up testing. The temporary WSL system-environment extraction was not used for accepted tests.

Host dependencies installed in the new distro: build-essential, cmake, Python3, Lua 5.1, Node, zlib/libpng/libjpeg/libexpat development libraries, libcurl development library, OpenVPN, OpenSSL, iproute2, iptables, util-linux, qemu-user, git, clang-format, file and zip/unzip. A private venv `/tmp/sb-vpn-python` contains Lupa 2.8 for Lua 5.4 firmware-adapter/recovery fixtures. No runtime device dependency was installed.

Latest source was mirrored with LF normalization to `/tmp/sb-vpn-source`, excluding ignored files. The native build uses `/tmp/sb-vpn-build`:

```sh
cmake -S /tmp/sb-vpn-source -B /tmp/sb-vpn-build \
  -DBUILD_SBPROXY=ON \
  -DPython3_EXECUTABLE=/tmp/sb-vpn-python/bin/python3 \
  -DSB_TEST_FIRMWARE_ADAPTERS=ON \
  -DSB_RADIO_SOURCE=/mnt/c/Projects/squeezebox \
  -DSB_SPOTIFY_SOURCE=/mnt/c/Projects/SBSpotifyConnect \
  -DSB_QOBUZ_SOURCE=/mnt/c/Projects/QoBuz
cmake --build /tmp/sb-vpn-build -j4
```

Run the complete suite **serially in a private host network namespace**, with loopback up. Existing fixtures reuse fixed ports 3483/9000 and others; parallel execution or unrelated host listeners can make them talk to the wrong service. The accepted run uses root because existing maintenance/deployment ownership fixtures require it:

```sh
sudo unshare --net sh -c 'ip link set lo up; ctest --test-dir /tmp/sb-vpn-build --output-on-failure'
sudo env SB_VPN_FAULT_LIBRARY=/tmp/sb-vpn-build/tests/libvpn_exec_fault.so \
  /tmp/sb-vpn-python/bin/python3 /tmp/sb-vpn-source/tests/vpn/test_netns.py \
  /tmp/sb-vpn-build/native/sbvpn/sbvpn
```

The second script creates its own private mount/network namespaces, private resolver tree and ephemeral test server/certificates. It must not be decomposed into networking commands run on a device.

ARM builds retain the existing SDK and static prefixes:

```sh
ARM_WOLFSSL_PREFIX=/mnt/c/Projects/SBStandaloneBase/StandaloneBase/build-proxy-tls/prefix \
ARM_IMAGE_PREFIX=/mnt/c/Projects/SBStandaloneBase/StandaloneBase/build-image-arm/prefix \
  sh /tmp/sb-vpn-source/scripts/build-arm.sh all
sh /tmp/sb-vpn-source/scripts/build-vpn-arm.sh all
sh /tmp/sb-vpn-source/scripts/verify-arm.sh baby /tmp/sb-vpn-source/build-arm/baby
sh /tmp/sb-vpn-source/scripts/verify-arm.sh fab4 /tmp/sb-vpn-source/build-arm/fab4
```

The final VPN-only rebuild incorporates the last validation fixes after the full target builds. Both packages contain those final manager bytes. Baseline vendored and music compiler warnings remain; no new VPN compiler warnings remain in the final VPN-only build.

Private package generation uses SB_PACKAGE_OUTPUT under `.local/vpn/candidates` and a private stage per target; see VPN_DELIVERY.md. Read-only final verification:

```sh
python3 scripts/verify-vpn-candidate.py /tmp/sb-vpn-source \
  /mnt/c/Projects/SBStandaloneBase/StandaloneBase/.local/vpn/candidates
qemu-arm -cpu arm926 /tmp/sb-vpn-source/build-arm/baby/sbvpn --version
qemu-arm -cpu arm1136 /tmp/sb-vpn-source/build-arm/fab4/sbvpn --version
```

## Accepted software validation results

Full host build passed. The full serial isolated regression passed **138/138**, zero failed/skipped, in 316.48 seconds. The optional SB_BUILD_MUSIC_SESSION=ON build and its **four additional** production bootstrap/resource-IPC/rendezvous/store-pairing tests passed. Final VPN group: **4/4** (included in the full suite), including 18 parser/storage/manager unittest methods. Real OpenVPN integration: **35/35 scenarios passed**, rerun after the final manager correction. Existing sibling Radio: **29 Lua suites passed**. Baby/Fab4 full cross-builds, final manager rebuilds, package verification and both ARM CPU smoke checks passed.

The full-suite pass preceded the final narrow down-hook correction; affected VPN and integration checks passed again afterward. Optional Music adds four tests after the core run; this is not a claim of a single 142-test invocation. Ordinary candidate packages exclude optional Music/UPnP deployment overlays; physical qualification remains separate. Candidate hashes are in VPN_DELIVERY.md.

Extra Music configuration commands:

```sh
cmake -S /tmp/sb-vpn-source -B /tmp/sb-vpn-build -DSB_BUILD_MUSIC_SESSION=ON
cmake --build /tmp/sb-vpn-build -j4
sudo unshare --net sh -c 'ip link set lo up; ctest --test-dir /tmp/sb-vpn-build -R "^music_(production_|rendezvous_pairing|store_pairing)" --output-on-failure'
```

## Fixes from this review

- Recognize only the measured root-owned `/mnt/storage/standalonebase -> /etc/standalonebase` firmware alias. Both ancestor trees still pass ownership/mode checks; arbitrary alias targets and profile/runtime symlinks remain rejected. A namespace test reproduces both accepted and rejected cases.
- Treat unreadable network journals as invalid rather than absent. Preserve them through normal manager exit and refuse false successful recovery. Reject corrupt persisted boolean/string setting types rather than silently dropping protection settings. New manager tests exercise both cases.
- Restore executable ZIP/deployment mode for the existing sb-tinysc-control helper. Candidate metadata now verifies that binary and sbvpn, CRC and exact Lua/JS/HTML assets. The standalone candidate verifier checks all eight executables against final build bytes, static dependencies, ARM EABI/CPU/soft-float attributes, target-only storage artifacts and absence of private runtime records.
- Correct three existing regression fixtures: descriptor cleanup is asynchronous and gets a bounded two-second wait; the Music HTTP fixture now supplies a trusted idle proof after Stop instead of expecting STMf input-flush acknowledgement to prove physical decoder release; the Comet backlog fixture now reads complete Content-Length frames instead of assuming a final HTTP header means its body arrived. The latter passed five consecutive runs. Production audio/ownership requirements were not weakened.
- An authenticated tunnel-down event leaves CONNECTED immediately, preserving an existing ERROR or intentional transition state and clearing connection duration/verification.
- Add an optional, disabled-by-default country lookup, bound to the independently checked public IP, with hourly request limiting, in-memory cache and strict success/IP/country-code validation. Invalid/mismatched replies do not change tunnel connectivity or copy profile labels into verified fields.

## Hardware inspection and acceptance boundary

At the initial review, the previously approved Radio **192.168.1.78 reported 7.7.3 r16676**, Linux 2.6.26.8-rt16, ARM926EJ-S / ARMv5TEJ, Logitech MX25 Baby Board. It runs Jive plus the existing sbbase, sbproxy and sbwebserver. Its config root is the trusted symlink described above; resolver is a root-owned regular 0644 file, default route is eth1 through 192.168.1.254 on 192.168.1.0/24, IPv4 forwarding is off and /proc/net/if_inet6 is absent.

OpenVPN and iptables/ip6tables are absent. No /dev/net/tun, loaded TUN module or matching TUN module under /lib/modules was found. Therefore the inspection does **not** rest on device-node absence alone. No modprobe/insmod, route, DNS, firewall, reboot or firmware operation was performed. A different 9.0.2 Radio address and compatible provider profile were requested. They were not available for the initial review; the follow-up above records the later 9.0.2 inspection and private deployment. No profile contents or VPN credentials were requested in conversation or printed.

On the actual 9.0.2 device, inspect OpenVPN --version/--help and cipher support, /proc/config.gz if present, /proc/modules, module tree and firmware boot/udev/module configuration. If TUN is packaged but unloaded, identify the firmware-supported modprobe/udev mechanism and verify matching kernel module/major-minor/permissions before approved activation. Do not create a node or load an unrelated module merely to make the capability check pass. If IPv4 or required IPv6 firewall support is missing, VPN remains unavailable where protection cannot be provided; bundling firewall tools requires matching kernel tables/REJECT support and independent tests, not just copying an executable.

| Physical acceptance item | Actual result |
| --- | --- |
| Firmware/model/runtime inspection | Passed read-only on the stock Radio; requested 9.0.2 target unavailable |
| SSH and forwarded HTTP root page | Accessible; HTTP 200 |
| Secure provider profile provision | Pending provider profile and supported target |
| Web and native VPN operation | Pending supported target; host/API/Jive fixtures cover software contracts |
| Public egress change and return after disconnect | Pending; host controlled egress is not Internet acceptance |
| VPN/fallback DNS, DHCP renewal and restoration | Pending on target; namespace equivalents pass |
| IPv4 kill switch, IPv6/local multicast and ownership recovery | Pending on target; namespace equivalents pass |
| Virtual Radio during VPN | Pending; existing remote regression fixtures only |
| Wi-Fi/Ethernet failure, reconnect and --recover | Pending physical test; process/route/manager failures tested on host |
| Startup after reboot | Pending explicit reboot approval and supported target |
| Spotify/Qobuz/Music/UPnP over VPN | Pending physical network/control/audio tests |
| Touch/Fab4 | No device supplied; ARM build is not hardware acceptance |

The literal IPv4 remote requirement remains enforced for kill-switch mode. LAN DNS exceptions are not used to bypass it. Public IPv6 is blocked while requested when supported, not tunneled. No protection before the Jive supervisor starts is advertised. Destructive network tests require confirmed local SSH/portal access plus recovery before beginning.

## Playback and Now Playing review

Reviewed actual Radio source at `C:/Projects/squeezebox`, revision c389a7d. `_watchdogTick` detects absent or stalled streams and schedules one reconnect timer; `_scheduleReconnect` checks current station/generation and backs off to 30 seconds. Stop invalidates generation and cancels timers; radio Pause uses Stop. Episode pause/EOF and intentional stop are excluded from recovery. The existing Now Playing callback remains authoritative. The normal reconnect path retains station identity/selection and does not seize a different current player; ICY/HLS and artwork paths are unchanged.

Decision: reuse that recovery path; add no VPN-specific audio dependency or duplicate event-triggered playback restart. The VPN manager continues publishing actual state and network_ready_generation without directly controlling audio. A new fixture executes the actual watchdog/scheduling/stop/pause methods with controlled decoder/timer boundaries: advancing bytes leaves playback/metadata/volume/logo unchanged; a stall schedules one retry; queued callbacks cannot restart after Stop/Pause; inactive/paused/EOF/intentional-stop states do not initiate recovery. This verifies method-level intent, not a real audio decoder or physical network interruption. All 29 existing Radio Lua fixture suites passed, including HLS metadata/player, Now Playing, standby recovery and playback controls.

Mandatory remaining measurements are actual initial connect/disconnect/switch/drop/reconnect/network-change behavior for an active stream, explicit stopped/paused state, ICY/HLS metadata continuity and local artwork; Spotify/Qobuz Connect and UPnP discovery/control; Touch USB/SD/Music and standby/resume. If hardware exposes a recovery gap, extend the existing event architecture based on that evidence rather than forcing unconditional restarts.

## Physical performance evidence

A read-only five-second observation on the stock Radio was taken while leaving its workload unchanged. It is **not an idle benchmark**, not a controlled bitrate, and cannot be used to estimate VPN overhead:

| Observed process | CPU (% of single core) | RSS KiB |
| --- | ---: | ---: |
| Jive | 14.27 | 34,432 |
| jive_alsa | 6.82 | 6,996 |
| sbproxy | 0.00 | 988 |
| sbbase | 0.12 | 604 |
| sbwebserver | 2.48 | 1,000 |

Total observed system CPU was 47.39%; MemTotal 62,112 KiB, MemFree 2,028 KiB, buffers 2,548 KiB and cache 16,412 KiB. RSS overlaps shared pages and must not be summed as unique memory. Three SSH-forwarded HTTP first-response observations were 51.7, 129.3 and 119.7 ms, including channel/network overhead, not device UI response time. No sbvpn/OpenVPN process was running. Raw/summary evidence remains in ignored `.local/vpn/`.

Still required: paired baseline/VPN runs of idle, 128 kbps, 320 kbps, HLS, available lossless, concurrent metadata/artwork, switching and reconnect. Record per-process and total CPU, RSS/free/cache, underruns/audible interruptions, recovery/connection/switch time, UI input latency and bounded VPN throughput. Use the same station/server/workload and repeat each sample; operator listening is needed for audible acceptance. No values for those unperformed tests are invented.

## Secure provisioning and future HTTPS recommendation

Keep SSH-forwarded paired provisioning for this candidate. Direct LAN HTTP still rejects profile and credential writes; no insecure fallback was added.

The vendored Mongoose supports server TLS initialization and a wolfSSL backend, while this webserver currently builds with MG_TLS_NONE. Its existing request handlers could serve a second HTTPS listener. The proxy's static wolfSSL prefix is a candidate library, but its present options do not enable certificate/key generation and its inbound handshake/API compatibility requires a separate build experiment. Source support is not a measured Baby memory or speed result. See [Mongoose TLS instructions](https://mongoose.ws/documentation/tutorials/tls/) and [build options](https://mongoose.ws/docs/getting-started/build-options/).

Recommended future architecture: retain one web process and handlers; add an optional bounded HTTPS listener with TLS 1.2+ and a unique persistent device key/certificate, generated once using strong OS entropy. Evaluate ECDSA P-256 versus RSA-2048 for browser compatibility and actual Baby handshake cost. Store keys in an owner-only directory, separate from ordinary backups; preserve identity on applet upgrade and require physical confirmation to rotate it. Validate time handling, SAN hostname/IP behavior and server-library configuration.

A self-signed certificate encrypts traffic but browsers cannot identify it automatically. Show its SHA-256 fingerprint on the physical Radio/Touch with clear one-time trust instructions, retain physical PIN pairing and browser revocation, and use Secure cookies on HTTPS. Update origin/Host handling and scheme/port validation deliberately, including rebinding/CSRF tests; do not trust X-Forwarded-* or redirect secret-bearing HTTP posts. Keep native loopback capability APIs on the private HTTP path. Avoid HSTS during bootstrap/recovery until the hostname/certificate story is stable. HTTPS authentication must cover Virtual Radio assets/API, management uploads and long polling without mixed-content requests.

For nontechnical users, a physical pairing screen can show the HTTPS hostname and fingerprint, but cannot remove browser certificate warnings without a trusted certificate. Public ACME issuance needs a stable domain/ownership strategy and often Internet connectivity; a shared private key or silent installation of a broad CA is unsuitable. A local user-trusted CA would add provisioning burden. Choose this identity/trust product flow before broad rollout, then measure bounded concurrent handshakes, RAM and CPU while playing audio on both targets. No HTTPS server redesign was implemented here.

## Optional country verification decision

Selected `https://ipwho.is/` as an optional development lookup: its [documentation](https://ipwhois.io/documentation) describes HTTPS, no API key, country_code and a 1,000/day per-client-IP free allowance with commercial use and no uptime guarantee. [Privacy policy](https://ipwhois.io/privacy) permits usage logging, including IP addresses. ipapi.co was not selected because its [free-plan pricing](https://ipapi.co/pricing/) describes development/testing rather than production use.

The implementation is explicitly opt-in and off by default. After an independent public IPv4 check, it requests only success/ip/country_code for that address through the existing certificate-validating HTTPS proxy. It sends no account, profile name, device identity or credential. The operator is told the public IP may be logged. Requests are limited to one per hour per manager lifetime, success cached in memory for that same IP; a new IP within the interval stays Unknown. Restart resets this in-memory limit. Proxy/body/time limits apply; exact returned IP and two uppercase country-code characters are required. Configured country remains a separate unverified label. Replies are approximate IP geolocation, not proof of physical presence or provider ownership. The free quota is shared by users of a VPN exit, so throttling/unavailability is expected and nonfatal.

Host fixtures cover opt-in off, successful lookup, binding to the checked IP, cache reuse, disabling, and rejecting a mismatched IP while keeping CONNECTED. They mock the proxy response and do not claim a real geolocation lookup. Actual proxy certificate validation/egress/country tests remain mandatory on the target. Review provider terms again before production release.

## Release gate

No production recommendation until complete build/regression evidence is accepted and mandatory supported Radio/Touch security, networking, streaming and performance tests pass. Private candidate integrity is useful review evidence; it is not approval to publish or deploy to an unqualified device. Credentials must be entered by the user through the encrypted paired portal, never pasted into this conversation. Reboot remains separately subject to explicit approval.

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
