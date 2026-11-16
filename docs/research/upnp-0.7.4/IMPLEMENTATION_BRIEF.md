# Codex — Complete Standalone UPnP/DLNA Network Music Implementation

## 1. Mission

Implement a complete, production-quality native UPnP/DLNA music solution for the Logitech Squeezebox Radio (Baby) and Logitech Squeezebox Touch (Fab4), integrated into the existing Standalone Base ecosystem.

The implementation must allow users to discover UPnP/DLNA Media Servers on their local network, browse their music libraries through the original Squeezebox **My Music** interface, search when supported by the server, and play music through the original SqueezePlay audio engine.

In addition, implement an optional UPnP/DLNA Media Renderer so that compatible smartphones, tablets and computers can send music to the Squeezebox using standard UPnP AV controls.

**This is a complete implementation task, from dependency selection and cross-compilation through production integration, automated tests, hardware validation where available, packaging and documentation.**

Do not stop after research, architectural analysis, dependency evaluation or a command-line prototype.

## 2. Autonomous execution

Execute the entire task autonomously in one continuous implementation effort.

The owner explicitly requests:

- No separate work-package approval cycles.
- No intermediate design approval requests.
- No routine confirmation questions.
- No stopping after dependency selection.
- No stopping after the first successful compilation.
- No stopping after discovery or browsing.
- No stopping merely because an initial implementation attempt fails.

Make reasonable engineering decisions independently.

Use the existing repository architecture, established safety contracts and documented project conventions as the primary source of truth.

If a particular implementation approach fails, investigate alternatives, resolve the issue and continue.

Internal milestones and automated acceptance gates are encouraged, but they must not become user-facing approval gates.

If a genuine safety-critical blocker prevents an operation on physical hardware, continue with all other implementation, simulation, packaging and validation tasks.

Do not bypass safety mechanisms or invent successful test results to satisfy the autonomous execution requirement.

The final report must clearly distinguish implemented, automated-tested and hardware-verified functionality.

---

# Part A — Existing Standalone Architecture

## 3. Project context

Inspect the current Standalone Base repository and all relevant existing Standalone applet repositories.

The ecosystem includes:

- Standalone Base.
- Standalone Radio.
- Standalone Music.
- Spotify Connect.
- Qobuz Connect.
- Native local HTTP streaming.
- Existing artwork proxy and image conversion.
- Now Playing publication.
- Native audio ownership and generation fencing.
- Existing Jive integration.
- Original SqueezePlay decoder.
- Native SQLite-backed local music libraries.
- Existing Baby and Fab4 cross-compilation infrastructure.

Standalone Music on Fab4 has already demonstrated:

- Local USB music indexing.
- Persistent SQLite library.
- Original Jive My Music browsing.
- Native MP3 playback through the physical SqueezePlay decoder.
- Native resource IPC and HTTP streaming.
- Ownership-aware playback control.

Review the current implementation and latest reports rather than assuming earlier work-package status is still current.

Relevant previous development includes:

- WP3a: SlimProto parsing and command handling.
- WP3b: Native Jive browsing.
- WP4: Queue and HTTP resource streaming.
- WP4a.5: First audible native MP3 playback.
- WP4a.6: Playback stability and exclusive ownership.
- Radio Stop, standby and unintended auto-resume fixes.
- Extended Storage safety and compatibility.

Reuse these implementations.

Do not create competing services, duplicate playback state machines or independent ownership managers.

## 4. Target hardware

### Logitech Squeezebox Radio — Baby

- ARM926EJ-S.
- ARMv5-class CPU.
- Legacy Linux 2.6.26-based firmware.
- Limited RAM and internal flash.
- Original Jive Lua environment.
- Original SqueezePlay audio engine.

### Logitech Squeezebox Touch — Fab4

- ARMv6-class CPU.
- Existing Standalone Base support.
- Original touchscreen Jive interface.
- Local USB/SD music support where available.

Determine actual kernel, libc, compiler and ABI compatibility using the existing toolchains and hardware inventory.

Do not assume that a generic modern ARM binary can execute on these devices.

## 5. Non-negotiable architectural requirements

The solution must:

1. Run directly on Baby and Fab4.
2. Require no external Logitech Media Server.
3. Require no TinySC or TinyLMS.
4. Require no Perl runtime.
5. Use the original SqueezePlay decoder.
6. Preserve the original Jive user interface.
7. Integrate with existing Standalone Base supervision.
8. Use the existing physical decoder ownership architecture.
9. Preserve Standalone Radio, Spotify Connect and Qobuz Connect.
10. Preserve existing local music functionality.
11. Remain lightweight enough for Baby.
12. Avoid unnecessary third-party dependencies.
13. Follow UPnP AV and DLNA standards rather than vendor-specific APIs.

Do not add:

- MPD.
- GStreamer.
- A second ALSA playback engine.
- Squeezelite as an additional playback engine.
- A complete local LMS installation.
- A new web-based music player.
- A duplicate audio-output pipeline.

Audio must be played through the physical Squeezebox audio output using the original SqueezePlay engine.

---

# Part B — Existing UPnP Implementations and Dependency Selection

## 6. Review existing implementations

Use the following open-source projects as technical references and possible reusable components.

### Lyrion Music Server

https://github.com/LMS-Community/slimserver

Relevant source:

https://github.com/LMS-Community/slimserver/blob/public/9.2/Slim/Plugin/RemoteLibrary/UPnP.pm

Inspect:

- RemoteLibrary UPnP discovery.
- ContentDirectory browsing.
- BrowseDirectChildren.
- BrowseMetadata.
- Search handling.
- DIDL-Lite metadata conversion.
- Audio resource selection.
- UPnP Media Renderer services.
- Interoperability workarounds.

Use this code primarily as a behavioral reference.

Do not introduce the LMS Perl runtime.

### Philippe44 LMS-uPnP

https://github.com/philippe44/LMS-uPnP

Inspect:

- Native UPnP control-point architecture.
- Existing ARMv5 and ARMv6 build targets.
- Device discovery and expiry.
- Network interface handling.
- SOAP and XML integration.
- Subscription and reconnect handling.
- Relevant memory-management fixes.
- Cross-compilation scripts.

Relevant dependencies:

https://github.com/philippe44/libpupnp

https://github.com/philippe44/crosstools

Important: LMS-uPnP operates primarily in the opposite direction from our intended DMP use case.

Do not attempt to integrate the entire bridge as a standalone music player.

### GMediaRender

