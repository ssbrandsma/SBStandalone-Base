# Radio standby recovery fix

The Radio at 192.168.1.222 briefly resumed audio while showing its standby clock. Logs correlated both bursts with Standalone Radio's stalled-stream watchdog reconnecting. Base rejected the firmware request `["power","0",false,"seq_no:78"]` and advertised hardcoded power/sequence state.

Base now accepts the firmware power request, queues a producer stop on power-off, and echoes current power, volume and sequence in player/server status. Older sequenced commands cannot reverse newer state. Power-on does not automatically start playback.

Standalone Radio checks `jiveMain:getSoftPowerState()` before watchdog recovery, decoder underrun recovery, power keepalive, reconnect scheduling and stream start. Standby clears pending intent and invalidates callbacks. Asynchronous connection/header callbacks cannot start audio while off. The last station is retained for an explicit later Play. Radio only stops a decoder it owns.

Tests: 39 host regression tests passed; the WSL Lua-dependent idle-fence test was skipped there and passed separately on Windows. All 24 Radio Lua suites passed. The new standby fixture also passed against the patched exact installed Radio source. Static Baby ARM build and ARM/QEMU standby and shared-radio Comet tests passed.

Private deployment evidence and immediate pre-fix backups are under `.local/standby-fix/`. Only the installed Radio StreamPlayer is patched; other development Radio changes are excluded. Hardware observation does not substitute for user confirmation that standby remains silent.

Deployment status: files installed on 192.168.1.222 and one normal SqueezePlay restart requested. Firmware subsequently reported a startup stall in skin image loading and SSH became unavailable. Services briefly appeared with new PIDs, but full hash/status verification and standby observation remain incomplete. Do not treat this as hardware-qualified until device access and UI health are restored. No reboot or watchdog-disable command was sent. Immediate pre-update backups are available.

Follow-up: owner reported working Now Playing and the new DHCP address 192.168.1.225. The relocated Radio was verified against its previously trusted SSH key. All 50 package files, three live native executable hashes, the Radio standby patch, unchanged other Radio files and preserved configuration were verified. Base health is OK and live station/track artwork metadata is present. Boot ID changed to `13ac716e-c255-48aa-818c-3d411efa1303`, confirming a reboot occurred after the startup stall. User confirmation of silence during an extended standby interval remains pending. No additional deployment or restart was performed during this follow-up.
