# WP4a.2 validation and failures

- Full host suite96/96 passed,425.99s; earlier95/95 baseline275.33s.
- Focused storage/ownership/production-entry checks4/4 passed,2.17s.
- Baby and Fab4 production-session cross-builds produced static ARM EABI5 binaries, no INTERP/NEEDED dependency.
- QEMU arm926/Baby and arm1136/Fab4: production fixture-option, incomplete activation and unapproved root refusal/capability cleanup checks passed.
- Privileged host private mount namespace: exact private8MiB DB placement and real mounted/scoped media traversal indexed MP3/FLAC fixture tracks, checked distinct IDs/integrity and clean unmount.
- Actual Fab4 scan-only native runtime:11 MP3 tracks,0 errors, SQLite integrity ok; RAM cleanup passed.

New tests cover no-atime descriptor inheritance/FD stability, Music reconnect requiring a fresh connection and native resource proof, fixture CLI rejection, explicit activation, ordinary-directory refusal and failed-startup capability cleanup. Host/QEMU positivity is not firmware release proof. New runtime ASan and full positive queue/audio QEMU were not run in this phase.

Music remains OFF in normal binaries and no production startup caller was added. No new listener, audio decoder, Perl/LMS dependency, artwork subsystem or ALSA setting was introduced. The optional manual session is not qualified for deployment.

Hardware qualification FAILED: one unexpected reboot during decoder-probe Jive restart; volatile inhibition disappeared, stock TinySC and resizer started, and USB `.Squeezebox/cache` was observed. Recovery restored original Meta/native/config hashes, original services, UI and watchdog; TinySC was stopped. Radio recovery logs show actual stream/decode/metadata. Audible recovery is reported separately. One custom-stations UI request was unavailable despite unchanged config. Spotify/Qobuz, standby, volume and optical/analogue regressions remain unqualified.

Final recovery observation: NPO Radio1 was stopped through the existing long-pause/Stop action. Final Base metadata is inactive/stopped; native services and Jive remain running, TinySC/Music are absent, and the test RAM volume is absent. The temporary authenticated remote session was logged out and its local cookie removed. The owner replied after the stop that nothing was playing and Now Playing was empty; this matches the stopped end state and does not confirm sound during the earlier sample. Radio audible recovery remains unverified.

Build evidence detail: the first ARM wrapper's trailing manifest stage failed while its script was being edited, after all three Fab4/Baby executables had compiled and ELF checks ran. Final scanner/archive/Base refresh completed with exit0 for both platforms and recorded final hashes; existing web/launcher source had not changed since their successful compilation. The final public wrapper passes shell syntax validation. Do not interpret this as an independently repeated end-to-end public-wrapper run after the manifest fix.
