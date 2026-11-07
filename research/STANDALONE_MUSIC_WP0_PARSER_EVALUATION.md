# Standalone Music WP0 Metadata Parser Evaluation

This is an isolated feasibility spike, not a scanner/indexer and not a production dependency selection. Tools: [TagLib adapter](../tests/music/parser_taglib.cpp), [existing-ID3 comparator](../tests/music/parser_hls.c), [build script](../tests/music/build_parser_spike.sh), [benchmark](../tests/music/benchmark_parsers.py), [generated corpus](../tests/music/fixtures/README.md). Normal sbbase/sbwebserver/sbproxy link no new metadata library.

## Reproducible setup

Pinned upstream reference: TagLib **v1.13.1**, archive `https://codeload.github.com/taglib/taglib/tar.gz/refs/tags/v1.13.1`, SHA256 `c8da2b10f1bfec2cd7dbfcd33f4a2338db0765d851a50583d410bacf055cfd0b`. This older release was chosen for compiler feasibility, not as an assertion it is the correct maintained/security-reviewed production release. The build script verifies the archive hash, builds privately under `.local/wp0`, disables shared libraries/tests/examples/zlib and statically links the tools. Nothing is installed into system locations by the final script.

```sh
export CMAKE_BIN=/mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/cmake
sh tests/music/build_parser_spike.sh host
sh tests/music/build_parser_spike.sh baby
sh tests/music/build_parser_spike.sh fab4
python3 tests/music/benchmark_parsers.py --iterations 25 --output .local/wp0/parsers-host.json
python3 tests/music/benchmark_parsers.py --platform baby --iterations 5 --output .local/wp0/parsers-baby-qemu.json
python3 tests/music/benchmark_parsers.py --platform fab4 --iterations 5 --output .local/wp0/parsers-fab4-qemu.json
```

The optional first download requires network; checked-in fixture tests do not. All tools require host C/C++ compilers; cross builds use existing Buildroot musl GCC/G++ 8.4.0 and ARMv5 soft-float or ARMv6 soft-float flags matching Base. QEMU is optional for emulated execution, not cross-compilation. No ARM parser prerequisite was missing here. Reusing an older CMake cache can reset flags on compiler-path changes; the final script configures twice and passes an explicit private install prefix to avoid a stale cache installing elsewhere.

## Corpus and correctness

Fourteen generated CC0 files: short silent MP3 with no tags; ID3v1, v2.2, v2.3, v2.4; FLAC and Ogg Vorbis comments; embedded 69-byte PNG in MP3/FLAC/Ogg; separate 225-byte generated JPEG cover; invalid UTF-8; truncated ID3; oversized declared ID3; actual 1 MiB ID3 text; malformed FLAC/Ogg. Unicode includes Cafe with an accented e and a musical-symbol surrogate pair; v2.2/v2.3 use UTF-16LE and v2.4 uses UTF-8. Generated media can be recreated using the optional FFmpeg tool; exact encoder bytes can differ by version. Normal host tests use the checked-in corpus and existing HLS parser regression tests.

TagLib recovered title/artist/album, approximately 0.2-second duration, 44100 Hz/stereo properties, available bitrate and expected PNG artwork on all five main ID3v2/FLAC/Ogg cases. ID3v1 title and the JPEG cover were also checked. Native host and both ARM tools under QEMU completed the corpus without crash/timeout. A corrupt/oversized tag can leave an otherwise recognizable MP3 audio stream: file-valid does not imply valid metadata, and a scanner must preserve parse-error/partial-field information. Invalid-Unicode repair policy still needs a defined strict/replacement contract; the small corpus is not a fuzz/security proof. ID3v2.2 PIC and broader VBR/unsynchronization/chained-Ogg cases remain future coverage.

Existing HLS parser accurately emits the v2.3/v2.4 fields and validated small APIC covered by its existing tests. It does not become an ID3v1/v2.2/FLAC/Ogg/properties reader because it receives entire files in this harness. Missing fields are reported as missing, not counted as speed wins for an equivalent workload.

## Measurements and interpretation

The ARM stripped **complete test tools**, including their static runtime and instrumentation, are approximately 932968 bytes for TagLib and 50100 bytes for the HLS comparator for each target. These are not incremental libtag-only sizes; the generic FileRef pulls in formats beyond this MVP. A narrower format wrapper may reduce size but was not measured. Host static glibc tools are larger and must not be substituted for target deployment sizes. Exact current figures and all per-file CPU/RSS/allocation records are in [measurements](STANDALONE_MUSIC_WP0_MEASUREMENTS.json).

