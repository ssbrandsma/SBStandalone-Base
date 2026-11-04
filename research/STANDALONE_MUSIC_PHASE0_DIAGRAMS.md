# Standalone Music Phase 0 Architecture Diagrams

Proposed design, not implemented behavior. See [report](STANDALONE_MUSIC_PHASE0_REPORT.md), [protocol](STANDALONE_MUSIC_PHASE0_PROTOCOL.md) and [evidence](STANDALONE_MUSIC_PHASE0_EVIDENCE.md). TinySC and Perl remain disabled; no arrows below involve LMS runtime.

## 1. Components and ownership

```mermaid
flowchart TB
  subgraph Firmware[Existing firmware]
    UI[SlimMenus / SlimBrowser / NowPlaying]
    MM[Fab4 MediaManager + Base Lua adapter]
    OS[Existing udev automount]
    SP[SqueezePlay decoder]
    ALSA[Shared PCM + jive_alsa]
  end
  subgraph Base[Standalone Base backend]
    CTRL[sbbase control loop: typed dispatch / owner / queue / transport]
    DBW[Database worker: bounded queries + one writer]
    SCAN[Low priority native scan helper]
    WEB[Existing sbwebserver: authorized file resources]
    IMG[Existing image worker / cache]
    PROXY[Existing sbproxy: HTTP / HTTPS / HLS]
  end
  APPS[Radio / Spotify / Qobuz existing integrations]
  DISK[Selected media: audio + Base SQLite + thumbnails]
  UI <-->|CometD / status / actions| CTRL
  CTRL -->|SlimProto commands| SP
  SP -->|STAT / RESP / DSCO| CTRL
  SP -->|GET encoded file| WEB
  WEB -->|bounded safe reads| DISK
  SP --> ALSA
  CTRL <-->|bounded requests / wakeup FD| DBW
  DBW <--> DISK
  CTRL <-->|jobs / progress / cancellation| SCAN
  SCAN -->|bounded metadata batches| DBW
  SCAN -->|read only files| DISK
  CTRL <-->|private resource IPC| WEB
  WEB <--> IMG
  IMG <--> DISK
  OS -->|mount state| MM
  MM <-->|storage generations / safe eject ack| CTRL
  APPS <-->|existing leases + acknowledged handoff| CTRL
  APPS --> PROXY
```

The scan-to-DB arrow is logical messaging through Base's bounded ingestion path; it does not mean shared SQLite connections or a scanner-owned writer. The existing Spotify bridge remains an app transport, omitted here for clarity; no new audio server is added. Local stream tokens resolve only to approved resources, not arbitrary files.

## 2. Scan sequence

```mermaid
sequenceDiagram
  participant A as Storage adapter
  participant C as Base control
  participant S as Scan helper
  participant D as DB worker
  participant F as Selected filesystem
  participant J as Jive
  A->>C: validated medium / generation / root
  C->>D: open schema and return existing library revision
  D-->>C: ready or explicit recovery error
  C-->>J: existing browse data available
  C->>S: incremental job with generation and budgets
  loop bounded traversal and duty-cycle slices
    S->>F: stat / bounded metadata reads
    S->>C: changed records / progress batch
    C->>D: ingest batch for current generation
    D->>D: short transaction; browse jobs have priority
  end
  alt complete root and still current generation
    S-->>C: completed traversal epoch
    C->>D: finalize unseen deletion and publish revision
    D-->>C: committed revision
    C-->>J: coalesced library/menu refresh
  else cancel / I/O failure / removal
    C->>S: cancel generation
    C->>D: abandon incomplete epoch; no deletion sweep
    C-->>J: available prior data / offline or error state
  end
```

## 3. Album browsing sequence

```mermaid
sequenceDiagram
  participant J as SlimBrowser
  participant C as Existing CometD dispatcher
  participant D as DB worker
  participant W as Existing web/image pipeline
  J->>C: browselibrary items offset limit mode:albums menu:1
  C->>D: validated filters + bounded page request
  D-->>C: total + ordered album rows + revision
  C-->>J: count / offset / item_loop / base actions
  J->>W: cover opaque ID + fingerprint + dimensions
  W-->>J: cached thumbnail or bounded render/placeholder
  J->>C: album item go with album_id and mode:tracks
  C->>D: disc/track ordered filtered page
  D-->>C: rows + stable IDs + total
  C-->>J: track items + play/add actions
```

No browse request claims playback. If a library revision changes between pages, the response/adapter invalidates stale pages rather than silently mixing orderings.

## 4. Local playback sequence

```mermaid
sequenceDiagram
  participant J as Jive
  participant C as Base owner/queue/controller
  participant O as Existing owner adapter
  participant W as Existing webserver
  participant P as SqueezePlay
  participant A as ALSA
  J->>C: playlistcontrol cmd:load album_id
  C->>O: yield request
  O->>O: stop producer, restore hooks, reattach player
  O-->>C: acknowledged or failure
  alt yield accepted and medium online
    C->>C: commit owner generation + queue revision
    C->>W: register bounded opaque resource
    C->>P: strm-s with codec and HTTP resource
    P->>W: GET authorized encoded audio
    W-->>P: unchanged MP3/FLAC bytes with backpressure
    P-->>C: RESP / connection status / STMs
    C-->>J: confirmed current track and playing status
    P->>A: decoded PCM through existing output
    W-->>P: input transfer EOF
    P-->>C: decoder-ready / drain events
    C->>C: contextual terminal decision once per epoch
    C-->>J: new queue index or stopped/error status
  else failed yield or stale medium
    C-->>J: explicit error; local stream not started
  end
```

For MVP, wait for correct terminal conditions before next start. Later gapless prebuffer can start next input before current audible completion, requiring separate identities and metadata timing.

## 5. USB/SD lifecycle

```mermaid
stateDiagram-v2
  [*] --> Absent
  Absent --> MountPending: firmware uevent
  MountPending --> Available: existing OS mount validated
  MountPending --> Unavailable: timeout or unsupported mount
  Available --> Indexed: open existing DB or completed scan
  Indexed --> Scanning: explicit or debounced reconciliation
  Scanning --> Indexed: complete generation published
  Scanning --> Indexed: cancel retaining last valid library
  Available --> Quiescing: safe eject request
  Indexed --> Quiescing: safe eject request
  Scanning --> Quiescing: safe eject request
  Quiescing --> Unmounted: cancel scan + stop local stream + close resources + ack
  Unmounted --> Absent: firmware unmount completes
  Available --> Offline: physical removal or EIO
  Indexed --> Offline: physical removal or EIO
  Scanning --> Offline: physical removal or EIO
  Quiescing --> Offline: physical removal first
  Offline --> Absent: generations revoked; descriptors closed
  Unavailable --> Absent: removal
```

Storage arrival never starts TinySC or steals existing app playback. Safe-eject acknowledgement precedes OS unmount. Physical removal is handled even if the Lua callback is missing. Reinserted media gets a new mount generation; pending work from its earlier appearance cannot publish or reopen files. A device-path directory alone is not evidence the medium remains mounted.
