#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include "sock.h"
#include "uri.h"
#include <arpa/inet.h>
#include <string.h>
/* Linker wrappers keep SDK SOAP/GENA parsing and delivery on the same bounded
 * policy as our control point, including SDK-internal event notifications. */
int __wrap_ixmlParseBufferEx(const char *buffer,IXML_Document **out)
{
 if(!out)return IXML_INVALID_PARAMETER;
 *out=NULL;if(!buffer)return IXML_INVALID_PARAMETER;
 size_t n=strnlen(buffer,SBUPNP_XML_MAX+1);if(n>SBUPNP_XML_MAX)return IXML_SYNTAX_ERR;
 *out=sbupnp_xml(buffer,n);return *out?IXML_SUCCESS:IXML_SYNTAX_ERR;
}
int __wrap_gena_validate_delivery_urls(SOCKINFO *info,URL_list *urls)
{
 if(!info||!urls||urls->size!=1||info->foreign_sockaddr.ss_family!=AF_INET)return -1;
 uri_type *uri=&urls->parsedURLs[0];
 if(uri->scheme.size!=4||memcmp(uri->scheme.buff,"http",4)||!uri->hostport.text.size||uri->hostport.text.size>=64)return -1;
 char host[64];memcpy(host,uri->hostport.text.buff,uri->hostport.text.size);host[uri->hostport.text.size]=0;
 char *colon=strchr(host,':');if(colon)*colon=0;
 struct in_addr address;if(inet_pton(AF_INET,host,&address)!=1||address.s_addr!=((struct sockaddr_in *)&info->foreign_sockaddr)->sin_addr.s_addr)return -1;
 uint32_t ip=ntohl(address.s_addr);if(!((ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8||(ip>>16)==0xa9fe))return -1;
 unsigned port=ntohs(uri->hostport.port);if(port<1024||port==3483||port==8765||port==9000)return -1;
 return 0;
}
