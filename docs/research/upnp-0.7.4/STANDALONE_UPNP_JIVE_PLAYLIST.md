# Original Jive playlist integration

The reference is SqueezePlay commit
`cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d`: SlimBrowserApplet, DB,
NowPlayingApplet and Player. Native status accepts the original
`status FROM QUANTITY menu:menu useContextMenu:1` request and subscription tags.
`count` is the total queue size, `offset` and `playlist_cur_index` are zero-based,
`item_loop` is the requested page and `playlist_timestamp` is the queue revision.
The original DB converts the index to its one-based UI representation.

Native local and network tracks share the existing bounded 256-entry queue.
Rows carry stable queue entry identities, context and revision. Selection,
removal, move and clear call that queue; stale identities/revisions and retired
providers are rejected. Move preserves the playing identity and stream epoch.
No Lua-side queue or second playback/ownership manager was introduced. A stopped
nonempty queue remains browsable; clear returns the original empty playlist.
Pause retains the current row. Natural completion advances only after native
stream terminal events. Selecting another row requires the existing idle fence.

The production fixture exercises 137 actual SOAP children, pages 0/50/100 and
the original 200-row request, later-page selection, context actions, move,
stale-action rejection, removal, native natural-next, pause/resume, stop and clear.
The original DB.lua is loaded under Lua 5.1 (only Jive's `!=` syntax is translated)
to verify all 137 rows and current highlight. Mixed local/network identity and
mutation are covered by native queue tests; physical mixed-source audio is untested.

External metadata providers expose only the actual current item. Spotify's
pinned client does not enable queue events; Qobuz's available adapter publishes
current metadata; Radio represents the actual current station. Their queues
are not synthesized. Selection/remove/move/clear are absent, and next/previous
reflect published capabilities. Source/generation/real track or station identity
qualify the presentation. Native rows cannot operate a newly active foreign
provider. Unsupported shuffle/repeat use original disabled styles and no-op
commands, including the original NowPlaying remap guards.

Renderer playback uses the same native queue/context and replaces its admitted
prepared context, rather than inventing an album. Ownership/standby gates remain
authoritative. It is disabled by default and does not restore a previous URI.

Original Now Playing cover, artwork widgets and playlist navigation remain in
use. No original Jive files are patched. Spotify now opens Now Playing once per
presentation session and preserves playlist focus on later track metadata.
Original Baby `go` / `go_now_playing_or_playlist` and Touch playlist actions still
call the original `showPlaylist`. Exact physical gesture behavior and audible
playback require the owner's acceptance test; host callbacks are not that evidence.
