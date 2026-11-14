#ifndef SB_REPOSITORY_XML_H
#define SB_REPOSITORY_XML_H
#include "repository.h"
#define REPO_XML_LIMIT (256u*1024u)
#define REPO_XML_ITEMS 64
#define REPO_XML_TOTAL 96
typedef struct {unsigned discovered,skipped;char diagnostic[160];} RepoXmlStats;
int repo_xml_parse(const char *,size_t,const char *,RepoApplet *,int *,RepoXmlStats *,char *,size_t);
int repo_xml_url(const char *,char *,size_t);
int repo_xml_resolve(const char *,const char *,char *,size_t);
#endif
