# WP4a.1 production bridge

Sources: `native/sbbase/include/ownership_bridge.h`, `src/ownership_bridge.c`, `src/ownership_http.inc`; Lua OwnershipHttp/Client/Runtime/Binding. Exact current source lines and hashes are in the evidence index.

## Authentication and startup

Only explicit `--ownership-token /var/run/sb-music-owner/token` activates the route on the existing Base HTTP listener9000. The administrator provisions an owned mode0700 volatile parent outside web/shared/removable roots. Token leaf must be absent; O_EXCL/O_NOFOLLOW creates mode0600 and writes32 random bytes as64 lowercase hex. Entropy/file work happens before the event loop. Lua reads the runtime secret after Base is ready; no secret is embedded in source, URLs or settings.

`POST /api/music/ownership` requires `X-Music-Ownership: <token>` and a kernel-verified IPv4 loopback peer. Token comparison is constant-time. Duplicate capability headers, bad tokens, duplicate/missing Content-Length, transfer encoding and oversized/invalid bodies are rejected. The custom header avoids installed SocketHttp overwriting Authorization with stored Basic credentials. Existing REST/CometD authorization is unchanged.

Clean close removes only the created token inode. Crash/failed startup may leave a stale token, which the next startup refuses. Confirm the old process is stopped before removing the stale leaf; provision a fresh token per incarnation. Trusted directory ancestry is an administrator responsibility.

Threat model: Base/Jive share a privileged local UID. Permissions exclude other UIDs, and the capability excludes unauthenticated local/LAN callers. This does not isolate applets within the same VM or defend against root or privileged packet capture. Unix peer credentials were evaluated, but installed Lua Unix-domain transport support was not established; existing asynchronous SocketHttp/RequestHttp was available. Loopback alone is never authentication.

## Protocol

At most384 ASCII bytes, exactly12 single-space-separated fields, no NUL/CR/LF:

```text
SBOWN1 operation owner incarnation sequence generation connection request timeout_ms flags revision reserved
```

Owners1..4 are Radio/Spotify/Qobuz/Music. Canonical unsigned64-bit identities are decimal strings in Lua/JSON. Mutations require increasing sequence numbers. The last successful mutation can be retried until replaced by another; retries return a fresh snapshot, never an obsolete grant. `hello` discovers sequence; `query` is read-only and cannot grant ownership.

Operations: hello/register/query/request/ack/release/idle/cancel/fault/reconnect/suspend/cleanup. Registration returns a native incarnation. Proof flags: producer1, hooks2, decoder4; partial ACK refuses, only all7 can commit. Adapter stages supply these independently. Request timeout1..10000ms; current incarnation/request/generation/connection and deadline must match. Invalid operations, overflow and stale tuples fail.

One physical handoff, one Lua mutation per adapter, four subscriptions,1024-byte replies. Query with current revision holds an existing client slot up to20s. Lua bridge timeout25s, command/registration deadline11s, release reconciliation9s, Music readiness5s. Completed Jive timers are cancelled. No fast permanent polling/thread/new port is introduced. Lost contact after45s or an explicit fault quarantines the owner; neither proves silence nor grants another owner.

## Connection and Music integration

HELO/detach generates the real connection tuple. HELO never establishes idle. Initial baseline needs all four explicit idle proofs on the same tuple. Reconnect retains the potentially active recorded owner and needs fresh cleanup/release. Standby admission holds compose with alarm holds; clearing a hold never auto-plays.

Bind a constructed WP4 instance to the same manager with `sb_ob_bind_music(bridge, sb_playback_bridge_proof, sb_playback_bridge_begin, sb_playback_bridge_ready, playback)`. Cleanup sends native stop and resource RESET; readiness remains false until private resource work settles. Final Lua decoder/hook proof still passes the independent WP4 gate. No binding means no successful Music proof. The bridge-only CLI starts no scanner, DB or Music controller; normal Music activation remains OFF.
