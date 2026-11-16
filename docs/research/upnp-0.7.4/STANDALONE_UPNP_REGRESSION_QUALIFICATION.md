# Regression qualification

Evidence is preserved in the candidate's `evidence/` directory and hashed by
`release-manifest.json` / `SHA256SUMS`. No physical device was used.

## Environment and historical failures

Testing uses a disposable Ubuntu 24.04 WSL distribution, root for complete
`/proc` executable-identity visibility, a private network namespace with an
isolated dummy LAN/multicast interface, and the configured Python/Lupa venv.
Root visibility is a qualification prerequisite, not a relaxed production guard.
The original WSL filesystem reported I/O errors and was not repaired or reused.
Another project's WSL validation shares the host network, so fixed-port fixtures
must not run on that shared network. Early collisions were diagnosed from the
wrong server responses and excluded from clean isolated qualification.

Missing host unzip caused applet deployment fixture failures; the prerequisite
was installed. Missing Lupa caused the idle-fence skip; the configured venv
removes it. Executable identity, internal-file maintenance, storage and
rollback/recovery checks retain their strict identity/hash/generation gates.

The old playback fixtures assumed a new SlimProto connection for every selected
or natural-next track. The production controller supports reuse: fixtures now
use the same peer and explicitly attest idle for user replacement/Stop. No
firmware flush is treated as idle proof. A native Stop transition was corrected
to preserve the in-flight selection during connection reuse.

An intermittent backlog test read three HTTP *headers* and then asserted that
the third body had arrived. A split receive exposed that race. It now parses
Content-Length and waits for all three bodies, retaining payload/order and
malformed-request assertions. Twenty consecutive targeted repetitions passed.
The connection test also rejects EOF instead of spinning forever on an empty
receive. Larger provider metadata uses the unit fixture's existing 8 KiB status
buffer; a separate 128-byte buffer still proves bounded oversize rejection.

## Final checks

The sealed complete Base run passed **133/133, zero skips**, in **287.23 seconds**.
The expanded installer fixture separately records ten passing transaction cases;
the original C-module verifier records four passing full-package/fault runs.
Twenty consecutive backlog repetitions passed. Radio's 29 Lua suites, Spotify's
32 tests, and Qobuz's lifecycle/Now Playing suites passed against the inspected
checkouts. Actual ZIP verification and both ARM/QEMU target runs also passed.

The final complete Base suite, ecosystem suites, original DB test, actual ZIP
installer verifier, ARM/QEMU and ASan/UBSan production chains are recorded in
the named evidence files. The finalization report gives the completed totals;
partial and historical logs are not substituted for a complete run.

The production chain includes actual SSDP, HTTP device descriptions, SOAP
Browse/Metadata/Search/capabilities, UpdateID/stale generations, authenticated
Base gateway, cancellation, native 137-entry queue, compressed socket resource,
opaque loopback resource token and exact HTTP MP3 bytes. Decoder frames and
physical idle attestation are explicitly simulated. Renderer checks include
AVTransport/RenderingControl/ConnectionManager, state correlation, ownership and
standby rejection, volume/mute, GENA lifecycle and callback origin rejection.

URL/XML/HTTP bounds, DTD/entity rejection, protected self endpoints, unsupported
resource forms, IPC authentication/capability negotiation and stale queue/provider
actions remain covered. Seek/shuffle/repeat are not silently implemented through
an unqualified path. No second decoder, stream player or ownership manager exists.

ASan and UBSan instrument both pupnp and the service, plus the full Base/web
network fixture. Leak detection is enabled. The ASan quarantine is explicitly
bounded to 4 MiB (thread quarantine 64 KiB) for the normal 16 MiB RSS growth check;
the default 256 MiB quarantine itself exceeded that check during repeated RPCs.
The assertion was not relaxed. All fixture child exits must be zero, including
graceful web shutdown, so a sanitizer termination cannot pass unnoticed.

## Resource interpretation

Phase samples include daemon startup/discovery/Browse/Search/pagination and
renderer enabled/disabled, plus Base/web idle, 137-item construction, simulated
playback and Stop/clear. Raw JSONL records RSS, high-water RSS, threads, file
descriptors, CPU seconds, HTTP latency and simulated startup latency without
request arguments or credentials. These are host figures, not Baby/Fab4 budgets.

The 30-minute soak keeps the real daemon alive, repeatedly browses/searches,
expires a secondary server, creates native 137-item playback sessions and tests
renderer subscriptions/control/cleanup. Base/web fixture processes are fresh
per cycle; this is not a 30-minute uninterrupted physical decoder or persistent
Base endurance test. RSS high water is interpreted with the clean sanitizer
teardown and descriptor/thread bounds; it is not proof of unlimited leak freedom.

The final default-allocator run completed cleanly in **1800.178 seconds**:
**5116 Browse/Search pairs and 44 native/renderer cycles**. Post-cycle RSS
samples ranged **2748–10552 KiB**, ending **9516 KiB**. Post-cycle descriptors
were **eight**; threads **14–18**. Daemon CPU time was **8.38 seconds**, about
0.47% of one host CPU over that interval. The final fixture and child shutdowns
returned zero. Raw samples and hashes are in `evidence/soak-30min.json` and its
log; these are sampled cleanup states, not continuous instantaneous maxima.

Measured phase samples (KiB, uninstrumented x86_64 host):

| Component | RSS range | Peak sampled VmHWM | Descriptors | Threads | CPU seconds at last sample |
| --- | ---: | ---: | ---: | ---: | ---: |
| sbupnp startup/discovery/browse/search/renderer | 2048–2188 | 2596 | 8 | 12–16 | 0.05 |
| Base idle/137-row playback/mutations/Stop/clear | 6656–8388 | 8388 | 9–10 | 2 | 0.03 |
| web resource bridge idle/playback/cleanup | 2304 | 2304 | 5–6 | 2 | below 0.01 tick |

The 56 measured CometD requests averaged 0.829 ms, maximum 2.104 ms. The two
network admission requests took 2.622 and 28.358 ms. Simulated Base startup to
confirmed playing took 158.886 ms for one item and 195.010 ms for the 137-item
container, including collector overhead. This tiny host sample is not a latency
guarantee or a measurement of audible ARM playback. VmHWM is resident high water,
not an exact peak malloc-allocation counter.

A separate diagnostic with `MALLOC_ARENA_MAX=1` completed **302.424 seconds**,
832 Browse/Search pairs and eight native/renderer cycles: RSS samples
2432–2920 KiB, ending 2560 KiB; descriptors eight; threads 15–17; daemon CPU
1.37 seconds. Compared with the default-allocator run and clean LSan teardown,
this supports glibc arena/cache retention as a contributor to the host RSS rise.
It does not prove that every possible long-lived workload is leak-free. The
diagnostic environment was not applied to production packages; ARM uses musl.

Neither host Linux nor QEMU demonstrates Linux 2.6 kernel behavior, audio timing,
watchdog ordering, Jive UI focus on a physical screen or audible output.
