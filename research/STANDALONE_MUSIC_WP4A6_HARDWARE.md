# WP4a.6 — Hardware record (DRAFT)

## Device + identity

- Squeezebox Touch Fab4, fw 7.7.3 r16676, MAC 00:04:20:22:92:c1.
- Address is DHCP-unstable: `.244` (mission) dead; `.248` dead;
  `.241` live all night (SSH + portal). Use `SB_DEVICE_HOST`;
  consider a DHCP reservation.
- Runtime identities (unchanged 21:15 → 01:40, no restarts):
  Jive 773, jive_alsa 805/806, watchdog 640, sbproxy 844,
  sbwebserver 861, sbwebserver-mus 10903, sbbase-music.61 10907.
- Base LEVEL 1 intact (no native redeploy; `/proc/10907/exe` +
  stage `/dev/sbmusic-wp4a6.M9tEF8` untouched).
- TinySC STOPPED (absent from ps all night). No duplicate listeners:
  127.0.0.1:9000 (base HTTP), :3483 (SlimProto), :8765 (proxy);
  0.0.0.0:8766 (music web), :80/:22 (system).

## Library + storage

- 273 tracks / rev21 browsed repeatedly (Artists/Albums/Tracks);
  no rescan performed or needed. USB user files untouched; DB
  writes confined to dedicated storage (no flash writes observed).
- 5 tracks played to drain; malformed Dr Dre file never selected.

## Portal driving (method note)

- Session (`/tmp/vr_client.txt` + cookies) valid all night;
  `stap.sh`/`srrc.sh` (8 s settle) added for lag (2–15 s input
  delay; screenshots lag reality ~5–10 s — SSH used as ground
  truth throughout).
- Remote vocabulary (complete): home, pause, play, power, back.
  Touch: ~60% first-try, bottom-row dead zone (y>~210: transport
  buttons unreachable), right-edge hits scrollbar.
- Photo screensaver/viewer appeared once (phantom); dismissed by tap.

## End state (01:40)

Standby (dark clock), radio stream suspended silently (decode
1/0 — still connected, output muted; ~460 MB/night worst case if
left; owner stops in the morning via on-screen button). Fence
idle (build 8, flag down, client held for radio). No errors, no
reboots, watchdog fresh, Jive healthy.
