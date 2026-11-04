#ifndef SB_TINYSC_LIFECYCLE_H
#define SB_TINYSC_LIFECYCLE_H
#include <stdint.h>
#include <sys/types.h>
#define SB_TS_MAX_PROCESSES 128
typedef enum { SB_TS_UNKNOWN,SB_TS_DETECTING,SB_TS_STOP_REQUESTED,SB_TS_STOPPING,
 SB_TS_STOPPED_VERIFIED,SB_TS_FAILED } sb_ts_state;
typedef struct { pid_t pid,parent; uint64_t start; unsigned role; char state,name[64]; } sb_ts_process;
typedef struct { unsigned count,uncertain; sb_ts_process rows[SB_TS_MAX_PROCESSES]; } sb_ts_scan;
typedef struct {
 sb_ts_state state; unsigned enabled,operational,ready,count,conflicts;
 uint64_t revision,observed_ms,stop_ms; char error[160];
 sb_ts_process processes[SB_TS_MAX_PROCESSES];
} sb_ts_status;
typedef struct {
 int enabled; unsigned poll_ms,stop_timeout_ms;
 const char *run_dir,*policy_path,*original_service,*control_path;
} sb_ts_options;
typedef struct sb_tinysc sb_tinysc;
uint64_t sb_ts_clock(void);
const char *sb_ts_state_name(sb_ts_state);
/* /proc worker operations, bounded; never invoke on the protocol loop. */
int sb_ts_detect(sb_ts_scan *);
int sb_ts_service(const sb_ts_options *,const char *action);
/* Explicit owner recovery only: policy off, no lease holder or Music process. */
int sb_ts_release(const sb_ts_options *);
sb_tinysc *sb_ts_start(const sb_ts_options *);
int sb_ts_snapshot(sb_tinysc *,sb_ts_status *);
int sb_ts_ready(sb_tinysc *);
int sb_ts_music_allowed(sb_tinysc *);
void sb_ts_operational(sb_tinysc *,int);
int sb_ts_fd(sb_tinysc *);
void sb_ts_drain(sb_tinysc *);
void sb_ts_close(sb_tinysc *);
int sb_ts_json(sb_tinysc *,char *,unsigned);
#endif
