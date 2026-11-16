#ifndef SB_VPN_HTTP_H
#define SB_VPN_HTTP_H
#include "mongoose.h"
int vpn_http_is_api(struct mg_str);
void vpn_http_handle(struct mg_connection *,
		     struct mg_http_message *,
		     int,
		     int);
int vpn_http_headers(struct mg_connection *, int, void *);
void vpn_http_tick(struct mg_mgr *);
#endif
