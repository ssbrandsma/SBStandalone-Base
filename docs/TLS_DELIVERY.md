# Secure HTTPS portal delivery and acceptance

Date: 16 November 2026. Status: **private development deployment; browser fingerprint/PIN/VPN-page acceptance confirmed; remaining production gates documented below**. No commit, push, catalog publication or firmware modification was performed. A controlled normal firmware reboot was explicitly approved and performed on 16 November; no ad hoc Jive restart was used.

## Delivered behavior

One existing sbwebserver process now serves HTTP on port 80 and HTTPS on port 443. The normal VPN provisioning path is the paired HTTPS portal, with no SSH tunnel. HTTP management and native loopback APIs remain available; HTTP profile writes from the LAN remain rejected. SSH loopback provisioning remains an advanced recovery option.

A device-local ECDSA P-256 certificate/key is generated once in the private persistent configuration directory, validated on startup, and retained across updates and web-server restarts. Corrupt, expired, mismatched or unsafe material disables HTTPS instead of silently changing trusted identity. Physical confirmation is required for explicit certificate reset. The full SHA-256 fingerprint and current HTTPS address have been added to the native Web Access menu. Scoped Secure Web Pairing Codes and HTTPS-only Secure/HttpOnly/SameSite cookies prevent HTTP PINs/tokens from authorizing HTTPS provisioning.

Details: [user guide](SECURE_WEB_PORTAL.md), [architecture and security policy](TLS_ARCHITECTURE.md), [VPN guide](VPN.md).

## Validation

- Full serial host regression in a private network namespace: **148/148 passed** (338.08 seconds), including the final connection-address and active-read/write idle-timeout changes and the Mongoose bounded-path regression.
- Real OpenVPN namespace integration: **39 checks passed**. This is host integration evidence, separate from provider/hardware acceptance.
- Independent Radio regression: **29 Lua suites passed**.
- Baby ARMv5TE soft-float and Fab4 ARMv6 soft-float builds: static ELF/ABI checks passed. All eight packaged executables are checked for architecture, no interpreter/shared-library dependencies and executable modes.
- The full real-socket HTTPS suite also passed with the actual Baby ARM/wolfSSL binary under ARM926 emulation: TLS 1.2/1.3, obsolete-version rejection, PIN/cookie separation, Host/Origin checks, forwarded-header rejection, profile CRUD/keep/replace/clear/redaction, certificate persistence/reset, expiry/corruption/symlinks, and incomplete-handshake connection limits/timeouts. Host OpenSSL success alone was not treated as ARM server acceptance.
- DOM tests cover HTTPS URL/port navigation, physical fingerprint acknowledgement before PIN entry, blocked HTTP secret forms, HTTPS editing, VPN formatting and background updates preserving editor/focus.

The final portal-only dependency is wolfSSL **5.9.4**, pinned and hash-verified. Both target webservers were rebuilt, static ABI checks passed, and the complete ARM926 HTTPS suite passed with this library. The outbound proxy dependency remains separate. The Mongoose path validator also includes the bounded-read advisory fix and a guard-page regression test.

ARM interoperability fixes were necessary: native wolfCrypt certificate generation replaces a stubbed OpenSSL-compatible extension constructor; the Mongoose wolfSSL server context loads its identity before SSL_new; server renegotiation information and extended master secret support preserve modern TLS 1.2 checks. No browser verification or outbound proxy TLS policy was weakened.

## Actual Baby results

Approved Radio: 192.168.1.78, Community Firmware 9.0.2 r17160, Linux 2.6.26.8-rt16. Port 443 was free. The existing hostname is SqueezeboxRadio. Clock and entropy were inspected; the persistent configuration uses the firmware-managed `/mnt/storage/standalonebase -> /etc/standalonebase` alias. TLS directory and certificate/key modes are 0700 and 0600, owned by root. The key was never exported or printed.

A temporary-port private server was tested first. Direct LAN HTTPS profile creation, credential keep/replace/clear, malicious-config rejection, deletion, redacted responses, HTTP 426 for plaintext provisioning, and HTTP 401 for replayed HTTP tokens all passed. Only a synthetic disposable profile was changed; it was deleted. Both pre-existing Proton US/NL profiles remained configured. No working provider credentials were read or re-entered.

