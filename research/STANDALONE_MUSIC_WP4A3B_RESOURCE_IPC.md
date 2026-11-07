# Production resource IPC

Implemented locally; actual production webserver tested on host and ARM/QEMU. Physical activation was refused and the files rolled back.

The session creates the pair before fork/exec; only each designated child inherits its own endpoint. No public descriptor-adoption or arbitrary-path route is introduced. Base uses the existing WP6a root descriptor and WP4 scope tuple, opens the source through existing bounded workers and transfers a validated descriptor over the private channel. The existing webserver registers opaque, expiring capabilities, validates media/owner/stream generations and supports revocation/reset. Root and source descriptors retain CLOEXEC protections.

`music_production_resource_ipc` exercises the production `--music-ipc` entry, not the fixture flag: registration/activation/revocation, GET/HEAD, 64-bit Range (including sparse files beyond 4 GiB), bounded buffers/backpressure and concurrent streams. Startup additionally sends a genuine RESET and waits for ACK before readiness without creating a stream or starting a decoder. Missing ACK cannot advertise playback.

HTTP delivery is transport evidence only. Actual Fab4 IPC/readiness/capability observations will be recorded separately from audible confirmation.

## Final hardware outcome

The new session never started. Fab4 still runs its original web PID 3312 and browse Base PID 3933. No qualification IPC pair, resource capability or decoder GET was activated. Production HTTP support is host/ARM/QEMU tested only; it is not active on the device after rollback.