https://github.com/hzeller/gmrender-resurrect

Inspect:

- UPnP Media Renderer device description.
- AVTransport implementation.
- RenderingControl implementation.
- ConnectionManager.
- GENA eventing.
- `output_module.h`.
- `output.c`.
- Existing output backend abstraction.
- GStreamer-specific dependencies.
- GLib dependencies.

Evaluate whether its protocol implementation or selected components can be reused with a SqueezePlay backend.

Do not require GStreamer.

### Upstream pupnp

https://github.com/pupnp/pupnp

Evaluate upstream pupnp as the preferred UPnP protocol library.

## 7. Select and qualify the protocol library

The preferred solution is a native C/C++ implementation based on pupnp/libupnp.

Evaluate maintained and security-patched versions that can realistically execute on the existing legacy Baby and Fab4 platforms.

Previous research identified pupnp 1.14.x and newer release branches as potential candidates.

Verify actual current release availability and source requirements.

Do not blindly select a release based on previous research or version numbers.

### Selection criteria

- ARMv5 compatibility.
- ARMv6 compatibility.
- Legacy kernel compatibility.
- Existing libc compatibility.
- Existing compiler compatibility.
- C/C++ language requirements.
- Threading requirements.
- Static versus dynamic linking.
- Binary size.
- Runtime RSS.
- UPnP Control Point support.
- UPnP Device support.
- SOAP support.
- XML/DIDL-Lite processing.
- GENA eventing.
- Security maintenance.
- Licensing and distribution obligations.

Try the most suitable maintained version first.

If the selected version cannot be built using the existing toolchain, investigate:

1. Existing supported toolchain alternatives.
2. Relevant portability patches.
3. Philippe44's cross-compilation approach.
4. Another maintained compatible release.
5. Another appropriately maintained lightweight UPnP library if necessary.

Do not select a known-vulnerable obsolete library merely because it compiles easily.

Do not upgrade the entire Squeezebox operating system to accommodate the library.

### Licensing

Verify the actual licenses of every reused component and dependency.

Keep required license notices and source attribution.

Do not copy GPL-covered implementation code into an incompatible project without addressing the resulting distribution obligations.

If directly reusing GMediaRender would create undesirable licensing or dependency requirements, implement the renderer service using pupnp and the UPnP standard instead.

Document the final library choice, exact revision and rationale.

### Required result

Produce reproducible Baby and Fab4 builds of the selected library and verify actual compatibility using available runtime environments.

Proceed immediately to implementation after selection.

---

# Part C — Native Standalone UPnP Service

## 8. Implement `sbupnp`

Implement a native service, tentatively named:

`sbupnp`

Use existing repository conventions if a different service name or location is more appropriate.

Suggested structure:

```text
native/sbupnp/
    src/
        main
        discovery
        device_registry
        device_description
        content_directory
        didl_parser
        media_resources
        search
        playback_adapter
        renderer
        av_transport
        rendering_control
        connection_manager
        eventing
        ipc
        diagnostics
    tests/
```

The service should be optional and supervised by Standalone Base using existing lifecycle mechanisms.

Prefer a separate process so that UPnP parsing or network failures cannot crash the complete Base service.

Avoid unnecessary duplicate infrastructure.

Reuse existing common native components for:

- Logging.
- Configuration.
- IPC.
- Lifecycle supervision.
- HTTP helpers.
- Authentication where applicable.
- Audio ownership.
- Now Playing.
- Artwork handling.

## 9. Capability separation

Implement two independently configurable modes:

### DMP — Digital Media Player

- Discover Media Servers.
- Browse external libraries.
- Search external libraries.
- Select audio resources.
- Play through SqueezePlay.

### DMR — Digital Media Renderer

- Advertise the Squeezebox as a UPnP renderer.
- Accept standard controller commands.
- Play selected network resources through SqueezePlay.
- Report actual playback state.

The DMP functionality is the primary My Music integration.

The DMR functionality is an optional background capability.

Neither should require a separate top-level UPnP menu.

## 10. Internal service communication

Use the existing Standalone Base and Jive IPC architecture.

Expose a small, bounded interface for:

- Service health.
- Discovered Media Servers.
- Server details and capabilities.
- Container browsing.
- Object metadata.
- Server-native search.
- Playback request.
- Queue operations.
- Transport controls.
- Renderer configuration.
- Current renderer state.

Do not expose an unrestricted arbitrary HTTP fetch or arbitrary decoder command endpoint.

Use existing local authentication and permission mechanisms.

Ensure every asynchronous request can be cancelled and associated with the correct client session and request generation.

Do not block the Jive UI while network operations are running.

---

# Part D — Media Server Discovery

## 11. Implement standard SSDP discovery

Use the selected UPnP library's native SSDP functionality.

Support discovery of UPnP Media Servers advertising a compatible MediaServer device type.

Discover their ContentDirectory service through the standard device description.

Handle:

- SSDP M-SEARCH.
- NOTIFY alive.
- NOTIFY byebye.
- Advertisement expiration.
- Device rediscovery.
- Device restart.
- IP address changes.
- Network reconnect.
- Multiple Media Servers.
- Multiple local network interfaces where appropriate.
- Duplicate advertisements.
- Different supported MediaServer service versions.

Respect advertisement cache lifetimes.

Avoid continuous aggressive discovery.

Use bounded discovery intervals and event-driven updates where possible.

Do not scan every IP address in the local subnet.

## 12. Device registry

Maintain a lightweight in-memory registry.

Identify servers primarily by stable UPnP device identity, including UDN, rather than IP address or friendly name alone.

Record:

- Stable server identity.
- Friendly name.
- Device type.
- Description URL.
- ContentDirectory service type and version.
- ContentDirectory control URL.
- Event subscription URL where applicable.
- ConnectionManager URL where available.
- Last seen time.
- Expiration time.
- Current reachability.

Resolve relative service URLs correctly against the device description and URLBase rules.

Handle two servers with the same friendly name without merging them.

Do not duplicate a server merely because its IP address changes.

Do not retain stale devices indefinitely.

Do not persist a complete server library.

---

# Part E — My Music Integration

## 13. User-facing menu design

**Do not create a top-level menu named UPnP, DLNA or Standalone UPnP.**

The user-facing functionality belongs under the existing:

**My Music / Mijn muziek**

Extend the existing My Music implementation rather than creating a competing music applet.

The menu should present available music sources.

Example on Touch:

