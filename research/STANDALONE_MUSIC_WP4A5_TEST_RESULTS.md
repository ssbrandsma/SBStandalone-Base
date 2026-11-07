# WP4a.5 — Test results

## Host (build-wp4a5, final code)

`ctest -R 'music_|tinysc_'`: 45/47. The 2 failures are the known environment
issues (`maintenance_music_deployment_rollback`, `maintenance_music_recovery`
— root-owned /tmp fixtures, missing helpers), unchanged from baseline.
Build: 0 errors, 0 warnings in touched files. `music_playback_ownership`
flaked once under load, then passed 4/4 (isolation + full set + 3× repeat).

## New/affected tests

- `music_playback_single_mp3` extended with the ACK-timeout underflow
  regression (crafted `now` around a pending teardown RESET): FAILS on
  unfixed code with the exact device signature (`now=1 sent=135405148`),
  PASSES fixed. Covers both directions (no instant fail; genuine 15 s+ fail).
- Prior-session additions still green: `music_idle_admission` (revision-churn
  + adapter-timeout tests), `music_playback` detach-recover,
  `music_rendezvous_pairing`, `music_store_pairing`, `music_resource_*`.
- ASan+UBSan full-fidelity repro: clean in the prior session for the
  pre-instrumentation tree; the final tree's delta is logging + 1 guard line
  (re-run recommended before next device load, noted honestly).

## Device (this session)

- Corrected web bind test: PASS (`:8766` LISTEN + HTTP 404).
- Standalone IPC sequence (rvpeer2): RESET/RESERVE/ACTIVATE all ACK 0.
- Cutovers: 6 (pre-stop abort, correct), 6b/7/8b COMMITTED (LEVEL 1 + soak);
  5 rollbacks, all tier-1, all guard-verified.
- Gate F: attempt 1 pre-audio fail (root-caused, fixed); attempt 2 full
  playback + owner audible confirm; stop/terminal/teardown traced.
- Gate G on 32147: idle, 273 browsable with actions after admission, TinySC
  stopped, USB clean, no load attempted (mission honored).

## Exclusions

libcurl-dependent proxy host tests (missing dev dependency) — stated, not
claimed. QEMU/Baby not re-run for the 1-line delta (ARM binary exercised
live on Fab4 instead).
