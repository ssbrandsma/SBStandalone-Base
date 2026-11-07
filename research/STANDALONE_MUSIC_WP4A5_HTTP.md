# WP4a.5 — HTTP: resource IPC and streaming

## Architecture (unchanged, verified end to end)

Base↔web pair over an `AF_UNIX SOCK_SEQPACKET` socketpair passed through the
`/var/run/sb-music-ipc` rendezvous at startup. Fixed 136-byte `SBM4` wire,
monotonic sequences, SCM_RIGHTS fd passing for RESERVE/REGISTER. Exchange per
load: `RESET` (probe) → `RESERVE`+fd → `ACTIVATE`, each ACKed; teardown
`RESET`s after terminal. The decoder fetches
`/music/stream/<token>` from the music web (:8766) with MP3 MIME, range
support, bounded buffers; capabilities revoke on stop/terminal.

## This session's evidence

- Pairing + bind proven repeatedly: `listening → binding → bound`, real
  `:8766` LISTEN, instant HTTP 404s. (The earlier "bound but no listener"
  verdict was the `2246` hex typo; corrected checks pass.)
- Standalone full-sequence peer (`rvpeer2`, musl/ARM): RESET/RESERVE/ACTIVATE
  all ACKed status 0 on-device — web and channel exonerated in isolation.
- Attempt-2 load fully traced both ends: `SEND op=3/5/6` + `RECV ACK` at
  seq 1/2/3, then decoder HTTP 200 (kind=3 event), ~33 s of playback.
- Added permanent capability-safe trace logging: `MUSIC_IPC_SEND/RECV/
  RECV_ERROR/RECV_INVALID` (shared), `MUSIC_IPC_DISCONNECT why=…` (web),
  `MUSIC_IPC_TIMEOUT op/seq/now/sent` (base). No tokens logged. EAGAIN never
  logged (tick-rate safe).

## Pre-existing quirks (noted, untouched)

`GET /` hangs on both music and production webs (shared handler); 404 paths
answer in ~0.1 s. No impact on streaming paths.
