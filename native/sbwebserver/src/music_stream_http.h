#ifndef SB_MUSIC_STREAM_HTTP_H
#define SB_MUSIC_STREAM_HTTP_H
#include "mongoose.h"
int sb_web_music_init(int private_ipc);
int sb_web_music_event(struct mg_connection *, int event, void *data);
void sb_web_music_tick(void);
int sb_web_music_enabled(void);
#endif
