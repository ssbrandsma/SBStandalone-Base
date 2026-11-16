# Standalone UPnP component

Imported from SBupnpdlna revision 75c9348712fc761728bdb6460d29424f74ae1d5c.
Original daemon code is MIT licensed; see SBUPNP-LICENSE.

pupnp/ixml 22.2.2 is pinned to
fac0494691434c35243131f66a0b27ea0c29f700:
https://github.com/pupnp/pupnp/tree/fac0494691434c35243131f66a0b27ea0c29f700
Its Intel BSD three-clause COPYING is supplied as PUPNP-LICENSE. Additional
individual source copyright/license blocks are retained in PUPNP-SOURCE-NOTICES.
The SDK is built without source patches; link wrappers retain the existing
bounded XML and same-controller GENA delivery policy.

The existing ARM SDK supplies musl 1.1.24 and GCC 8.4.0/libgcc. Their unmodified
MUSL-COPYRIGHT, GCC-RUNTIME-EXCEPTION and GPL-3.0 texts accompany the component.
These notices cover this optional daemon, not a new license for the rest of Base.
No GMediaRender, GStreamer, LMS, Spotify or Qobuz implementation is included.