```text
My Music
    USB Music
    SD Card
    Synology NAS
    MinimServer
    Music on PC
```

Example on Radio without local storage:

```text
My Music
    Synology NAS
    MinimServer
    Music on PC
```

Only display local music sources that are actually available and supported.

Display discovered external servers using their advertised friendly names.

The exact local source names must follow the existing Standalone Music UI conventions.

### Preserve existing functionality

- Existing local SQLite library browsing.
- Existing local USB/SD music handling.
- Existing My Music playback behavior.
- Existing favorites or other supported menu integration.
- Existing Jive localization.
- Original physical-button navigation on Baby.
- Touchscreen navigation on Fab4.

Do not remove My Music functionality when no UPnP servers are found.

Do not require a local music library to be initialized before network music becomes available.

On Baby, My Music must be able to show external servers even when Standalone Music has no local USB/SD source.

## 14. Music source abstraction

Introduce or extend a lightweight shared music-source abstraction.

Conceptually:

```text
My Music
    Music Source Registry
        Local USB/SD Provider
        UPnP Media Server Provider
```

The local provider continues using the native SQLite library.

The UPnP provider uses remote ContentDirectory services.

Both share the existing physical playback and ownership infrastructure.

Do not merge remote tracks into the local SQLite database.

Do not rewrite the existing local music architecture unnecessarily.

Do not make UPnP an obligatory dependency for local music.

### Source navigation behavior

- Display a stable source list.
- Update it when servers appear or disappear.
- Preserve the user's selected menu position where practical.
- Show a concise no-servers message when appropriate.
- Provide a lightweight refresh action if needed.
- Handle unavailable servers gracefully.
- Never freeze the UI while waiting for discovery or network responses.

---

# Part F — Server-Native Music Library Browsing

## 15. Core design principle

**The remote Media Server owns the library structure.**

The Squeezebox must display the hierarchy returned by the server using standard UPnP ContentDirectory functionality.

Do not reconstruct the server's library structure locally.

Do not create synthetic Artists, Albums, Genres, Composers or Playlists categories merely because the client expects them.

If the server provides these categories, display them.

If the server does not provide them, do not invent them.

Examples:

```text
Synology NAS
    Music
        Artists
        Albums
        Genres
        Folders
```

```text
MinimServer
    Albums
    All Artists
    Composer
    Date
    Genre
    Folder View
```

```text
Simple Media Server
    Music
        Folder A
        Folder B
```

All three should work through the same generic browsing implementation.

## 16. Standard ContentDirectory browsing

Implement standard actions:

- BrowseDirectChildren.
- BrowseMetadata.
- GetSearchCapabilities.
- GetSortCapabilities.
- GetSystemUpdateID where supported.
- Other standard discovery or capability actions when needed.

Start library navigation with the standard root object ID:

`0`

Use server-provided object IDs for subsequent browsing.

Treat all object IDs as opaque strings.

Never assume object IDs are numeric, predictable or stable across different servers.

Scope object identity to the Media Server identity.

Two servers may both use object ID `123`, but these refer to different objects.

## 17. Pagination

Use server-side pagination:

- StartingIndex.
- RequestedCount.
- NumberReturned.
- TotalMatches.
- UpdateID.

Use bounded page sizes appropriate for Baby.

Do not request an entire large music collection in one operation.

A typical initial page size may be 25 or 50 items, adjusted based on actual Jive behavior and performance testing.

Handle:

- Servers returning fewer items than requested.
- Empty pages.
- Changing TotalMatches.
- Server library changes during browsing.
- Invalid or inconsistent result counts.
- Missing optional metadata.
- Delayed responses.
- Request cancellation.

Avoid infinite pagination loops.

Do not retain all pages indefinitely.

Use a small bounded cache only if it provides measurable usability benefits.

## 18. Generic DIDL-Lite navigation

Parse returned DIDL-Lite objects.

Map:

- Container objects to navigable Jive menu entries.
- Supported audio item objects to playable tracks.
- Audio playlists or containers to the existing queue interface where supported.
- Unsupported image and video items to appropriate filtered or non-playable representations.

Do not assume every generic container contains audio.

A generic container may contain music deeper in its hierarchy.

Do not recursively crawl an entire server to determine whether a container eventually contains audio.

Preserve server-provided ordering by default.

Use server-provided titles and metadata.

Use meaningful standard object classes where available, but continue to support generic containers.

Handle valid vendor extensions by ignoring unsupported optional properties.

Do not introduce vendor-specific browsing logic unless required for a documented interoperability defect.

---

# Part G — Native Search and Sorting

## 19. Search must remain server-side

Implement Search using the standard ContentDirectory Search action.

Do not create:

- A local remote-library search index.
- A full metadata cache.
- A local database of external tracks.
- A recursive scanning fallback.
- Client-side searching across the entire server library.

### Capability discovery

Call GetSearchCapabilities.

Inspect the actual capabilities returned by the server.

Also inspect the relevant container's standard searchable attribute when applicable.

Do not assume that Search is universally supported.

Determine whether the current search scope can legitimately be searched.

Handle servers that advertise capabilities but reject unsupported search requests.

## 20. Search user interface

Add a localized Search entry to an external library only when appropriate server-side search support has been established.

The Search entry is a client-side action derived from the server's standard capabilities, not an invented server category.

Example:

```text
Synology NAS
    Search
    Artists
    Albums
    Genres
    Folders
```

Possible search scopes:

- All supported searchable audio items.
- Track title.
- Artist.
- Album.
- Genre.
- Other supported standard metadata.

Only offer fields that are supported by the server and that can be mapped correctly to the existing Jive text-input interface.

Do not hardcode a search field that the server does not advertise.

Handle wildcard search capability correctly if reported.

### Search execution

Use valid UPnP SearchCriteria expressions.

Correctly escape:

- Quotes.
- Backslashes where required by the grammar.
- User-entered special characters.
- XML content.
- UTF-8 text.

Do not allow malformed user input to alter the intended search expression.

Use server-side pagination for search results.

Where supported, use suitable audio-class criteria to avoid returning unrelated video or image objects.

Do not silently fall back to a complete library scan when Search fails.

Report unsupported search behavior gracefully.

## 21. Sorting

Discover GetSortCapabilities.

Preserve server order by default.

Only send SortCriteria supported by the server.

Do not implement a local global sort requiring the complete server library.

A simple optional sort UI is acceptable only where supported by the server and consistent with the existing Jive interface.

Do not fabricate sorting capabilities.

---

