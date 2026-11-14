#ifndef SB_REPOSITORY_SOURCES_H
#define SB_REPOSITORY_SOURCES_H
#include "mongoose.h"
int repo_sources_is_api(struct mg_str);
void repo_sources_handle(struct mg_connection *,struct mg_http_message *);
void repo_sources_tick(struct mg_mgr *);
void repo_sources_reconcile(void);
int repo_sources_list(char *,size_t);
int repo_sources_download(struct mg_connection *,struct mg_http_message *);
int repo_sources_headers(struct mg_connection *,int,void *);
#endif
