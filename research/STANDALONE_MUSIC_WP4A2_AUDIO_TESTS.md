# WP4a.2 audio evidence

| Test | Result |
| --- | --- |
| Native MP3 GET/RESP/STAT/STMs | Not run on Touch |
| Native MP3 heard | Not tested; no stream started |
| Native FLAC heard/codec | Not tested |
| Real native-library Jive browsing | Not tested |
| Native queue/pause/next/previous/gap | Not tested |
| Physical owner release | Not qualified |
| Analogue / optical / volume / standby | Not qualified; output settings not changed |
| Spotify before test | User confirmed it was playing; stopped through app UI with permission |
| Radio recovery | NPO Radio1 selected through real UI; MP3 decoder start, HTTP200, ICY metadata and active Now Playing observed |

Radio recovery audible confirmation is separate from protocol logs. No native Music audible claim is made. Further Music tests were cancelled when the Jive probe rebooted the Touch and TinySC started. FLAC/queue tests did not follow because MP3 qualification never occurred.

Final recovery observation: NPO Radio1 was stopped through the existing long-pause/Stop action. Final Base metadata is inactive/stopped; native services and Jive remain running, TinySC/Music are absent, and the test RAM volume is absent. The temporary authenticated remote session was logged out and its local cookie removed. The owner replied after the stop that nothing was playing and Now Playing was empty; this matches the stopped end state and does not confirm sound during the earlier sample. Radio audible recovery remains unverified.
