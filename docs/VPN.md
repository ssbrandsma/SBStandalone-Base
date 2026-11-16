# Standalone VPN user guide

This development feature adds a VPN tab to the paired web portal and a Standalone VPN menu to the Standalone Base applet on Radio (Baby) and Touch (Fab4). Hardware acceptance is pending. It requires firmware-provided OpenVPN 2.5+ in the 2.x series, usable TUN, root network privileges, and suitable firewall/resolver support. Stock firmware without these capabilities remains usable with VPN unavailable. The applet does not install OpenVPN, load modules, or upgrade firmware.

## Provision a profile securely

1. Obtain a generic **OpenVPN client / TUN / IPv4** profile from your provider. Download the provider's **OpenVPN service credentials**; these may differ from its website password. Both certificate-only and service-password authentication are supported. Certificates, unencrypted private keys, and optional tls-auth/tls-crypt material must be inline in the `.ovpn` file. External file paths are rejected.
2. Open **Standalone Base > Web Access > Device Certificate** on the physical device. Open its HTTPS address in your browser and compare every SHA-256 fingerprint group in the browser's presented certificate against that screen before accepting a site-specific exception. See the [secure portal guide](SECURE_WEB_PORTAL.md) for browser limitations and recovery. Never blindly dismiss a warning.
3. Select **Secure Web Pairing Code** on the physical device and pair the HTTPS browser. Existing HTTP pairing does not authorize secure provisioning. No SSH tunnel is needed.
4. Select **VPN > Add profile**, choose the `.ovpn`, and enter its name and country. Provider, city and description are optional labels. For service-password profiles, enter the service username, select **Replace password**, and enter its password. Select **Validate and save**.
5. Select the profile and **Connect / switch**. CONNECTING is a request in progress; CONNECTED appears only after OpenVPN and the manager confirm the tunnel, routes and requested DNS policy.

The importer reports unsupported directives rather than running arbitrary provider scripts. It does not support TAP, provider plugins, nested configs, proxies, encrypted private keys, compression, IPv6 transport or custom routes. Ask your provider for a plain modern OpenVPN TUN profile. Do not remove certificate verification to bypass an import or connection error. See the developer document for the precise supported subset.

## Daily operation

The ordinary paired LAN portal can connect, disconnect, switch existing profiles, change settings, validate saved profiles and display sanitized diagnostics. Editing, replacing credentials and deleting profiles require the paired HTTPS portal or advanced SSH loopback recovery. Passwords and private configurations cannot be read back through either API.

**Keep existing / no password** preserves the saved password during edits. **Replace password** replaces it; **Clear stored password** removes it explicitly. A blank username during a browser edit keeps the existing username. Certificate-only profiles do not need a service password.

Under **Standalone Base ? Standalone VPN** on the device, use Enable/Disable VPN, VPN Location, Connect on Startup, and VPN Status. Location groups existing profiles by configured country, with a second selection when that country has multiple profiles. Profile provisioning remains in the browser. Both interfaces display the same backend state. After selecting a new location while connected, the manager switches the tunnel.

Enable **Connect on startup** only after a successful manual connection. **Automatic reconnect** retries transient failures with bounded backoff. Authentication failures require an explicit retry after correcting credentials. Startup is tied to the existing Jive supervisor; it does not protect traffic earlier in boot.

## DNS, IPv6 and kill switch

Keep **Use VPN DNS** enabled. The manager uses server-pushed IPv4 DNS or the explicitly configured fallback VPN DNS address. Without either, connection fails rather than silently using ISP DNS. Fallback DNS must be an address reachable through the tunnel, outside connected LAN prefixes and distinct from the server endpoint. Resolver changes made by DHCP are tracked and restored appropriately on disconnect.

The optional **Kill switch** requires usable IPv4 firewall support, VPN DNS enabled, and literal IPv4 server addresses in every remote entry. Hostname endpoints are refused for this mode because a scoped bootstrap DNS exception is not implemented. While VPN is requested, it blocks public physical IPv4 egress during connection, tunnel loss, retries and profile switching. Explicit Disconnect removes the restriction. LAN, SSH, portal access, connected local networks, IPv4 multicast and link-local traffic remain available under the existing system firewall policy.

When IPv6 exists, public IPv6 is blocked while VPN is requested; connected local IPv6, link-local and local multicast remain usable. IPv6 is not tunneled. If the necessary IPv6 firewall is unavailable, VPN is unavailable rather than claiming protection. This is a client for this device, not a router for other devices.