# Part H — Metadata, Artwork and Audio Resources

## 22. Standard DIDL-Lite metadata

Support relevant standard fields, including:

```text
dc:title
dc:creator
upnp:class
upnp:artist
upnp:album
upnp:genre
upnp:composer
upnp:originalTrackNumber
upnp:albumArtURI
res
res@protocolInfo
res@duration
res@bitrate
res@sampleFrequency
res@nrAudioChannels
res@size
```

Treat optional properties as optional.

Support multiple metadata values and artist roles where appropriate.

Do not fail an entire track because an optional metadata field is missing.

Use metadata from the selected server.

Do not download every audio file merely to extract ID3 tags.

Normalize metadata only as required by the existing Now Playing and Jive contracts.

## 23. Artwork

Reuse the existing Standalone artwork endpoint, image proxy and conversion infrastructure.

Support server-provided artwork URLs.

Handle relative URLs according to applicable standards.

Do not create a competing artwork proxy.

Do not create a permanent cache of every remote album cover.

Use bounded resource limits and the existing minimal-cache design.

Artwork failure must not prevent music playback.

Ensure artwork URL fetching follows the same network safety policy as other UPnP resources.

## 24. Audio resource selection

A DIDL-Lite item may contain multiple `res` elements.

Select resources using:

- Standard protocolInfo.
- Transport protocol.
- MIME type.
- DLNA profile information where available.
- Actual SqueezePlay codec support.
- Device-specific playback capabilities.
- Server-provided transcoded alternatives.

Prefer resources that can be played directly through the existing decoder.

For example:

- MP3 over HTTP.
- FLAC over HTTP when supported and qualified.
- Other supported audio codecs where verified.

Do not advertise or assume unsupported codecs.

Do not require the client to transcode music.

If the server offers an alternative MP3 resource for an otherwise unsupported source format, select it when appropriate.

Do not infer codec compatibility solely from filename extensions.

Handle missing or inaccurate MIME information conservatively.

Reject clearly unsupported transport protocols.

Do not attempt to play image, video or executable resources.

---

# Part I — Actual Music Playback

## 25. Reuse the existing SqueezePlay playback infrastructure

This is the most important integration requirement.

The selected remote audio resource must play through the original SqueezePlay audio engine and the physical Squeezebox speaker or audio output.

Reuse the existing Standalone Base/Radio/Music playback paths.

Evaluate whether SqueezePlay can directly consume the remote HTTP URL or whether the existing local proxy/resource mechanism is required for compatibility.

Prefer direct streaming where it is reliable and supported.

Use the existing local proxy when needed for:

- Legacy HTTP compatibility.
- Supported HTTPS handling.
- Redirect handling.
- Resource normalization.
- Existing security and ownership integration.

Do not introduce an unnecessary proxy layer if the decoder already supports the resource.

Do not download complete audio files before playback.

Do not buffer complete tracks in RAM or flash.

## 26. Shared playback adapter

Implement a reusable remote-music playback adapter.

Both DMP and DMR modes must use the same underlying playback adapter.

The adapter must integrate with:

- Standalone Base ownership.
- SqueezePlay decoder commands.
- Resource lifecycle.
- Current playback generation.
- Now Playing.
- Queue handling.
- Transport state.
- Standby and power-state handling.

Do not create separate audio producers for DMP and DMR that can conflict.

Treat UPnP as a distinct registered playback source under the existing shared ownership architecture.

## 27. Exclusive physical decoder ownership

All playback must respect the existing ownership manager.

Potential owners include:

- Standalone Radio.
- Standalone Music.
- Spotify Connect.
- Qobuz Connect.
- Standalone UPnP.

Requirements:

- Only the current qualified owner may issue decoder commands.
- Explicit user playback requests may initiate a supported handoff.
- Automatic reconnects may not silently preempt another owner.
- Stale callbacks may not issue commands.
- Ownership requests and acknowledgements must be correlated.
- Stop and release must follow actual producer and decoder state.
- Metadata ownership is not equivalent to physical decoder ownership.
- A timeout does not prove physical decoder release.

Do not weaken the ownership implementation to make UPnP playback easier.

If an existing ownership contract is insufficient for UPnP, extend it consistently rather than bypassing it.

## 28. Standby and unintended playback prevention

Reuse the existing Stop/standby lifecycle fixes.

UPnP playback must not unexpectedly restart after:

- Explicit Stop.
- Standby entry.
- Loss of ownership.
- Server rediscovery.
- Device reconnect.
- Network reconnect.
- HTTP reconnect.
- Renderer subscription renewal.
- Repeated discovery advertisements.
- Applet reload.
- Service restart.

Maintain a distinction between:

- Selected resource.
- Desired playback intent.
- Current playback state.
- Current owner.
- Power state.
- Stream generation.

Do not allow a remembered URI to imply permission to restart playback.

An authorized wake operation must follow the existing product power-state policy.

Preserve legitimate alarm functionality.

## 29. Native transport controls

Support through existing Jive controls:

- Play.
- Pause.
- Resume.
- Stop.
- Previous track.
- Next track.
- Seek when supported.
- Volume.
- Mute.
- Now Playing.

Use existing native transport commands and actual decoder capabilities.

Do not invent unsupported SlimProto or Jive commands.

Only expose seek where the resource and decoder path support it.

For HTTP resources, verify appropriate range or other supported seek behavior.

Do not claim successful seek merely because the UI position changed.

## 30. Queue integration

Reuse the existing native queue where appropriate.

Extend queue item representation to support an external resource without forcing remote tracks into the local SQLite track model.

A remote queue item may contain:

- Media Server identity.
- Server object ID.
- Resource URI.
- Selected resource properties.
- Available metadata.
- Known duration.
- Playback capability information.

Use a common abstraction rather than fake local SQLite track IDs.

Support:

- Play one track.
- Add one track.
- Play next.
- Add album or playlist contents where supported.
- Automatic next-track playback.
- Queue navigation.

Retrieve remote container contents in bounded pages.

Do not load enormous libraries or recursively enumerate unlimited nested folders into the queue.

Respect the existing bounded queue design.

For a selected album or playlist exceeding the immediate queue capacity, implement bounded lazy retrieval if consistent with the existing architecture, or clearly enforce the supported limit.

Do not promise unbounded queues.

## 31. Playback resilience

Handle:

