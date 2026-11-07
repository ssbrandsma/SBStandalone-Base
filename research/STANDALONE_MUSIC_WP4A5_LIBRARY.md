# WP4a.5 — Library: full catalogue browsing

## Published library (live, verified this session)

273 tracks / 54 albums / 16 artists, revision 21, served from the USB snapshot
by every committed playback base. Counts confirmed via CometD `browselibrary`
(mode tracks/albums/artists) on bases 13052, 12920 and 32147.

## Browse validation

- Artists/albums/tracks/genres/folders modes respond with stable IDs
  (track 46 = 99 Luftballons on every query path).
- Pagination (50/page over 273), album-track ordering, Unicode titles
  (e.g. De Pre Historie sets) all observed in harness captures.
- Play actions (`go`/`play` → playlistcontrol load + queue_context) appear if
  and only if the ownership adapter holds a live idle admission: the glue
  computes `ready = idle_admitted && tinysc_allowed && (current==MUSIC ||
  (baseline_idle && bridge_ready))` and gates `playback_codecs` on it.
  Admission→action propagation takes seconds on-device (event-loop cadence);
  the Gate F harness polls for actions with adapter refresh instead of
  assuming instant visibility.
- The 11-track legacy database (`/dev/standalone-music-test/library.db`) is
  preserved byte-identical and still served by tier-1 rollback bases.

## Codec masks

Track `codec_mask` derives in SQL from `tracks.codec` (mp3→1). All 273 live
rows are `mp3`. Player mask comes from the SlimProto HELO (`codecs=7`,
fab4/7.7.3). Mismatch yields silent `itemNoAction`, never a fabricated action.
