# Radio deployment review ? 2026-11-13

Target: **192.168.1.222**, Logitech MX25 Baby Board, firmware9.0.2r17157.
Deployed the normal Baby StandaloneBase0.7.0 working-tree package, not the
experimental Music runtime. No other device was contacted for this deployment.

## Review findings corrected before deployment

- Shared Base HTTP pipelining stopped after its bounded64-request turn unless
  more network input arrived. The parser now schedules buffered work for a later
  loop turn. A real200-request pipeline regression failed before/pass after.
- The shared8-entry playerstatus subscription cache permanently refused new
  request identities once full. New subscriptions now replace an old cache entry;
  a20-identity regression failed before/pass after. Jive request/stream CID
  broadcasting and subscribe acknowledgements remain intact.
- New Comet transport tracing was unconditional. Per-receive/round/shift traces
  now require diagnostic mode, avoiding production log floods. Header lookup is
  confined to the current request's headers.
- Music's external ownership takeover could retain a completed stream's stale
  queue context. A real ownership regression exposed this and now passes after
  invalidating the context on an external owner. This Music coordinator is not
  activated in the deployed normal Radio build.
- Remembered-browser storage rejected the Radio's managed configuration alias
  `/mnt/storage/standalonebase -> /etc/standalonebase`. It now resolves the
  explicitly configured directory and validates/pins the resulting private
  directory. Credential-file symlinks/unsafe modes still fail closed. Tests cover
  restart, renewal, logout/revoke durability, eviction, faults and the alias.

## Validation

- Host CMake build passed.
- Relevant CTest selection:38pass,1skip,0fail (61.26s). The skipped Lua idle-fence
  case separately passed under Windows/Lupa against actual Radio/Spotify source.
- Baby static ARMv5TE/EABI/soft-float build and verify-arm checks passed. Existing
  image-render/vendor warnings remain; no new link dependencies are required.
- Exact production Baby Base passed the shared regressions underQEMU.
- Exact production Baby webserver passed remembered-browser tests underQEMU.
- ARM proxy fixture with the same current source/TLS libraries passed TLS/RSA4096
  and HLS/AAC/MP3 byte-delivery regressions underQEMU. Only this host fixture has
  explicit loopback fixture-port allowances; it was never included in the package.
- Host proxy-specific CTest targets are excluded because host libcurl development
  files are missing; the ARM/QEMU proxy results above are separate evidence.
- All packaged Lua files passed `luac -p`. Deployment package dry-run passed.

## Deployment and preservation

Original applet archive and resolved persistent configuration are backed up
privately under `.local/radio-222-review/`. The first config-root archive held
only its symlink; `config-contents-before.tar.gz` explicitly follows the configured
root via `tar -C` and holds the actual11-file backup.

Deployment used `scripts/deploy_dist.py --preserve-config`, strict-known-host SSH,
the existing atomic-copy workflow and the Radio's stock SqueezePlay restart.
This flag was added to avoid replacing user configuration with package defaults.
Only StandaloneBase was installed. Original Radio/Spotify Lua hashes are unchanged.
Jive restarted to load the larger pairing-code text; watchdogPID709 remained and
boot ID stayed unchanged. No device reboot or firmware/watchdog modification.

Post-install checks verify all50 package files, the live SHA-256 of exactly one
Base, webserver and proxy, Base0.7.0 health, Jive's Comet subscription, unchanged
persistent config.json and served remembered-browser checkbox. Experimental
Music/fence carrier modules are absent from the package.

The Radio reports NPO Radio2 playing MP3/192kbps with changing live metadata.
The owner subsequently confirmed **sound and the usual playback controls work**.
This supplements the instrumented playback evidence with physical confirmation.
No additional station
or synthetic playback was started by this deployment verification.

Rollback artifacts: `applet-before.tar.gz`, its SHA manifest,
`config-contents-before.tar.gz`, and its manifest. These are preserved locally;
no rollback was needed. A restore must use these exact backed-up files rather
than reconstructing the old installation from current source.

## Artifact receipt

PackageSHA256: `504728cf321a9961ee7abd93768f8eb84d938bcba89426ebae988e7c6251b07c`.

- sbbase: PID20406, liveSHA256 `8b9fb038413b5251863d8e370efae0c934042d6ebad91216613a51b2d210c75b`.
- sbwebserver: PID20414, liveSHA256 `e81f7f8c425c18c24cace33c3d7fdb87c3cbbbe904e7950d15021f7b3dc12774`.
- sbproxy: PID20398, liveSHA256 `8ec063ec36993918cb17c7a7eaf1e07dd6f1316fd6d576cfcec7c88cd3ff5ebf`.

Raw commands/results/manifests/backups: `.local/radio-222-review/`. No commit, tag or publication was performed.
