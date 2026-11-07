# Executable identity model

## Trust and levels

The authenticated deployment host, root-owned source-bound artifacts and serialized maintenance actor are trusted. Arbitrary root/ptrace attacks and self-modifying trusted firmware are outside this model; receipt text, PID, argv or pathname alone never authorizes a process. Concurrent unrelated deployment is not permitted. Source hashes and independently expected generation/rollback receipt hashes must be pinned by the caller; receipt self-hashes are not an authenticity authority.

LEVEL 1 is implemented: exact PID/start, expected stable parent, recognized role, readable `/proc/PID/exe` descriptor, expected size/SHA, root/non-writable file metadata, unchanged metadata during hashing, current proc executable inode/device equal to that FD, matching executable mapping inode/device/path, and final process identity recheck. Linux's exact ` (deleted)` suffix is accepted only alongside still-readable kernel-correlated live bytes. The current pathname's different bytes are never substituted.

LEVEL 2 is reserved and disabled in production. The diagnostic pin holds a CLOEXEC FD, rehashes it after a bounded wait, and still requires current kernel correlation. A readable pinned artifact without that relationship returns LEVEL 3. On the actual current Base, initial open fails, so a pre-replacement pin cannot be created retroactively. The target permits read-only pin stability observations on proxy/web, but pin-across-replacement on this UnionFS was not authorized or qualified.

LEVEL 3 refuses: unknown role, inaccessible live bytes/mappings, stale PID/start/parent, wrong artifact, changed inode/metadata, failed hash or insufficient continuity. No exception accepts PID 3933 or a particular old SHA.

## Interfaces and limits

`inspect-exe PID START PARENT SIZE SHA` and `inspect-pin ...` emit `SB_EXEC_IDENTITY_1` structured results with separate error-stage/errno fields and elapsed microseconds. Both are read-only, explicit commands on the production helper. `verify-exe` preserves its old success/exit API and delegates to this verifier, so existing source-bound coordinator actions inherit stronger proof. There is no public network API or numeric FD adoption interface.

Hash bound: 2 MiB; buffer: 8192 bytes. Maps: at most 262144 bytes/4096 lines with 2048-byte lines. One pinned executable FD, temporarily one extra maps/stat FD; all close on return and the executable descriptor is CLOEXEC. At most a fixed 100 ms diagnostic pin wait in production; test pauses/faults compile out. No continuous hashing or Base control-loop work is introduced.

Compiler ARMv6 static frames: executable_hash 8552 bytes, executable_inspect 2432 bytes, existing aggregate main frame 34416 bytes. These are compiler frame measurements, not total RSS or a complete libc call-stack bound. Actual embedded peak RSS was not measured. No dynamic memory is allocated explicitly by the new proof logic; libc fopen buffering remains bounded by the existing library.

## Replacement policy

Avoid replacing a path still used by a process. Native copy/remove refuses active targets; checks occur before and immediately before rename. The serialized root deployment workflow is still required; this is not a kernel atomic exclusion lock against arbitrary concurrent exec. New generation files are write-once even when inactive. Base/web/session generations must match, parentage and listener ownership must verify, and the retained proxy remains separate. Unknown or mixed generations refuse.

These primitives do not qualify a complete new runtime on the Touch. Current Base is LEVEL 3, so admission remains closed and no fallback level is enabled.

## Recorded latency and storage

| Role | Level | Live verification ms | Bytes hashed | Pin probe ms |
| --- | ---: | ---: | ---: | ---: |
| base | 3 | 0.693 | 0 | 4.498 |
| proxy | 1 | 226.214 | 874644 | 538.897 |
| web | 1 | 51.083 | 291976 | 237.295 |

Pin probe times include two hashes and a deliberate 100 ms wait. Samples are actual Fab4 read-only observations, not streaming CPU budgets. Host receipt format fixture: 19661 bytes; median verify 84.410 ms. The current four prospective Music artifacts total 1341032 bytes before filesystem allocation/receipt/recovery reserve; none were installed. Source provenance JSON is 17736 bytes. Target peak RSS, full coordinator startup cost and actual generation storage overhead remain unmeasured because those actions were not authorized/qualified.
