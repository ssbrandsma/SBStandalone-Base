#ifndef SB_PORTAL_H
#define SB_PORTAL_H
#include "mongoose.h"
int portal_https_port(void);
void portal_init(struct mg_mgr *, int http_port, mg_event_handler_t, void *);
int portal_event(struct mg_connection *, int, void *);
int portal_handle(struct mg_connection *,
		  struct mg_http_message *,
		  int internal);
int portal_host_ok(struct mg_connection *, struct mg_http_message *);
#endif
