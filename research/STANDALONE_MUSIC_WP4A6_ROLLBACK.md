# WP4a.6 — Rollback (DRAFT)

## What changed on the device (tonight)

- `/usr/share/jive/applets/StandaloneBase/PlaybackFenceV6.lua`:
  build 7 → 8 (live, `4cc7256ae356…`) → 10 staged on disk
  (`9e9f34ec64fa`, NOT installed — install needs ImageViewer tap).
- `/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua`:
  v6b carrier splice (staged, unchanged since build-8 deploy).
- `/usr/share/jive/applets/SpotifyConnect/SpotifyConnectApplet.lua`:
  spliced then RESTORED pristine, md5-verified
  (`567236868e4b…` before == after). Zero footprint.
- Nothing else: no native binaries, no DB, no music files, no
  radio/Spotify behavior files, no Jive restart, no reboot.

## Rollback procedures

- Fence (any bad behavior): delete the marker
  `/var/run/standalonebase/music-fence.enabled` → all fence
  instances go passive (wraps pass through, no bridge writes);
  or restore `PlaybackFenceV6.lua` from `/root/wp4a6-backup/`
  and re-open ImageViewer. Receipt: `/root/wp4a6-fence-receipt.json`
  (extended with v6b/v6c steps + md5s; copy in
  `.local/wp4a6/deploy/receipt.json`).
- Radio/Spotify: pristine (Spotify md5-verified). Radio applet
  carries PRE-EXISTING device-only WP4a.6 patches (not made
  tonight) — do not "restore" them away; exported to
  `.local/wp4a6/tonight-20261110/radio-*.lua`.
- Base/web/Jive/watchdog: untouched tonight; no rollback needed.
- Worst case: device reboot restores stock behavior (fence is
  file-based, marker-gated; base versioned artifacts intact).
  NOT authorized without owner consent; never needed tonight.

## End state

Standby, fence idle (build 8), radio suspended silently, base
LEVEL 1, TinySC stopped, USB untouched. Safe to leave; safe to
wake (power button) and continue in the morning.
