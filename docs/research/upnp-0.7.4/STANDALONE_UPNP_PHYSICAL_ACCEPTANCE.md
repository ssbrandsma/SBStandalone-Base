# Owner acceptance: Baby and Fab4

These are local candidates, Base 0.7.4-rc1 / UPnP 0.3.0. Read the license and
installer qualification reports first. Neither target is unconditionally ready:
the Baby module source hold and independent post-boot recovery qualification
remain open. This checklist is for the owner after those gates are resolved.
No physical device was contacted, package installed, original UI observed,
audible playback confirmed or Linux 2.6 runtime verified during this assignment.

## Preparation and installation

Record the actual model, firmware, active applet path, installed Base version,
available storage and current service health. Back up the old applet, catalog,
Base settings, favorites and Connect settings without printing credentials.
Keep an independent firmware recovery route and verified old package. Do not
initialize Extended Storage, change flash layouts or enable TinySC as part of
this test. Leave renderer disabled initially. Remove no existing user data.

Baby: use only the `baby` ZIP and matching XML. Current activation qualification
is LogitechMX25Baby / 7.7.3 r16676. Other firmware must remain gated. Fab4: use
only the `fab4` ZIP; retain existing local storage and TinySC arbitration settings.
The Fab4 archive intentionally has no Baby kernel module or empty UBIFS image.
The current network activation gate requires 7.7.3 r16676 on both targets and
existing qualified TinySC arbitration on Fab4. If that gate is absent, record
ordinary Base/web fallback; do not enable or bypass it solely for this test.

An older adapter needs the backed-up Lua-only bootstrap followed by the normal
firmware lifecycle before it can stage a full native update transaction. See
the installer report. The legacy SSH full-package deployment is blocked for
network packages. Do not work around that guard. The new stock adapter commits
only verified complete target packages and invokes stock completion; allow
that normal completion to finish. Do not manually restart Jive, replace running
executables, launch a second decoder or treat files-on-disk as loaded Lua proof.

After normal activation, verify the About/menu version, target marker,
`network-music.compat.json`, actual process executable identity and bounded
supervisor status. Use the existing authenticated web interface locally; never
paste private `/var/run` tokens, passwords or Connect credentials into reports.
Verify old Radio/Spotify/Qobuz configuration before starting music.

## Baby navigation and playback

1. Open **My Music**, choose a NAS, browse its own folders and select an album.
   Confirm discovery is asynchronous and the original UI stays responsive.
2. Play a supported numeric-LAN HTTP MP3. Confirm original cover, title, artist
   and artwork, and independently listen for actual sound. The host byte fixture
   is not evidence of sound. Confirm volume and physical pause/resume/Stop.
3. Use original Now Playing playlist navigation. Source inspection identifies
   `go` / `go_now_playing_or_playlist` calling `showPlaylist`; record the actual
   knob/button gesture on this firmware rather than assuming a new custom menu.
4. Load a container of at least 137 direct playable children. Scroll past rows
   50 and 100, select a later row, observe the audible/current highlight and
   natural next. Open the original context menu: Play, Remove, Move Up/Down,
   Clear. Moving an inactive row must not restart audio; stale views must not
   operate a new provider. Stop must retain the nonempty queue; Clear shows empty.
5. Let a final track complete; confirm actual stopped state. Reopen Now Playing
   and the playlist without track-change notifications pulling focus away.

## Fab4 navigation and local/network checks

Repeat the same workflow using the original Touch Now Playing/list control.
Verify the physical screen gesture and context menu on the device. Also play a
known local item with existing storage, then network, then local again, using
the existing My Music actions. Confirm one decoder, correct queue identities,
highlight/revision, natural-next and no stale stream after Stop/source changes.
Do not infer physical mixed-queue success from the native mixed-type unit test.

The expanded Baby applet is about 8.1 MB in member bytes; conservative same-size
staging/rollback planning exceeds 20 MB before other applets and reserve. A fresh
Baby with insufficient existing writable capacity must reject the update. This
candidate does not automatically initialize or migrate Extended Storage.

## Both targets: providers, failures and optional renderer

Start Spotify, Qobuz and Radio through their existing controls. The playlist may
show the real current track/station only; no invented future queue should appear.
Changing Connect tracks must preserve playlist focus. Native old row actions
must fail after source takeover. Shuffle/repeat remain disabled where unsupported.
Verify standby, alarm preemption and return to manual playback without an
ownership bypass. Disconnect/reconnect the NAS, change its content generation,
cancel browsing and stop during loading; no old request should start later.

Finally enable **Standalone Base → Network Playback** explicitly. Use one LAN
UPnP controller to prepare an admitted MP3, Play/Pause/Stop, volume/mute and GENA
updates. Preparing a URI alone must not start audio. Active foreign ownership
and standby must reject conflicting Play. Disable the renderer and verify it
withdraws and stops its own session; restart must not restore a prepared URI.

Record package SHA-256, firmware, exact steps, timestamps, heard result, current
UI row and sanitized errors. Unsupported hostname/HTTPS/chunked/redirect streams
should fail clearly, not hang or fall back to an arbitrary decoder URL. For
missing sources, verify NAS discovery/network first. For unavailable playback,
check ownership, idle fence and resource compatibility before retrying. On
repeated startup failures, verify ordinary Base/web fallback. Stop testing on
unexpected restart or storage changes and preserve the journal and old tree.
Do not delete rollback slots or attempt an unqualified live rollback.