The tested server and assets were then installed on the normal ports. The web process alone was restarted. The same certificate fingerprint was observed across both web-server replacements, and Jive's process identity remained unchanged. Temporary test listeners were stopped. Current user entry point: **https://192.168.1.78/**. A final Host-validation optimization was also deployed with a web-only restart, preserving the same certificate. Public certificate verification with a local client trust context succeeded for both the IPv4 SAN and the case-insensitive hostname SAN; `.local` name resolution itself is not claimed as verified.

Direct HTTPS authentication/profile operations and a Virtual Radio session/frame succeeded on the installed port-443 server. In the earlier installed HTTPS build, existing US and NL profiles were also connected and switched through HTTPS: verified public IPv4 results were 149.22.84.136 and 185.132.133.128, respectively. Their traffic objects were available and session IDs changed. The original selected profile and disconnected state were restored; no provider credentials were re-entered. Controls remain subject to existing physical firmware authorization. API tests and same-origin asset checks do not establish an interactive browser mixed-content or certificate-warning acceptance result.

The development harness authenticated the TLS endpoint using a fingerprint obtained independently through the approved SSH/native capability channel, before sending its test PIN. This proves the direct encrypted provisioning transport. It does **not** replace the required normal user test of comparing the browser certificate with the physical LCD and then selecting Secure Web Pairing Code.

## Resource measurements

Final wolfSSL 5.9.4 Baby static web-server file: **866,168 bytes**, compared with **337,032 bytes** for the previous binary (increase 529,136 bytes). Fab4 is also 866,168 bytes. No new background daemon is installed; the temporary acceptance server was removed from service.

| Window | Duration | Web CPU | RSS | Traffic |
| --- | ---: | ---: | ---: | --- |
| Idle HTTPS-capable listener | 5.001 s | 1.200% | 1,284 KiB | No dashboard polling |
| HTTP status polling | 13.028 s | 1.612% | 1,284 KiB | Six requests, one connection |
| HTTPS status polling | 13.093 s | 1.451% | 1,292 KiB | Six requests, one TLS connection |

Initial TLS handshakes were excluded from those polling CPU windows. The final library uses portable P-256 acceleration and restricts inbound key-exchange groups to P-256. An independent single-handshake sample measured **0.819 seconds wall time and 0.31 seconds web CPU**. Final TLS 1.2 and TLS 1.3 acceptance handshakes took 0.8563 and 0.918 seconds, respectively; the polling connection opened in 0.7727 seconds. The separate polling harness's initial-connect CPU field was inconsistent with its wall interval and is excluded; the dedicated sample above supplies the handshake CPU evidence.

These are short sequential samples from a warm process after the provisioning/frame checks, not a controlled estimate of negative/zero overhead or a cold-listener memory comparison. Framebuffer capture leases had expired before sampling. An earlier high-CPU window overlapped capture and included handshake CPU in an unsuitable polling denominator; it is not used to infer polling overhead. Host validation resolves the accepted local address once per connection instead of enumerating interfaces on ordinary requests.

The first unrestricted-group wolfSSL 5.9.4 build required approximately 1.9 to 2.0 seconds of handshake CPU and had intermittent request disconnects during the hardware check. Restricting the test client to P-256 reduced CPU to 0.28 seconds; the final server enforces that group. The complete direct-LAN profile/credential/HTTP-rejection/Virtual Radio frame acceptance then passed without those disconnects. The final ARM suite also rejects a P-384-only peer.

Audio playback was not started for this measurement; the user had stopped KINK and previously waived additional VPN playback measurements. A controlled audio/HTTPS load test and physical Touch acceptance remain unverified.

## Remaining acceptance and recovery

The updated Lua source was loaded by the approved reboot. Physical confirmation of the Device Certificate and Secure Web Pairing Code menu behavior is still pending. The task explicitly prohibits Jive restart or reboot without approval. Earlier VPN work observed an unexpected reboot during a Jive restart. The maintenance README explicitly says its primitives/coordinator are unqualified and production mutations are Fab4-only; do not use them as a Baby restart procedure. The approved normal firmware reboot exposed the delayed entropy recovery issue described below.

Interactive Chrome, Edge and Firefox certificate inspection/exception acceptance has not been exercised. Their official documentation was reviewed; mobile certificate-viewer/exception limitations are documented. No automatic CA installation, shared root CA, public DNS/cloud dependency, or global browser security bypass is part of the workflow. The HTTPS page blocks PIN entry until the user acknowledges comparing the full physical fingerprint.

The approved boot exposed low startup entropy; the final retry build still needs cold-boot acceptance. HTTPS fails closed on insufficient entropy or an unset clock, while HTTP remains available. Interrupted identity creation/rotation may require advanced local recovery; normal upgrades never rotate identity. HTTPS browser sessions currently require pairing again after web-server restart. Fab4 is build-compatible but has no physical acceptance result.