- Media Server disappears.
- Selected resource becomes unavailable.
- HTTP error.
- Network disconnect.
- Server IP address changes.
- Resource URI expires or changes.
- Unsupported codec.
- Decoder error.
- Pause during buffering.
- Stop during buffering.
- Stop while paused.
- Natural end of track.
- Next-track transition.
- Owner generation change.
- Standby during playback.

Retry only while playback intent remains valid.

Use bounded retry policies.

Do not automatically switch to another owner or source.

Report failures in the existing Jive user-facing manner.

---

# Part J — Optional UPnP Media Renderer

## 32. Implement DMR mode

Implement a standards-based UPnP AV Media Renderer.

Use the selected UPnP library.

Reuse GMediaRender or LMS protocol behavior where technically and legally appropriate.

Do not require GStreamer, MPD or another audio engine.

Expose the actual Squeezebox as a renderer discoverable by compatible UPnP controllers.

The renderer should use a stable unique device identity and a configurable friendly name.

Example:

`Squeezebox Radio — Kitchen`

Use the existing device name where available.

## 33. Renderer user interface

Do not create a top-level Media Renderer or DLNA menu.

Expose renderer configuration under an appropriate existing Standalone Base settings section.

Suggested:

```text
Settings
    Standalone
        Network Playback
            Allow Network Playback
            Device Name
```

Use actual Jive settings conventions.

The renderer must be opt-in by default for new installations unless an existing approved product policy dictates otherwise.

When a controller initiates permitted playback, show the track in the original Now Playing screen.

## 34. Standard renderer services

Implement the appropriate standard MediaRenderer device description and supported service descriptions.

Required services:

- AVTransport.
- RenderingControl.
- ConnectionManager.

Support service versions and action contracts consistent with the actual implemented behavior.

Do not advertise unsupported actions.

### AVTransport

Implement relevant standard actions:

- SetAVTransportURI.
- GetMediaInfo.
- GetTransportInfo.
- GetPositionInfo.
- GetTransportSettings.
- GetCurrentTransportActions.
- Play.
- Pause.
- Stop.
- Seek when supported.
- Next and Previous where appropriate.
- SetNextAVTransportURI where supported by the actual queue behavior.

Only advertise actions that can be implemented correctly.

Handle unsupported actions using appropriate standard fault responses.

Do not report PLAYING merely because Play was accepted.

### RenderingControl

Support:

- GetVolume.
- SetVolume.
- GetMute.
- SetMute.

Map these to existing Squeezebox controls.

Do not allow a renderer request to interfere with the current owner's protected audio state outside the established policy.

### ConnectionManager

Publish actual supported sink protocolInfo.

Do not advertise formats the target device cannot decode.

Support required connection-information actions according to the selected service version.

## 35. Eventing

Implement standard UPnP GENA eventing where required.

Handle:

- SUBSCRIBE.
- UNSUBSCRIBE.
- Subscription renewal.
- Expiration.
- LastChange events where appropriate.
- Transport state changes.
- RenderingControl state changes.
- Resource changes.
- Playback completion.
- Playback errors.

Keep event state synchronized with the real SqueezePlay playback state.

Use bounded subscription counts, timeouts and event queues.

Do not allow stale subscriptions to accumulate indefinitely.

Coalesce frequent position-related events where permitted.

## 36. Renderer playback policy

A UPnP controller on the network must not be allowed to silently bypass existing playback ownership.

Treat SetAVTransportURI as preparation, not permission to start physical audio.

Play must follow the renderer admission policy.

Use the following safe default:

- DMR disabled: reject incoming renderer playback.
- DMR enabled, decoder idle, device awake: allow qualified playback.
- DMR already owns decoder: allow valid controller operations.
- Another application owns decoder: do not silently preempt it.
- Device in standby: reject unsolicited audio start unless explicitly allowed by the configured wake policy.
- Stale controller or playback generation: reject or safely reconcile the request.

If the existing architecture supports a qualified user-authorized cross-application handoff, reuse it.

Do not enable arbitrary LAN clients to take over Spotify, Radio or local Music playback without an explicit product policy.

---

# Part K — Security, Resource Limits and Legacy Compatibility

## 37. Network security

UPnP devices and servers are untrusted network inputs.

Implement appropriate controls for:

- Device description fetching.
- SOAP responses.
- DIDL-Lite XML.
- Audio resource URLs.
- Artwork URLs.
- Event callback URLs.
- HTTP redirects.
- Discovery traffic.
- Renderer requests.

### Required protections

- Bound XML document sizes.
- Bound XML nesting and object counts.
- Disable external entity resolution where applicable.
- Prevent XML entity expansion attacks.
- Validate supported URI schemes.
- Prevent arbitrary access to local privileged endpoints.
- Prevent requests to unrelated sensitive network destinations through the service.
- Apply a consistent LAN resource-origin policy.
- Revalidate redirect destinations.
- Bound request duration.
- Bound response size.
- Bound concurrent requests.
- Bound SSDP registry size.
- Bound subscription count.
- Bound event queue size.
- Handle invalid UTF-8 and malformed metadata safely.

Do not allow unrestricted `file://` or other unsupported local resource schemes.

Do not expose the renderer to the public Internet.

Do not request UPnP Internet Gateway port mappings.

Do not create router port-forwarding rules.

Bind advertised renderer services only to the appropriate local network interfaces.

Preserve the existing local authentication mechanisms for internal Standalone IPC.

## 38. Memory, CPU and storage requirements

Baby is a constrained embedded device.

Optimize for:

- Small native service footprint.
- Bounded allocations.
- Lazy browsing.
- Small metadata pages.
- Minimal caching.
- No complete remote-library indexing.
- Low idle CPU usage.
- Low background network activity.
- Small thread count.
- No additional audio decoder.
- No unbounded logs.
- No unnecessary flash writes.

Reuse the project's existing memory and storage conventions.

Do not require Extended Storage simply to provide basic UPnP browsing unless measurements prove that no suitable package footprint is achievable.

If optional Extended Storage can improve installation flexibility, integrate with the existing supported installation mechanism.

Do not create or resize flash volumes automatically.

## 39. Performance measurements

Measure:

- Binary size.
- Package size.
- Dependency size.
- Idle RSS.
- Discovery RSS.
- Browsing RSS.
- Search RSS.
- Renderer RSS.
- Steady-state playback RSS.
- Idle CPU.
- Discovery CPU.
- Browsing latency.
- Search latency.
- Playback start latency.
- Stop/release latency.
- Network traffic.
- Thread count.
- File descriptor count.
- Growth over repeated browsing and playback.

Use actual measured values.

