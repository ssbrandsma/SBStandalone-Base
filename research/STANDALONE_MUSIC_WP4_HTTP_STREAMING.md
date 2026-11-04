# WP4 authorized HTTP streaming

Implementation: [file-open worker](../native/sbbase/src/music_file.c), [private IPC](../native/common/music_resource_ipc.c), [pure HTTP policy](../native/common/music_http.c), [registry/read worker](../native/sbwebserver/src/music_stream.c), [Mongoose adapter](../native/sbwebserver/src/music_stream_http.c).

## Authority and startup

Music extends the existing sbwebserver Mongoose event loop. No second HTTP server, new listener, public path resolver or general filesystem endpoint is introduced. Normal binaries do not construct the registry or register this route. The separate test binary accepts an inherited connected AF_UNIX/SOCK_SEQPACKET descriptor. The descriptor itself is a private capability; no Unix socket pathname/listener or public registration endpoint exists.

The Base constructor receives an independently validated WP6a root and already-open approved library. File opens run on one256KiB-stack worker. WP6a walks the indexed relative path using descriptor-relative O_NOFOLLOW operations, validates mount/root identity and same-filesystem scope, and refuses symlink escapes/special files. File size and indexed modification time must match. The opened descriptor's device, inode, size and nanosecond mtime are captured. No external caller supplies a file path. Database placement/sidecars remain subject to WP1/WP6a policy; no hidden directories or internal-flash fallback are created.

## IPC contract

Version2 is136 explicit network-byte-order bytes, independent of native struct layout: magic SBM4@0; version/opcode/codec/method-or-status bytes@4..7; sequence@8;32-byte token@16;16-byte source@48; media generation@64; owner generation@72; stream epoch@80; expiry@88; size@96; device@104; inode@112; mtime seconds@120; mtime nanoseconds@128. Integer fields are u64. Audio MIME is derived only from m/f codec, never arbitrary headers.

REGISTER and RESERVE carry exactly one SCM_RIGHTS descriptor. Other messages carry none. Reject truncated/extra ancillary data, wrong versions, missing identities, zero tokens, invalid methods/codecs, malformed timestamps and nonmonotonic sequences; close received descriptors on rejection and set CLOEXEC explicitly. ACK statuses are internal0 success /1 rejection /2 capacity/expiry, not HTTP codes.

RESERVE accepts an inactive capability and capacity before a takeover; GET remains404. ACTIVATE binds the acknowledged physical owner generation only after the controller validates current ownership and decoder quiescence. RESET/revoke invalidate admissions and late completions. Music release acknowledgement waits for RESET acknowledgement as well as positive producer/hook/decoder proof. RESET acknowledges logical revocation, not completion of an arbitrary kernel read or safe eject. Peer loss revokes all slots.

## Serving and errors

`/music/stream/<64 lowercase hex>` is a bearer-capability route. Tokens use32 bytes from /dev/urandom; track IDs/filenames are not secrets and cannot substitute for tokens. Query/path suffixes are rejected. Authorized GET and HEAD preserve encoded bytes with audio/mpeg or audio/flac. HEAD verifies the descriptor on the worker but performs no audio pread.

Single closed/open-ended/suffix byte ranges use overflow-checked u64 arithmetic. Headers and body offsets are64-bit even on ARM32; pread uses64-bit off_t and each socket write remains small. Tests include offsets beyond4GiB. Multipart/duplicate ranges are rejected. Normal responses are200/206; invalid range416 includes `Content-Range: bytes */size`; unknown token404; known expired/revoked token410; method405; saturated admission503. A missing/changed/unlinked file fails verification; I/O errors before headers become an error response, and after headers close the body instead of inserting another HTTP response.

## Bounds, cancellation and FD ownership

Eight registry slots, eight fixed read jobs, four HTTP stream clients,16KiB per read job, and at most roughly32KiB queued audio per socket plus headers. No complete file is buffered. The single read worker uses a condition variable while idle. The explicit Music test mode uses the existing Mongoose loop with10ms polling; normal product polling stays unchanged. Slow progress has a15s deadline. Base file reads never run on its protocol loop.

Base owns the opened descriptor until registration ACK, then closes its copy. Registry owns the received duplicate. Each read job owns a CLOEXEC duplicate and uses pread, never a shared-offset seek. Each read verifies pinned device/inode/size/mtime, then checks for change after reading. Results carry registry sequence; revocation/expiry/slot reuse discards old bytes. HTTP poll rechecks token validity before enqueueing. Disconnect cancels its job; BUSY cancellation IDs remain in bounded retiring client slots until retry succeeds.

A running read cannot be forcibly cancelled safely on old kernels. Its descriptor remains owned until return and its result cannot be published after revocation. Already transmitted/kernel-buffered bytes cannot be recalled. Host tests do not qualify hostile mount-namespace races or physical removal behavior on original kernels; those remain activation gates inherited from WP6a.