Original executable/Lua/assets and a mode/hash manifest are retained under ignored `.local/vpn/https-device-backup/`. Rollback restores those files atomically and restarts only the identity-checked web process with its original arguments. Do not remove or export persistent TLS keys or VPN storage. No public release should be advertised as fully accepted until the physical fingerprint/PIN/browser workflow is completed.

## Private packages

Private candidates are under ignored `.local/vpn/candidates/`; baseline 0.7.3 filenames are for internal acceptance only and must receive an approved release version before publication. Archive verification excludes device TLS identities, credentials and runtime profiles. The final packages include wolfSSL 5.9.4 provenance and the upstream TLS license. Verification compared assets and all eight executables with the build outputs and rejected device identities/runtime profiles.

| Target | Bytes | Files | SHA-256 |
| --- | ---: | ---: | --- |
| baby | 3180465 | 61 | `42545b40bb6717ceced2005f3c4ab1ee0183ab268a451c7804f7a8fb12a15d09` |
| fab4 | 3087762 | 59 | `48dca29e2143e122b07bfbacbef6e0a58db58df4e1e1288fac8160064614aee0` |


### Approved reboot follow-up

The Radio returned to its normal home screen. The first startup reported HTTPS unavailable while HTTP, Jive and the VPN manager ran. Low credited kernel entropy was observed, and the persistent certificate/key permissions remained intact. The check also encountered an interruption and changing startup process identities; the cause of that transient was not established. No second reboot was requested.

This exposed a missing delayed-start recovery path. The private server now retries secure initialization every five seconds and checks the credited entropy count before attempting to read 128 bits. Focused host checks and the actual ARM socket suite pass, including recovery without a process restart. The final full host regression passed **148/148** in **338.08 seconds**. Final cold-boot acceptance of the retry build remains pending.

At the final read-only state check, the boot had remained up for over ten minutes, HTTP was available, the public leaf still matched the pre-reboot fingerprint, and both original profile IDs retained configured credentials. HTTPS was still waiting at 126 credited bits; physical menu interaction and browser trust/PIN acceptance are pending. Do not describe the current secure listener as available until recovery is observed.

The existing VPN startup limitation also reproduced: the firmware TUN module was absent after reboot. Loading the already installed module with `/sbin/modprobe tun` and refreshing capabilities restored `available=true` / `DISCONNECTED`. No kernel or firmware was installed or changed, and no provider connection was started. Automatic TUN activation remains a separate pre-existing production gate.

### Physical menu and delayed-start recovery confirmed

The user confirmed that Device Certificate displays the HTTPS address and all four fingerprint rows. The subsequent check found HTTPS available automatically with the same pre-reboot fingerprint; credited entropy was 185 bits. Direct LAN HTTPS returned 200 for the portal, secure.js and vpn.js, and rejected unauthenticated profile provisioning with 401. Both original profiles retain configured credentials. VPN now reports CONNECTED; no disconnect or provider switch was requested by this validation.

This confirms recovery on the already booted Radio after physical menu interaction; it does not prove that every unattended cold boot will supply sufficient entropy. Interactive browser certificate comparison, site exception and physical secure-PIN pairing remain pending user confirmation. No additional reboot was issued.

### User browser acceptance confirmed ? 16 November 2026

The user confirmed: ?Fingerprint matches; paired; VPN visible.? This establishes the manual comparison of the complete browser certificate SHA-256 fingerprint with the physical Radio display, successful physical Secure Web Pairing Code authentication on direct LAN HTTPS, and access to the VPN page. The browser product/version was not recorded; this result is not a cross-browser compatibility matrix. No VPN password or pairing PIN was shared in the development conversation.

Together with the direct encrypted synthetic-profile/credential acceptance, this validates the normal browser provisioning path without an SSH tunnel. Actual provider credential replacement through the user browser was not separately confirmed in this final check; the direct HTTPS automated hardware test covered replacement while preserving both real provider profiles.

Remaining production gates: unattended low-entropy cold-boot behavior, automatic activation of the firmware TUN module, physical Touch acceptance, and representative audio/HTTPS load testing. The approved boot recovered HTTPS automatically after physical menu activity, retaining the original certificate. This is a successful Baby user-workflow acceptance, not blanket production qualification. No commit, push or publication is authorized by this acceptance.
