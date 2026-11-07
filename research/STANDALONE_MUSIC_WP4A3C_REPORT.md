# WP4a.3c - Executable identity and replacement readiness

Final status: **partially complete, hardware admission blocked**. The read-only investigation and local safety changes are complete. The current Base remains LEVEL 3. WP4a.3b activation **cannot safely resume** under this evidence. No active executable was replaced, no service restarted, no watchdog value changed, no USB scan requested and no decoder command sent.

## Confirmed findings

The failed WP4a.3b operation was a live executable **open** failure, not a SHA mismatch: readlink succeeds but open of `/proc/3933/exe` returns errno 2 (ENOENT), with zero bytes hashed. The current installed file is readable and has the restored known SHA, but its merged inode is 2069 while the process still maps inode 1992. Copying the original file back did not recreate the missing live executable proof. Proxy and web have independently readable and kernel-correlated live executable hashes (LEVEL 1).

Atomic rename of a new temporary file over the canonical path is established from the invoked copier and deployment records; no in-place executable overwrite was used. The first retained direct open failure is after that deployment. There is no before-replacement live descriptor/stat witness, so its exact onset, the old inode's link count and the precise UnionFS whiteout/copy-up sequence cannot be established retrospectively. No destructive reproduction was attempted. See UNIONFS for the confidence boundaries.

## Implementation

- New bounded native `executable_identity.inc`: separate process/start/supervisor, readlink, open, read/hash, metadata, active inode and executable mapping checks. Structured errno/stage diagnostics and a read-only held-descriptor rehash probe.
- Existing coordinator `verify-exe` now uses this stronger verifier. LEVEL 1 requires expected live bytes and independent kernel correlation. LEVEL 2 is explicitly disabled: neither a receipt nor a pinned artifact alone can retroactively prove this already-unreadable Base. LEVEL 3 always refuses.
- Native copy/remove checks refuse paths still used by live processes. Legacy installation checks every planned target before any persistent staging or overlay mutation; already-restored files are not recopied during rollback.
- Canonical 64-hex generation paths and a same-generation Base/web/session topology are supported; mixed generations or missing/mismatched supervisors refuse. Retained proxy topology remains explicit.
- Offline `music_generation.py` creates source-bound, write-once generation receipts and verifies payloads and independently pinned rollback receipts. All generated bundles remain `activation_qualified:false`; no hardware candidate was produced after failed current identity qualification.

Only StandaloneBase sources/tests were changed in WP4a.3c. Previous uncommitted WP4a.3b work and all prior evidence were preserved. No Radio/Spotify/Qobuz repository or installed application was modified in this phase.

## Validation and hardware

Twelve relevant host CTest groups pass, including executable proof, topology, watchdog probe/coordinator, file/deployment rollback and TinySC. Nine generation-format cases and thirteen executable-identity cases cover controlled faults and lifecycle scenarios. Six focused ASan/UBSan groups pass. Baby ARMv5 and Fab4 ARMv6 static maintenance builds pass; production ARM/QEMU reads the private 20-slot IPC layout without mutation and refuses unsupported host mutation. No new kernel API or large dependency was introduced.

On 192.168.1.244 only, strict-known-host SSH reconfirmed Fab4 7.7.3 r16676, the same boot, Jive 3192/start 55166, Base 3933/start 17600372, proxy 3282/start 56496, web 3312/start 56733 and watchdog 639/start 923. Both Jive registrations, watchdog FD and listener ownership pass. Original installation hashes/modes/ownership match the restored baseline. TinySC remains fresh STOPPED_VERIFIED/count zero; original-protocol browse returns 11 non-playable tracks. USB write counters remain 6 writes/29 sectors for sda and sda1. The hash-verified RAM diagnostic helper was removed.

## Footprint and readiness

Production helper size is 91212 bytes on both architectures, 4096 bytes above the WP4a.3b helper. Native diagnostics use a bounded 8192-byte hash buffer, at most two extra open descriptors, and run only at explicit boundaries. Compiler static frames are documented in IDENTITY; actual target peak RSS and coordinator startup overhead were not measured. Read-only live verification took 226.214 ms for the 874644-byte proxy and 51.083 ms for the 291976-byte webserver in the recorded samples. Base refused in 0.693 ms before hashing. Host fixture receipt verification median was 84.410 ms; no ARM throughput inference is made.

The versioned artifact format and guards are locally tested, but there is **no qualified activation or rollback generation for the current unreadable process**. Phase J was conditional on successful Phase I and was not entered. A separate owner-authorized maintenance/cold-start plan must first establish a live-byte-verifiable baseline while addressing the RAM library and watchdog/TinySC requirements. That transition is outside WP4a.3c; no raw stop, reboot or hash bypass is proposed as an automatic action.

## Documentation

Companion files: `STANDALONE_MUSIC_WP4A3C_UNIONFS.md`, `..._IDENTITY.md`, `..._DEPLOYMENT.md`, `..._TEST_RESULTS.md`, `..._HARDWARE.md`, `..._ROLLBACK.md`, `..._EVIDENCE.md`. Private current evidence: `.local/wp4a3c/`.
