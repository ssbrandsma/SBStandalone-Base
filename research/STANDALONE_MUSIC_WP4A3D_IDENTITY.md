# Identity observations

No identity rule changed in WP4a.3d. `executable_identity.inc` continues to require
live executable bytes and kernel correlation. The legacy process has not received
a special trusted-content classification.

Fresh direct observations: Base PID 3933/start 17600372, parent 1, state S,
command line selecting browse-only with its original startup scan argument.
Its executable mapping is still device 00:0b/inode 1992, at the canonical
`sbbase-music` path. Direct `cat /proc/3933/exe` fails (recorded exit 1).
This shell observation does not independently record errno; ENOENT was established
by WP4a.3c's native probe. **Base remains LEVEL 3**, no fresh bytes hashed.

The installed pathname remains 932644 bytes/SHA-256
`682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c`.
That is installed-file evidence only. No new generation or new live process exists.

Other installed hashes freshly read:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| sbproxy | 874644 | a74524626cdb1a0ac82778b79cedc1982ffe3f3076185a80017cc68c3aea90de |
| sbwebserver | 291976 | 4f33d5890ad41b540f048096e71f1e87333529870eb3d6503a920f064d7ae8eb |
| sbbase-core | 173148 | 5754f168a141af7b359912431a1449c1a5c21eb40b5c09e64365eae8dcd14421 |
| sbbase wrapper | 590 | 492f3fd3dc93c33746e32c792ddda0f00ad39aa234a97f51f23ab1cdf6732e41 |

Proxy/web LEVEL 1 measurements belong to WP4a.3c. This phase checked their stable
PID/start and installed bytes but did not rerun the native live-hash verifier.
No new LEVEL 1 claim is made for them. Full legacy replacement-target admission,
including a fresh native listener/role/watchdog proof, remains outstanding.
