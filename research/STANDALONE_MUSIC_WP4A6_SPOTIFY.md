# WP4a.6 — Spotify coexistence (DRAFT)

## Status: preserved, stopped, non-interfering

- Installations intact: `/usr/share/jive/applets/Spotify/` and
  `SpotifyConnect/` (incl. librespot + ogg-http-bridge) untouched.
- Daemon state: `State: stopped`, no librespot/spot process all night.
- No Spotify producer activity during any of the 5 music plays or the
  radio session (no callbacks, no decode takes, no bridge events).
- Fence Spotify wraps (`wrapSpotify`) active; nothing fenced (nothing
  attempted). Host adapter tests unchanged (suite green).

## Carrier attempt (tonight)

SpotifyConnect was tried as an alternate install carrier
(`scripts/wp4a6-fence-v6c.py`) because the ImageViewer menu is
portal-unreachable. The splice was luac-verified as a full file and
uploaded, but opening Spotify Connect did NOT fresh-load the applet
(already cached) so the carrier never fired. The pristine file was
restored IMMEDIATELY with md5 verification
(`567236868e4bf1e9424b874846e998c3` before == after). Spotify menu
opened normally afterwards (Manual/stopped). Zero footprint remains;
receipt records splice + restore.

## Morning

No Spotify action required. Optional (not acceptance): start Spotify
from its menu after music releases, confirm it plays, stop it —
only if time permits; cross-application handoff stress is out of scope.
