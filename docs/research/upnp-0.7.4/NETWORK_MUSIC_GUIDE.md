# Network music: operation and integration

## Player operation

After installing a qualified Standalone Base package containing Network Music,
open the existing **My Music** menu. Discovered MediaServers appear by their own
friendly names. Two servers may have identical names; their UDN identities remain
distinct. Open a server to follow its native folders and categories. The player
does not invent an artist/album hierarchy or index the remote library locally.

Search is offered only when the selected container and server advertise support.
Available fields and sorting come from the server's capabilities. Default order
is the server's own order. Additional pages load asynchronously; leaving the
window cancels its outstanding requests. Server disappearance or a changed
generation invalidates old requests. Reopen the server after it returns.

Select a supported MP3 for Play, Add or Play Next. Container actions collect
playable direct children into a bounded queue edit; they do not recursively
descend into subfolders. Collections exceeding 256 returned objects are rejected
without a partial edit. Local and network items share the existing Music queue,
Now Playing display and physical controls. Playback remains subject to decoder
ownership, standby and alarm admission.

Under **Standalone Base → Network Playback**, enable the optional renderer and
choose its device name. External UPnP controllers can then prepare an MP3 URI and
request playback. Preparing a URI does not start sound. Play is admitted through
the same Base ownership path, and conflicting playback or standby can reject it.
Disable the setting to withdraw advertisements and stop renderer-owned playback.
The default is disabled; previous prepared URIs are not restored on restart.

Current supported input is numeric LAN IPv4 HTTP MP3 with an audio MIME type and
Content-Length. Hostnames, HTTPS, chunked audio and redirects are unsupported.
Seek and single-resource Next/Previous report standard faults. Physical firmware
qualification remains necessary before presenting these candidates as a release.

## Developer integration

The code is split across the UPnP service and Standalone Base:

| Boundary | Contract |
| --- | --- |
| Jive → Base | Authenticated `/api/network-music` on Base's existing loopback HTTP listener; async requests and cancellation |
| Base → sbupnp | Same-UID private Unix `SOCK_SEQPACKET`, `SBUPNP1`/`SBUPNR1`, fixed bounded requests and bounded JSON replies |
| Queue identity | Explicit local/network discriminant; network source, opaque object ID, server generation and separately stored URI |
| Base → web resource bridge | Existing wire v2 for files; negotiated wire v3 socket resource for remote compressed streams |
| Base → decoder | Existing scoped SlimProto commands and loopback opaque resource token; no arbitrary remote decoder URL |
| Renderer → Base | Same authenticated backend for prepare/play/control/state; no second decoder or ownership implementation |

Discovery callbacks enqueue bounded notices; description and SOAP work run away
from Base's UI/main loop. The daemon revalidates server generations after RPC.
Queue selection fetches metadata again, validates resource type and identity,
then commits through the shared queue. Container selection pins UpdateID across
pages and commits once. A later user action or Stop invalidates pending intents.

The resource worker opens the remote HTTP stream, validates its bounded headers,
then transfers a compressed TCP socket to the web bridge. Reads use existing
bounded jobs and backpressure; a complete track is never downloaded to RAM or
flash. Token expiry, reset, terminal events and explicit stop retire resources.
Natural completion advances the typed queue and fences late old-stream events.

The production network-only profile does not open SQLite or scan storage. Baby
activation checks the board and exact qualified firmware string; Fab4 activation
uses existing TinySC arbitration. Jive reuses PlaybackFenceV6. A marker created
by this profile is removed on cleanup or startup fallback; existing markers are
preserved. Repeated native startup failure restores ordinary Base/web services.

## Security and qualification

pupnp is pinned to 22.2.2. The linked XML wrapper applies the same length, depth,
node, UTF-8 and DTD policy to SDK SOAP/GENA parsing. Device descriptions and
service/resources use admitted numeric origins. Protected self-management ports
are rejected. GENA callback delivery is restricted to one URL on the subscribing
controller's own admitted private address. No DNS or redirect fetch path exists.

Build/test procedures and exact evidence are in the repository README and
`STANDALONE_UPNP_TEST_RESULTS.md`. Host controller and decoder simulations are
explicit test boundaries; they do not demonstrate audible physical playback.
Both services and Base runtime binaries are statically cross-built for Baby and
Fab4. QEMU validates CPU compatibility of service/core execution, not firmware
socket behavior, audio timing or watchdog interactions.

The optional Base package overlay is selected with `SB_PACKAGE_UPNP_ROOT`.
It preserves standard binaries, existing configuration and renderer-off defaults.
Do not overwrite a live executable: use existing immutable generation deployment,
executable identity checks and rollback. Complete the distribution notice/source
audit and deployment regressions before publishing. No public upload is part of
these build scripts.
