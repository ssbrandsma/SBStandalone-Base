# Standalone Base 0.7.3

0.7.3 includes the remaining repository functionality and WP4a.6 research materials that were outside the 0.7.2 storage release.

The Applet Repository page can add public XML repository URLs, upload static XML catalogs, rename sources, refresh URL sources, and remove sources. Source settings and cached entries persist separately in `applet-sources.json`; XML management does not rewrite the base `config.json`. Failed refreshes retain the last valid catalog. Base/manual entries take priority, and conflicting same-version XML packages are blocked. The installer filters model and firmware bounds and selects the newest compatible version.

Downloads use the public-address-guarded proxy, bounded redirects, size/time limits, SHA-1 verification, and ZIP validation. The yxml parser is pinned with its MIT license included in both packages. Catalogs are limited to eight sources, 64 entries per source, 96 imported entries overall, 256 KiB XML and a 59 KiB installer projection.

Safe dynamic Extended Storage validation from 0.7.2 remains in place, including complete swapped A/B layouts and 113-LEB 9.0.1 cramfs volumes. This release does not bypass the physical confirmation required for storage initialization.

The WP4a.6 reports and diagnostic tools are committed as research. Their documented failed pause path and unproven transport tests remain unresolved. Playback experiments are not enabled by the release package; the additional Lua diagnostic modules are dormant.

Validation covers repository CRUD, invalid XML and private URL rejection, conflicting packages, checked package downloads/cache, failed-refresh fallback, restart persistence, installer projection above ten entries, 200 KiB persisted XML, firmware bounds, numeric version selection, and stale snapshot fallback. Existing storage, repository merge/sync, deployment, installer, backpressure and lifecycle regression suites are also exercised, together with static ARM builds and QEMU checks for both targets.

Published metadata advertises 0.7.3. Embedded package configuration references the already published immutable 0.7.2 packages to avoid a self-referential ZIP checksum. The existing 0.7.2 artifacts and unrelated applet releases are preserved.