Do not present unverified resource estimates as benchmarks.

Optimize material memory leaks and unnecessary copies.

Test repeated server discovery, disappearance and rediscovery.

Test large server libraries without loading the entire library.

---

# Part L — Automated Testing

## 40. Build validation

Build for:

- Host development environment.
- ARMv5 Baby.
- ARMv6 Fab4.

Use the established cross-compilation toolchains.

Verify:

- ELF architecture.
- ABI compatibility.
- Required dynamic libraries.
- Unsupported instructions.
- Runtime loader compatibility.
- No unexpected modern libc requirement.
- Kernel compatibility as far as practical.

Run QEMU validation where available.

Distinguish CPU emulation from actual legacy kernel qualification.

Do not assume that execution under a modern QEMU userspace proves compatibility with the original device firmware.

## 41. Implement a deterministic UPnP test server

Use an appropriate host-side test fixture or existing UPnP test server.

The fixture must expose real standard UPnP behavior, including:

- SSDP discovery.
- Device description.
- ContentDirectory.
- BrowseDirectChildren.
- BrowseMetadata.
- Optional Search.
- Optional sort capabilities.
- DIDL-Lite responses.
- Audio resources.
- Artwork resources.

Use synthetic or redistributable test media.

Include:

- A root with music, images and video.
- Artist containers.
- Album containers.
- Genre containers.
- Arbitrary custom containers.
- Nested folders.
- Playlists.
- Tracks with missing metadata.
- Tracks with multiple resources.
- Large paginated containers.
- A server without Search.
- A server with limited Search capabilities.

The test server is development infrastructure only.

It must not become a runtime dependency on the Squeezebox.

## 42. Discovery tests

Cover:

- One Media Server.
- Multiple Media Servers.
- Duplicate SSDP messages.
- Duplicate friendly names.
- Server IP change.
- Server expiration.
- Server byebye.
- Server reappearance.
- Unsupported device type.
- Malformed description.
- Relative control URLs.
- Multiple service versions.
- Network reconnect.
- Bounded registry behavior.

## 43. Browsing and metadata tests

Cover:

- Root browsing.
- Nested containers.
- Opaque string object IDs.
- Identical object IDs on different servers.
- Arbitrary server-defined menu categories.
- Multiple pages.
- Empty containers.
- Large collections.
- Partial metadata.
- UTF-8.
- Multiple artists.
- Album artwork.
- Multiple audio resources.
- Unsupported media resources.
- Invalid XML.
- Oversized XML.
- Invalid result counts.
- Cancellation.
- Server disappearance during browsing.

Prove that the implementation does not recursively index external libraries.

## 44. Search and sorting tests

Cover:

- Server without Search.
- Searchable container.
- Non-searchable container.
- Title-only capability.
- Artist capability.
- Album capability.
- Wildcard capability.
- Unsupported search field.
- Escaped special characters.
- Unicode search.
- Paginated results.
- Search cancellation.
- Server search failure.
- Supported sorting.
- Unsupported sorting.
- Preservation of server order.

## 45. Playback and ownership tests

Cover:

- Play remote MP3.
- Play qualified remote FLAC.
- Select supported alternative resource.
- Reject unsupported codec.
- Play from Jive.
- Pause and resume.
- Stop.
- Natural completion.
- Next track.
- Previous track.
- Queue addition.
- HTTP interruption.
- Range-based seek where supported.
- Stop during buffering.
- Standby during playback.
- Radio ownership conflict.
- Spotify ownership conflict.
- Music ownership conflict.
- Stale callback after release.
- Stale URI after explicit Stop.
- No unexpected auto-resume.
- Remote server disappearance.
- Resource revocation.
- No duplicate decoder commands.

## 46. Renderer tests

Cover:

- Renderer disabled.
- Renderer enabled.
- SSDP discovery by controller.
- Device description.
- Service descriptions.
- ConnectionManager protocolInfo.
- SetAVTransportURI.
- Play.
- Pause.
- Resume.
- Stop.
- Actual state reporting.
- Volume.
- Mute.
- Seek when supported.
- Position reporting.
- Event subscription.
- Event renewal.
- Subscription expiration.
- LastChange events.
- Playback completion.
- Invalid instance ID.
- Unsupported action.
- Malformed SOAP request.
- Competing controllers.
- Another app owns the decoder.
- Standby playback rejection.
- Stale playback generation.
- Invalid or unauthorized resource URL.

Use standard fault codes appropriate to the implemented UPnP service.

## 47. Existing ecosystem regression tests

Run the relevant existing suites for:

- Standalone Base.
- Standalone Radio.
- Standalone Music.
- Spotify Connect.
- Qobuz Connect adapters where applicable.
- Native ownership.
- SlimProto.
- Local browsing.
- Local SQLite library.
- Native HTTP streaming.
- Resource IPC.
- Artwork.
- Now Playing.
- Standby and power lifecycle.
- Watchdog-safe supervision.
- Applet startup and shutdown.
- Extended Storage compatibility.

Run sanitizers on relevant host native components where practical.

Do not silently exclude failed suites.

Preserve any pre-existing unrelated failures in the report and distinguish new regressions.

---

# Part M — Physical Hardware Qualification

## 48. Autonomous hardware preparation

Use existing authorized development hardware and deployment procedures.

Inspect current project documentation and device inventory.

Do not assume historical IP addresses, firmware versions or process identities are still current.

Verify:

- Device identity.
- Strict known-host identity.
- Baby or Fab4 platform.
- Firmware and kernel.
- Available storage.
- Running Base and webserver generations.
- Original Jive health.
- Watchdog health.
- Current physical audio owner.
- Existing service listeners.
- Rollback readiness.

Do not access unrelated network devices.

The user has authorized autonomous implementation and normal reversible testing on existing authorized development devices.

This does not authorize destructive flash changes, bootloader modification, factory reset, watchdog disabling or access to third-party devices.

## 49. Deployment safety

Use established immutable versioned deployment mechanisms where applicable.

Preserve:

- Existing executable identity verification.
- Existing supervisor integration.
- Existing rollback artifacts.
- Existing service supervision.
- Existing Jive UI.
- Existing storage and user settings.

Do not overwrite active native executable files in place.

Do not create duplicate listeners or duplicate Base services.

Do not assume that copying Lua files updates an already running Lua VM.

Use qualified applet activation or reload mechanisms.

Do not perform an unsafe Jive restart or reboot.

