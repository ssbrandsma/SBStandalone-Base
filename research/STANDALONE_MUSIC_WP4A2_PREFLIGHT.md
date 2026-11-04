# WP4a.2 actual Fab4 configuration and preflight

Touch192.168.1.244, firmware7.7.3 r16676, kernel2.6.26.8-rt16-332-g5849bfa, ARMv6,125848KiB RAM, no swap. Original Base0.6.7. Known-host SSH worked; SFTP was unavailable. USB replacement resolved the initial Wi-Fi-NIC/no-storage blocker.

STORE N GO `/dev/sda1`, vfat, mounted rw at `/media/sda1`, original mount20,8:1, about365812KiB free. Mount IDs are session-local; the later reboot re-enumerated storage, so repeated numerical IDs are not continuity proof across boots. No USB format, deliberate remount, eject, audio-file replacement or unknown USB code execution was performed. The reboot necessarily re-mounted storage through normal firmware startup.

Only the selected `/media/sda1/mp3/Fleetwood Mac - Rumors-1977-FAF` folder was scanned:11 MP3s,3 JPEGs and1 NFO. Native scanning ignored unsupported files and used no-atime relative reads. No whole-stick scan or native `.Squeezebox` write was requested. Native persistence used the explicitly approved, private <=8MiB tmpfs `/dev/standalone-music-test`; no internal-flash fallback was used. It was removed after the scan and all temporary RAM state was gone after recovery.

A later preflight found active Spotify bridge/supervisor/librespot despite Base's inactive metadata. The user confirmed Spotify was playing and explicitly authorized stopping it. The installed Stop Spotify Connect UI was used and processes disappeared. ALSA RUNNING and Base stopped were never substituted for decoder proof.

Stock SqueezeCenter startup on mounted storage was identified before the Jive probe. The temporary inhibition depended on a RAM marker and failed across an unexpected reboot; TinySC started. This was a failed safety gate, not successful preflight qualification. The processes were stopped and the resulting USB `.Squeezebox/cache` directories were left intact. See recovery report; do not claim TinySC stayed off throughout the task.

Original three native-service hashes, Base configuration hash and four affected Meta hashes matched backup after recovery. Current GUI/native services and normal watchdog are restored. Future reboot safety with attached USB remains unqualified.
