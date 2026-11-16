#ifndef SB_HTTP_H
#define SB_HTTP_H
int http_server_run(int port, const char *root);
void http_server_request_stop(void);
#endif