If a required activation cannot be performed safely using current authorization and established procedures, complete the build, package and all other tests and report that exact physical-validation limitation.

Do not ask routine questions merely to continue source implementation.

## 50. Physical DMP test

Where authorized Baby hardware and a reachable test Media Server are available:

1. Start Standalone Base and the new UPnP service.
2. Verify service health.
3. Discover the Media Server.
4. Verify the server appears under My Music.
5. Open the server root.
6. Browse the server's actual music hierarchy.
7. Open an artist or album provided by the server.
8. Display the track list.
9. Display available metadata.
10. Search using server-native Search where supported.
11. Select a known-good MP3.
12. Acquire qualified decoder ownership.
13. Start playback through SqueezePlay.
14. Verify actual HTTP delivery.
15. Verify correlated decoder playback events.
16. Verify Now Playing.
17. Play the complete track.
18. Verify natural completion.
19. Play another track.
20. Test Pause, Resume and Stop.
21. Verify resource and ownership release.
22. Verify no unexpected playback resumes.

Use owner-confirmed audible evidence if naturally available during testing.

Do not claim that audio was heard if no human or qualified audio-capture mechanism observed it.

Do not block the complete implementation waiting for an audible confirmation.

Use decoder events and transport evidence as a separate verification level.

## 51. Physical Fab4 test

Where authorized Fab4 hardware is available:

- Verify existing local My Music remains functional.
- Verify local and external sources appear together.
- Verify local SQLite browsing is unchanged.
- Browse a remote Media Server.
- Play remote music.
- Stop remote playback.
- Play a local USB/SD track.
- Verify ownership remains exclusive.
- Verify Radio and other existing playback sources remain functional.

Do not rebuild the local music library merely to test UPnP.

Do not modify source music files.

## 52. Physical renderer test

Enable renderer mode through the normal settings path.

Use a standard UPnP controller or an automated standards-based controller fixture.

Verify:

1. The Squeezebox is discoverable as a renderer.
2. Supported protocolInfo is reported.
3. SetAVTransportURI accepts a valid resource.
4. Play starts actual SqueezePlay playback.
5. Pause and Resume work.
6. Stop releases the actual producer and decoder.
7. Volume and mute behave correctly.
8. Transport state reflects actual playback.
9. Event subscriptions receive correct state changes.
10. Renderer mode can be disabled.
11. Unsolicited playback while another application owns the decoder is rejected according to policy.
12. Standby does not permit unauthorized playback.

Where practical, test with at least one independent existing UPnP controller implementation.

Do not require a specific smartphone application to be installed by the owner before continuing.

Use a host-side controller fixture when a physical controller is unavailable.

## 53. Soak testing

Perform bounded-duration automated and hardware soak tests where available.

Exercise:

- Repeated discovery.
- Repeated browsing.
- Repeated search.
- Multiple track transitions.
- Repeated Stop and Play.
- Server disappearance and return.
- Renderer subscriptions and expiry.
- Standby and wake.
- Resource cleanup.
- Memory growth.
- File descriptor growth.
- Thread growth.

Suggested target: at least 30 minutes of representative continuous operation on physical hardware where practical.

Do not introduce unbounded automatic retries.

If a physical safety gate fails, stop that hardware operation, preserve the device, collect evidence and continue other work.

---

# Part N — Packaging and Release Integration

## 54. Package design

Integrate the UPnP component with the existing Standalone Base packaging approach.

Determine whether the feature should be distributed as:

- An optional native component managed by Standalone Base.
- A separate installable Standalone extension.
- An optional package containing native service and Jive integration.
- Another existing project-approved packaging arrangement.

Choose the option with the lowest complexity and best compatibility.

Do not create a redundant top-level user-facing UPnP applet.

The package may internally be named StandaloneUPnP, but the music browsing functionality must appear under My Music.

## 55. Compatibility and upgrade

Preserve compatibility with:

- Existing Standalone Base installations.
- Existing Standalone Radio installations.
- Existing Standalone Music installations.
- Devices without local storage.
- Devices without Extended Storage.
- Baby and Fab4.
- Existing user configuration.

Define explicit minimum Base version requirements if the new playback or ownership contract requires them.

Do not allow a partially upgraded combination to bypass ownership or resource safety.

Ensure the feature can be disabled or removed without breaking local My Music.

Do not automatically enable DMR network playback during upgrade.

## 56. Installation lifecycle

Ensure:

- Installation does not require LMS.
- Dependencies are included or correctly declared.
- Service startup is supervised.
- Service shutdown is clean.
- Configuration survives supported upgrades.
- Service removal releases network resources.
- Service removal releases decoder ownership if applicable.
- Device discovery stops when the service is disabled.
- Renderer advertisements are withdrawn when disabled.

Use the established applet package and repository metadata format.

Create appropriate versioned build artifacts.

Do not publish a public release automatically.

---

# Part O — Documentation and Final Deliverables

## 57. Documentation

Create concise but complete documentation.

Suggested files:

```text
research/STANDALONE_UPNP_IMPLEMENTATION.md
research/STANDALONE_UPNP_ARCHITECTURE.md
research/STANDALONE_UPNP_COMPATIBILITY.md
research/STANDALONE_UPNP_TEST_RESULTS.md
```

Use actual repository documentation conventions.

Include:

- Selected UPnP library and exact version.
- Dependency and license analysis.
- Build instructions.
- Native service architecture.
- Internal IPC contract.
- Music source integration.
- Discovery behavior.
- ContentDirectory browsing.
- Search capability handling.
- Audio resource selection.
- Playback integration.
- Ownership model.
- Standby policy.
- Renderer behavior.
- Security limits.
- Baby/Fab4 compatibility.
- Resource measurements.
- Hardware results.
- Packaging.
- Known limitations.
- Rollback and removal.

Store private evidence using the existing `.local` convention.

Do not include private credentials, access tokens or unnecessary device-identifying information in public documentation.

## 58. User-facing documentation

Provide a simple English user guide explaining:

### Playing music from a NAS or PC

1. Enable the Media Server on the NAS or computer.
2. Connect the Squeezebox to the same local network.
3. Open My Music.
4. Select the server by name.
5. Browse its music library.
6. Select a track or album.
7. Play through the Squeezebox.

Explain that available Artists, Albums, Genres, Folders and Search options depend on what the server supports.

Explain that the Squeezebox does not copy or re-index the complete remote library.

### Sending music from a phone

