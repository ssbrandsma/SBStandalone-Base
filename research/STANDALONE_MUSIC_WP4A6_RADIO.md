# WP4a.6 — Radio coexistence (DRAFT)

## Baseline + drift

Standalone Radio preserved and working (538 Non-Stop via
playerservices.streamtheworld.com, 18+ min continuous play, VU live,
metadata updates). No Qobuz. No radio files modified by this session.

DRIFT FOUND (pre-existing, preserved, NOT reverted): the on-device
`/usr/share/jive/applets/StandaloneRadio/StreamPlayer.lua` carries 6
`WP4a.6:` annotations from a prior session (ownsPlayback gates in
`_scheduleReconnect`, `_handleDisconnect`, `_handleMetadata`,
`_audioRecoveryTick`). These exist nowhere in the repo. Exported to
`.local/wp4a6/tonight-20261110/radio-StreamPlayer.lua` (+
`radio-StandaloneRadioApplet.lua`). Morning decision: commit as
vendor snapshot or leave device-only (behavior depends on them).

## Fence wraps (code-verified)

V6 wraps `_handleDisconnect`, `_scheduleReconnect`, `_begin`,
`_notifyState`, `_audioRecoveryTick`, `_handleMetadata`,
`_resetAudioRecovery`, `stop`, `pause`: every path passes through
when ungated; fenced only when `gated()` (music active). V5's `stop`
wrap always passes through; V5 has no action watchers. V6 action
watchers (-50) never consume. Host: Phase N green.

## Live results

- Radio plays after music released: PASS (benign takeover traced).
- Radio pause ACTION (portal `pause`, == remote): FAIL — 3 presses,
  action reaches Jive (fence `lastAction` fresh each time), decode
  never dips (sampled +2/+6/+15 s). Station taps prove `stop()`
  itself works (decode idles), so the break is between the -100
  listener firing and `StreamPlayer:pause/stop` taking effect.
  Build-10 diag (`RPAUSE`/`RSTOP` ring entries + radio state in
  status) decides in the morning whether the methods are reached.
- Station tap on playing station: RESTARTS (stop + `_begin`), never
  stops. NP shows transient "Stopped" during resolve (~6 s CDN
  setup), then plays. There is currently NO portal-reachable radio
  stop (on-screen pause button is outside the portal touch zone;
  owner's finger untested).
- The 6 s "auto-resume": NOT a timer. Code audit proves no timer
  survives an explicit stop (`_cancelReconnect` in `stop()`;
  watchdog/audio-recovery gated on `playbackActive`/`ownsPlayback`,
  both false post-stop; `_handleDisconnect` early-returns on
  `reason=nil`). The +6 s CONNECT is the tap's own `_begin`
  completing. Resort state after REAL stop: unknown (no real stop
  executed); morning retest with on-screen button.
- desiredStation retained across stops (by design, §6); no resume
  observed without a fresh `_begin` trigger.

## Morning

1. Build 10 → radio pause → ring `RPAUSE`? `RSTOP`? → fix.
2. Owner-finger on-screen pause → radio stop verdict.
3. Post-stop 60 s watch → true auto-resume verdict (Test3).
4. Decide radio-applet drift disposition.
