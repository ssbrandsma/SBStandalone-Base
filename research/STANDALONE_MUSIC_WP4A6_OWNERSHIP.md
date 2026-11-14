# WP4a.6 — Exclusive decoder ownership (DRAFT)

## Invariant (implemented + live-verified)

At any moment at most one application drives the physical decoder.
Enforcement points:

- SBOWN1 bridge (base side): grants owner==4 (Music) only from a
  positively idle, fenced state; heartbeat + incarnation tracking.
- Fence flag (Jive side, build 8+): tracks the GRANT, not the HTTP
  stream object. Jive slurps a loopback track in ~6 s (FIN + DSCO,
  `pbStream=nil`) then decodes from buffer for minutes; builds ≤7
  cleared the flag on HTTP disconnect (wrong). Build 8 holds while
  granted and releases after >3 s decoder idle (`releaseIdleGrant`);
  the flag clears on owner-loss with decoder idle.
- Command path: base `sb_transport_owned_command` gates strm-p/u/q
  on owner/epoch/connection match (fail-closed; no log on reject).

## Live evidence (tonight, build 8)

- 5 music plays, 0 radio/Spotify takes (ring + decode + owner traced).
- Grant hold observed every play: `musicActive=true owner=4 decS=1`
  with `pbStream=nil` (e.g. epoch 19, header `a4b4d07b…`).
- `RELEASE-IDLE owner=4 idle>3s` fired after epochs 15, 16, 18
  (ring timestamps 19924219, 20874033, 21836385); owner→0, flag
  cleared (`grant-lost`), decode idle. Epochs 17/19 released cleanly
  (owner=0 idle / clean radio-takeover sequence).
- Bridge incarnations 14→15 (one SlimProto flap reconciled, no flap
  storm; 0 unexpected reconnects during steady play).
- Radio takeover observed only AFTER music released (epoch 19 drain →
  q → release → 538 `_begin`): benign, correct order, fence
  surrendered via `adapterHold` (`client=false`, `owner=nil`).

## Grant race + hardening (builds 9/10, staged)

Every play shows one transient `FLAG-OFF reason=disconnect`: the
disconnect backstop runs before the first snapshot sync (snapshot
still ungranted). It self-healed within one tick every time
(`updateGrantFlag` re-latches; no spurious release sent — fail-safe).
Build 9 closes the window: ungranted + decode-busy HOLDS (re-arm)
in both the disconnect backstop and the 5 s tick backstop; added
foreign-owner surrender (native ownership moved on → reap even if
busy) so a busy foreign decoder cannot wedge the fence.
Host proof: new Phase O (`O1` failed before, passes after; `O2` idle
clear; `O3` sync-keeps-flag; `O4` moved-on reap).

## Morning verifications

- Build-10 live: no `FLAG-OFF` transients during play (ring).
- Foreign-surrender live: music release → radio owns → flag down
  promptly (fence.status `owner` + `musicActive`).
- Post-flap generation alignment: pause/user-stop verdict decides
  whether `sb_owner_controls` mismatches after incarnation bumps
  (see TRANSPORT).

## Files

`applet/PlaybackFenceV6.lua` (M.BUILD=10 staged, 8 live),
`tests/music/test_playback_fence.lua` (Phases K–O),
`.local/wp4a6/tonight-20261110/{fence.status,fence.streams,strm_timeline}.txt`.
