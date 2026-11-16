# Secure Standalone Base portal

The secure portal uses the existing web interface over HTTPS. VPN profile uploads and credentials can be configured directly from a paired browser; SSH is an optional recovery tool.

## First use

1. On the physical device open **Standalone Base > Web Access > Device Certificate**. Note the HTTPS address and the full SHA-256 certificate fingerprint.
2. Open that HTTPS address in a desktop browser. A self-signed certificate warning is expected for a previously unknown device. Inspect the **certificate presented by the browser**, before entering any pairing code. Compare every SHA-256 group with the physical device display. Continue only when all groups match. A fingerprint displayed by the HTTP website is not independent identity verification.
3. If your browser provides a site-specific certificate exception, accept it only after that comparison. Do not install a shared root CA, disable certificate checking globally, or accept an unexpected identity change. An expired certificate or hostname mismatch must be investigated separately.
4. On the physical device select **Secure Web Pairing Code**. On the HTTPS page confirm that you compared the certificate, then enter that code. HTTP pairing codes and HTTP browser sessions cannot authorize HTTPS provisioning.
5. Open **VPN**, choose **Add profile** or **Edit / replace credentials**, upload the provider's validated inline `.ovpn` configuration and enter its OpenVPN service credentials. Existing profiles need no re-entry. Keep/replace/clear password operations retain their existing meaning.

HTTP remains available for compatible ordinary management and native Jive APIs. Its **Open Secure Portal** link opens the same device over HTTPS. The HTTP VPN page does not open a credential form. It never redirects an already submitted secret-bearing POST. No root password, terminal or additional client software is needed for normal HTTPS provisioning.

## Browser limitations

Firefox desktop provides a certificate viewer from its certificate warning and site-information panels; compare the SHA-256 fingerprint, not a SHA-1 fingerprint. Chromium desktop browsers, including Chrome and Edge, provide certificate details, but warning-page actions and exception availability vary by browser version and enterprise policy. Some mobile browsers do not expose a usable full fingerprint or site-specific exception. Use a desktop browser that exposes the full presented certificate rather than skipping verification. Actual interactive Chrome/Edge/Firefox/mobile acceptance is still required; implementation tests are not browser-trust acceptance.

The certificate contains the device's existing hostname with `.local` where applicable, localhost, and IPv4 addresses present at creation. A SAN does not make a self-signed certificate trusted. A stable existing local hostname avoids DHCP-IP changes when local discovery resolves that name. Name resolution and address collisions depend on firmware/network configuration; no new DNS/cloud service is installed. If an address changes, prefer the stable hostname. If you use the new IP and the browser reports a name mismatch, first verify the current address and every fingerprint group against the physical screen; a browser with site-specific exceptions can retain that verified device identity without changing its key. Browser support varies. Reserve certificate reset for explicit recovery, not routine DHCP changes.

## Trust options considered

A desktop browser with a full presented-certificate viewer and a site-specific exception is the supported first-use approach. It needs no terminal, Radio root password, client installation or public DNS. A mobile browser without that viewer cannot safely complete the same comparison; use a suitable desktop browser for provisioning. Existing HTTP remains available for ordinary non-secret management.

Automatically installing a CA, distributing a shared root, or requiring public DNS/ACME/cloud issuance was rejected. Manual operating-system trust-store installation is more invasive than a site exception and is not required. A QR code alone cannot authenticate the certificate presented by the browser. No replacement cryptographic protocol was added to bypass a browser warning. Actual desktop-browser usability remains a release gate.

## Recovery

If HTTPS is unavailable, HTTP management remains available and LAN credential writes remain blocked. Check the clock, secure storage and entropy; a missing/incomplete/expired/corrupt identity is reported rather than silently replaced. An incomplete first creation or interrupted rotation requires advanced recovery before retrying.

**Reset Device Certificate...** is an advanced physical menu action with a separate confirmation. It changes identity and revokes browser sessions; verify the new full fingerprint and pair again. VPN profiles and credentials are preserved. Do not use certificate reset as a routine update step. The identity is stored separately from package assets and ordinary backup files, so updates preserve it.

HTTPS pairings currently last up to one day in memory and require pairing again after webserver restart; the old HTTP remembered-browser behavior remains available for ordinary HTTP management. The SSH loopback portal remains supported for advanced provisioning/recovery, with its original paired-browser requirement. No plaintext LAN provisioning override exists.

Sources: [Firefox certificate viewer](https://support.mozilla.org/en-US/kb/secure-website-certificate), [Chrome connection verification](https://support.google.com/chrome/answer/95617), [Edge certificate inspection](https://learn.microsoft.com/en-us/microsoft-edge/devtools/security/), [Firefox Android exception limitation](https://bugzilla.mozilla.org/show_bug.cgi?id=1813868), [wolfSSL keys and certificates](https://www.wolfssl.com/documentation/manuals/wolfssl/chapter07.html).

### HTTPS waiting after startup

On legacy firmware the device clock or kernel entropy pool may not be ready when the webserver starts. HTTP remains available and sensitive LAN HTTP writes remain blocked. Secure initialization retries every five seconds without changing the device certificate. Use the physical controls to open Web Access and try Device Certificate again after a short wait. If it remains unavailable, follow the recovery checks above; do not reset the certificate just to bypass an entropy or clock problem.
