# Release candidate dependency and source audit

2026-11-15. Applies to Base 0.7.4-rc1 with sbupnp 0.3.0, both targets.
**Local review artifacts; distribution clearance is blocked.** This is an
inventory of the inspected files and build inputs, not a commercial license grant.

| Actual component | License evidence | Source supplied with this candidate |
| --- | --- | --- |
| sbupnp original code | root MIT LICENSE | committed UPnP source archive |
| pupnp/ixml 22.2.2 | Intel BSD-3-Clause COPYING plus source author notices | exact `fac0494691434c35243131f66a0b27ea0c29f700` archive, unmodified |
| Base original modules | Base MIT LICENSE; copied components retain their own terms | committed Base source archive |
| web Mongoose 7.17 | embedded GPL-2.0-only or commercial header; no commercial grant assumed | exact vendored mongoose.c/h in Base archive, web source/build scripts; GPL-2.0 text in ZIP |
| SBWebserver | adjacent GPL-2.0 LICENSE | complete tracked web sources/assets in Base archive |
| curl 8.18.0 | supplied source COPYING (curl license) | supplied source/configuration archive and consumed static-library hash |
| wolfSSL 5.8.2 | supplied LICENSING: GPLv3-or-later or commercial; GPL route assumed for review | supplied source archive, Base build-proxy-tls.sh/export-proxy-tls.cmake, generated options and consumed library hash |
| SBHttpsProxy/sbproxy original code | owner confirmed it was developed by this project and is part of Base; explicit adjacent MIT LICENSE added | exact proxy/HLS source and MIT notice in Base archive; GPLv3-or-later wolfSSL terms remain separate and supplied |
| musl 1.1.24 | MIT and file-specific permissive notices in MUSL-COPYRIGHT | SDK identity and library/compiler hashes; unmodified notice retained |
| GCC 8.4.0 libgcc | GPLv3 plus GCC Runtime Library Exception 3.1 | exception and GPL text, compiler identity/hash, flags retained |
| zlib 1.3.2 | zlib license | exact `da607da739fa6047df13e66a2af6b8bec7c2a498` source archive |
| libpng 1.6.59 | PNG reference library license, retained in SVG notice and source | exact `cd952f49f95bb27154ae77dbb103032d95f6e580` archive |
| libjpeg-turbo 3.2.0 | IJG/BSD/zlib file-specific notices | exact `c85e6b905bf237038faa936dab160ebfc5da0344` archive |
| NanoSVG `239e102` / stb `2c980` | zlib / MIT-public-domain alternatives in headers | actual vendored headers and SVG-THIRD-PARTY-NOTICES in Base/archive and ZIP |
| SQLite 3.54.1 | public-domain header and PROVENANCE.json | actual amalgamation, SHA-256 provenance and build script in Base archive; network-only profile opens no database |
| yxml | MIT COPYING | actual vendored source, YXML-LICENSE in ZIP |
| uPlot | MIT LICENSE | actual JS/CSS/license in Base archive and ZIP |
| Mozilla CA bundle, 2026-09-25 | curl CA Extract identifies MPL 2.0; matching dated archive verified against its published SHA-256 | exact upstream LF PEM in committed Base archive; CRLF-only package conversion; MPL-2.0 and CA-BUNDLE-NOTICE in ZIP |
| Baby sbubifs-authorized.ko | module GPL-2.0; exact SHA-256 `63652ce67df06a78abb84a4986253bdab02fbd7b7c000779c60b3d393ba9566b` | build script/SB patch in Base; **exact prepared vendor kernel, .config, Module.symvers and full matching source not recovered** |
| Baby empty UBIFS image | filesystem data, not a shipped mtd-utils executable | tracked image and creation documentation; historic generator binaries excluded from source export |

The ARM executables are statically linked. The proxy combines curl, wolfSSL,
musl and libgcc; wolfSSL is not BSD or GPLv2. Web/Mongoose and the proxy are
separate executables communicating through existing interfaces. Their presence
in one ZIP does not make every component MIT. Original proxy ownership was
clarified by the project owner on 2026-11-15 and recorded in its adjacent MIT
LICENSE; third-party copyright and license terms remain intact.

No GMediaRender, GStreamer, LMS, LMS-uPnP or Spotify/Qobuz implementation is
linked into sbupnp. Original Jive code was inspected as a behavioral reference;
the external DB contract test loads an independently obtained reference file.
Source archives retain vendored copyright headers and complete available
upstream notices. ZIPs contain applicable runtime notices, GPL texts and this
hold notice. Reused SDK libraries are identified by their consumed hashes;
they were not silently represented as freshly rebuilt third-party dependencies.

## Explicit release blockers

1. Recover and verify the *exact* source/configuration for the authorized Baby
   module. The original WSL source filesystem currently fails with I/O errors.
   The available SqueezeOS checkout and SB patch are useful inputs, but are not
   proof of complete corresponding source for that binary. No replacement kernel
   module was built or loaded on a device during this assignment.

The former proxy provenance gap is closed by that owner clarification and the
explicit MIT notice. The candidate retains wolfSSL's GPLv3-or-later terms,
complete supplied source and actual build configuration; no commercial license
or blanket MIT relicensing of linked dependencies is claimed.

The former CA provenance gap was resolved against the primary
[curl CA Extract page](https://curl.se/docs/caextract.html), its dated
[2026-09-25 PEM](https://curl.se/ca/cacert-2026-09-25.pem) and published checksum.
Committed PEM SHA-256 is
`a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`;
the package differs only in CRLF line endings. The complete
[Mozilla MPL 2.0 text](https://www.mozilla.org/media/MPL/2.0/index.txt) and a source
availability notice accompany it. This closes that documented file/notice gap;
it does not resolve the separate module blocker.

Consequently the accompanying archives are **available-source review bundles**,
not a certification of complete corresponding source for the whole Baby ZIP.
Fab4 omits the Baby module/image and has no remaining identified source-inventory
gap from this audit. Public publication was not requested or performed. The
separate physical activation/recovery gates remain for both targets. For Baby,
after resolving the module source gap,
regenerate the immutable manifest and packages; do not reuse old checksums.