1. Enable Network Playback in Standalone settings.
2. Open a compatible UPnP/DLNA controller.
3. Select the Squeezebox renderer.
4. Select compatible music.
5. Start playback.

Explain the ownership and standby behavior.

Clarify that UPnP/DLNA does not automatically provide system-wide audio casting from every phone application.

## 59. Release notes

Prepare a short user-facing English release announcement.

Suggested wording:

> Standalone Base now supports UPnP/DLNA network music libraries directly through My Music. Discover compatible Media Servers on your local network, browse their albums, artists and folders, search when supported by the server, and play music using the original Squeezebox audio engine.
>
> Optional Network Playback also allows compatible UPnP controllers to use the Squeezebox as a Media Renderer. No Logitech Media Server is required.

Adjust this text to reflect functionality actually implemented and verified.

## 60. Source control

Preserve existing user changes.

Inspect the initial Git working tree.

Do not discard unrelated modifications.

Do not blindly stage the entire repository.

Commit logically related implementation changes according to existing project conventions.

Keep generated files, secrets and private hardware evidence out of public source control.

Provide a final summary of relevant commits and any remaining uncommitted changes.

---

# Part P — Final Acceptance Criteria

The complete implementation is accepted when the following requirements have been met or explicitly classified with a genuine, documented verification limitation.

## Core architecture

1. Native UPnP library qualified for the supported platforms.
2. Reproducible ARMv5 Baby build.
3. Reproducible ARMv6 Fab4 build.
4. No LMS, TinyLMS, Perl, MPD or GStreamer runtime dependency.
5. Original SqueezePlay decoder used.
6. Existing Standalone Base ownership reused.
7. Existing Standalone applets preserved.

## My Music

8. Media Servers discovered automatically.
9. Servers shown under the original My Music menu.
10. Friendly server names used.
11. Existing local music sources remain available.
12. No unnecessary top-level UPnP menu.
13. Generic server-defined library browsing works.
14. Artists, Albums, Genres and custom categories appear when provided by the server.
15. No synthetic server library structure.
16. No complete remote-library indexing.

## Search and metadata

17. Search shown only when supported.
18. Search executed by the Media Server.
19. Sorting follows server capabilities.
20. DIDL-Lite metadata correctly parsed.
21. Album artwork displayed where available.
22. Compatible audio resources selected.

## Playback

23. Remote MP3 playback works.
24. Qualified FLAC or other supported resources work where enabled.
25. Actual SqueezePlay state drives Now Playing.
26. Play, Pause, Resume and Stop work.
27. Natural completion works.
28. Queue and next/previous behavior work within supported limits.
29. Physical decoder ownership remains exclusive.
30. Standby and explicit Stop prevent unintended restart.
31. No complete audio-file buffering or extra decoder.

## Renderer

32. Optional renderer discovery works.
33. Standard MediaRenderer descriptions are valid.
34. AVTransport works.
35. RenderingControl works.
36. ConnectionManager accurately reports supported resources.
37. GENA eventing works where required.
38. Renderer uses the same physical playback infrastructure.
39. Unauthorized or conflicting network playback is rejected.
40. Renderer mode can be disabled cleanly.

## Quality and release

41. Positive and negative automated tests implemented.
42. Existing ecosystem regression suites run.
43. Relevant sanitizers and QEMU tests run.
44. Resource usage measured.
45. No unbounded memory or descriptor growth.
46. Existing storage and firmware remain unchanged.
47. Release artifacts prepared.
48. User documentation complete.
49. Actual physical validation results documented.
50. Remaining limitations explicitly identified.

---

# Part Q — Execution Order

Execute the following sequence autonomously:

1. Inspect the current repositories and preserve existing changes.
2. Review current playback, ownership and My Music architecture.
3. Select and qualify the UPnP library.
4. Establish reproducible Baby and Fab4 builds.
5. Implement native service and discovery.
6. Implement device registry and ContentDirectory.
7. Implement DIDL-Lite metadata and resource selection.
8. Implement generic paginated browsing.
9. Integrate Media Servers into the original My Music menu.
10. Implement server-native Search and sorting.
11. Integrate artwork and Now Playing.
12. Implement the shared remote playback adapter.
13. Integrate transport controls and queues.
14. Implement ownership and standby fencing.
15. Implement optional Media Renderer.
16. Implement standard renderer services and eventing.
17. Implement resource limits and network protections.
18. Add deterministic test fixtures and regression tests.
19. Run host builds and tests.
20. Run ARMv5 and ARMv6 builds.
21. Run QEMU and sanitizer validation.
22. Deploy on authorized hardware where safely possible.
23. Validate actual browsing and playback.
24. Validate renderer behavior.
25. Measure performance and run soak tests.
26. Fix discovered issues.
27. Complete packaging and documentation.
28. Produce the final consolidated report.

**These are internal execution phases, not approval checkpoints. Continue automatically from one phase to the next.**

---

# Part R — Final Report

At completion, report:

1. Overall implementation status.
2. Selected UPnP library and exact revision.
3. Baby and Fab4 build results.
4. Native service size and dependencies.
5. My Music integration screenshots or actual UI evidence where available.
6. Discovered Media Servers.
7. Actual server-native browsing results.
8. Actual Search capability results.
9. Metadata and artwork results.
10. Remote MP3 playback results.
11. Other qualified audio formats.
12. Transport and queue results.
13. Ownership and standby results.
14. Renderer discovery and controller results.
15. CPU/RAM/storage measurements.
16. Automated test counts and failures.
17. Physical hardware validation status.
18. Audible confirmation status, if independently available.
19. Packaging and release artifacts.
20. Relevant source commits.
21. Remaining limitations and any genuine blockers.

Do not claim that a feature works merely because it has been implemented.

Do not claim physical audio confirmation based only on an HTTP response.

Do not hide hardware-validation limitations.

Do not stop after writing the final architecture proposal.

---

## Final instruction

**Build the complete Standalone UPnP feature now.**

The user-facing product must be simple:

> Open My Music, select a local or network music library, browse the music as organized by its server, and play it through the original Squeezebox audio engine.

The underlying implementation must remain standards-based, lightweight and integrated into Standalone Base.

Use existing libraries and existing project infrastructure wherever practical.

Do not duplicate functionality already provided by UPnP ContentDirectory, DIDL-Lite, Search, AVTransport, RenderingControl or ConnectionManager.

Complete the implementation autonomously, resolve ordinary engineering problems independently, perform all available validation, and deliver the complete source code, tests and release-ready artifacts in a single consolidated result.