The dashboard distinguishes configured country labels from independently verified location. After each successful tunnel connection, the manager automatically checks public IPv4 through api.ipify.org using the existing TLS-validating HTTPS proxy. **Refresh Public IP** requests another bounded check, with a minimum 60-second interval for that tunnel generation. Status polling does not poll the external service. The dashboard shows verification state, last successful timestamp, age and sanitized errors. A failed lookup leaves VPN connectivity unchanged. Switching or reconnecting immediately clears the previous tunnel's verified IP and location.

**Optional location lookup** is off by default. When enabled, the manager sends the independently verified public IP to ipwho.is over validated HTTPS. That service may receive and log the IP; no profile credentials are sent. The response must name the same IP. Country code/name, region and city are approximate IP-based information and may be unavailable. Results are cached for one hour per IP in a bounded eight-entry memory cache; external location attempts are at least 60 seconds apart. A new IP may wait for this cooldown. Disabling the option cancels pending lookup and clears displayed location. Restart clears the cache and rate limits.

## Live traffic

The top cards display received/sent session totals and current receive/transmit rates using decimal B, KB, MB and GB. These are actual managed TUN-interface bytes, not physical-interface traffic, encrypted wire overhead or a bandwidth speed test. The manager reads counters directly every approximately two seconds; the page reuses the status API at that interval and advances connection duration locally. Background updates preserve open profile editors and focus.

A new explicit Connect, profile switch or manual Reconnect starts a new accounting interval. Automatic retries retain accumulated totals where possible. Disconnect freezes the last completed session and sets live rates to zero. All accounting is in memory: manager restart starts a disclosed new interval. Missing samples, counter resets and interface recreation can leave small gaps; this is operational telemetry, not billing-grade accounting. No traffic history is written to device storage.

## Disable and recover

Select **Disconnect**, then turn off **Connect on startup**. Disconnect intentionally restores normal networking and removes the VPN kill switch. The settings do not erase profiles. Disconnect and disable startup before uninstalling; preserve the manager and runtime journal until recovery succeeds.

If the portal is unavailable, SSH to the device and run the installed manager as root, using the actual applet installation directory:

```sh
/path/to/StandaloneBase/sbvpn --recover
```

This asks a running manager to disconnect and disable startup. If the manager is offline, it locks the runtime directory, verifies recorded process identity, and restores only its owned routes, resolver contents and firewall chains. Exit status zero indicates successful recovery. It preserves unrelated firewall rules and routes. `sbvpn --ctl status` displays redacted status; `sbvpn --capabilities` probes runtime support.

Do not delete the runtime journal before recovery: it identifies owned changes and the previous resolver state. Invalid/corrupt journals are retained and may require manual inspection by a developer; the tool refuses a broad network reset. VPN profiles contain secrets at `/mnt/storage/standalonebase/vpn` (0700 directory, 0600 files). They are intentionally excluded from ordinary configuration backups and diagnostics. A firmware or applet upgrade should preserve this directory; keep a separate secure copy of provider material if needed.

## Acceptance still required

Real Radio and Touch connection, provider compatibility, actual public Internet egress and DNS leaks, discovery, audio continuity, CPU/RAM and throughput must be checked before production release. See [delivery report](VPN_DELIVERY.md) and [readiness review](VPN_READINESS_REVIEW.md). The development Baby now runs 9.0.2 r17160 with OpenVPN 2.5.11 and a packaged TUN module. The supported manual activation is `/sbin/modprobe tun`, followed by Refresh capabilities in the VPN page; activation is currently required again after boot. Its kernel has NETFILTER disabled, so the kill switch is unavailable and correctly refused. The user provisioned credentials and confirmed both Proton profiles work. Automatic public IP/location, profile-switch reset and frozen disconnect totals have now passed on the Baby. Controlled ordinary/HLS playback through the tunnel and comparable performance measurements remain pending manual station operation. An unexpected device reboot occurred during the Jive restart step and requires lifecycle investigation. See the readiness review for the complete current state.

## Advanced SSH recovery

If HTTPS cannot be used, an administrator can optionally establish an encrypted loopback forward:

```sh
ssh -N -L 127.0.0.1:8088:127.0.0.1:80 root@RADIO
```

Open `http://127.0.0.1:8088`, pair using the ordinary HTTP pairing code, and provision through the SSH-encrypted transport. Keep the forward bound to 127.0.0.1. This is an advanced recovery path, not the normal setup workflow. Direct LAN HTTP provisioning remains rejected.