Ordinary small host TagLib fixtures use roughly 1.6-1.9 MiB VmHWM and low-millisecond or sub-millisecond CPU per file in the WSL filesystem workload. The actual 1 MiB title fixture reaches approximately **19 MiB VmHWM and 15 MiB peak live C++ new payload**, far above the proposed 4 MiB scanner target/8 MiB ceiling. The precise final-run table is included in the main report/measurement artifact. This disqualifies unrestricted use despite good ordinary-format correctness.

HLS parser object allocation is fixed at 67232 bytes in the comparator; the comparator records one explicit allocation, while stdio/libc allocations are not counted by that counter. TagLib instrumentation counts C++ new calls and peak live requested payload, not all C malloc/allocator overhead. RSS is read from `/proc/self/status` VmHWM after exec; inherited getrusage peaks were avoided. CPU uses process clock over repeated reads/parses, excluding final JSON printing. Host filesystem I/O and caches affect results. QEMU figures are explicitly emulation (including host/QEMU memory), **never ARM device timing/RAM**.

The TagLib test process has a 64 MiB address-space cap, 2 MiB test-file ceiling and caller timeout. These are spike protections, not a suitable production policy: real audio files exceed 2 MiB. Production probing must bound metadata blocks and bytes read while allowing large audio files, preserve artwork offset/length references without copying large blobs, and enforce cancellation/fault isolation. A global RLIMIT alone is not cooperative metadata parsing.

## Strategy comparison and recommendation

| Strategy | Evidence/coverage | Resource/build/license and maintenance implications | Recommendation |
|---|---|---|---|
| Extend existing HLS ID3 utilities | Executed host/ARM comparator plus existing v2.3/v2.4 regression; UTF/APIC validation already exists | Small C code, bounded frame design; missing v1/v2.2, file properties, full tags and FLAC/Ogg | Reuse encoding/image checks, not the whole live-stream state machine as a file indexer |
| Focused C libraries: libid3tag + libFLAC metadata + libogg/comment reader | Qualitative upstream/API comparison; no combined adapter/build measured in WP0 | Native C; multiple allocations/APIs and remaining MP3 duration/MP4 work; GPL libid3tag and BSD-style Xiph terms require pinned license review | Plausible implementation comparator, no claimed measured footprint or drop-in coverage |
| C-compatible external strategy: TagLib with narrow C ABI | v1.13.1 actually cross-built; generic C binding lacks rich format-specific artwork access, so spike uses C++ | Toolchain works; broad correctness; larger binary and unbounded metadata memory. Source offers LGPL 2.1/MPL 1.1 alternatives; no production license decision made | Keep as a correctness/reference tool; do not integrate unrestricted version tested here |
| Minimal required-format C readers | Not implemented/benchmarked as a full parser; bounded HLS subset demonstrates reusable primitives only | Best control of byte/heap budgets and offset-based covers; highest parser/security/Unicode/VBR maintenance burden | Recommended production direction behind a narrow C metadata interface, with explicit correctness gate against reference corpus |

Recommendation: keep the production interface native C and enforce per-block/text/artwork-reference budgets before parsing. Reuse proven HLS text/image routines, evaluate focused C format readers in WP2 and use TagLib as a reference comparator. Do **not** add the tested unrestricted TagLib to Base merely because ordinary fixtures pass. If a bounded TagLib wrapper is preferred for implementation speed, it must first demonstrate equivalent adversarial memory/byte-read limits and supported-maintained-version/toolchain compatibility; that alternative remains open. No full custom parser has been proved yet, so this is a strategy recommendation with measured rejection criteria, not a claim the future C reader already works.

Primary sources: downloaded/pinned TagLib [build instructions](https://github.com/taglib/taglib/blob/v1.13.1/INSTALL.md), [LGPL](https://github.com/taglib/taglib/blob/v1.13.1/COPYING.LGPL) and MPL files; [FLAC metadata API](https://xiph.org/flac/api/group__flac__metadata.html), [Vorbisfile](https://xiph.org/vorbis/doc/vorbisfile/) and [libid3tag upstream mirror](https://github.com/tenacityteam/libid3tag). Actual format/runtime conclusions above come from the spike and source, not library names. AAC/ALAC remains outside initial MVP.
