# Unqualified Fab4 reliability prototype

Do not deploy these files. qualification.json is false and the host CLI refuses installation. Factory boot can erase the overlay on persistent mount failure, so required fail-closed protection was not achieved.

See research/STANDALONE_MUSIC_WP4A2A_REPORT.md. These files are excluded from normal packaging. No restart/reboot helper is provided or qualified. The recovery CLI performs file-level recovery only and preserves existing guards by default.
