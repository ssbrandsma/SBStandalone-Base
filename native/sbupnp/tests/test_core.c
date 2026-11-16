#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *origin="http://192.168.1.20:8200/description.xml";
int main(void)
{
 assert(sbupnp_url(origin,NULL));
 const char *bad[]={"http://127.0.0.1/a","http://8.8.8.8/a","file:///etc/passwd","https://192.168.1.20/a","http://192.168.1.20:22/a","http://192.168.1.20:8765/a","http://user@192.168.1.20/a","http://192.168.1.20/a\r\nx","http://0177.0.0.1/a","http://192.168.1.20/a#b"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!sbupnp_url(bad[i],NULL));
 assert(!sbupnp_url("http://192.168.1.21/a",origin));
 char url[2048];assert(sbupnp_resolve(origin,"/music/song.mp3",url,sizeof url));assert(!strcmp(url,"http://192.168.1.20:8200/music/song.mp3"));
 assert(!sbupnp_resolve(origin,"http://127.0.0.1/a",url,sizeof url));
 const char *xml="<root><device><deviceType>urn:schemas-upnp-org:device:MediaServer:2</deviceType><friendlyName>NAS &amp; Music</friendlyName><UDN>uuid:one</UDN><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:3</serviceType><controlURL>/control</controlURL><eventSubURL>/event</eventSubURL></service></serviceList></device></root>";
 sbupnp_server server;assert(sbupnp_description(xml,strlen(xml),origin,"uuid:one",&server));assert(!strcmp(server.name,"NAS & Music"));assert(!strcmp(server.control,"http://192.168.1.20:8200/control"));
 assert(!sbupnp_description(xml,strlen(xml),origin,"uuid:wrong",&server));
 const char *didl="<DIDL-Lite xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:upnp='urn:schemas-upnp-org:metadata-1-0/upnp/'><container id='opaque/$1' parentID='0' searchable='true'><dc:title>Albums</dc:title></container><item id='x&amp;y' parentID='opaque/$1'><dc:title>Track</dc:title><upnp:class>object.item.audioItem.musicTrack</upnp:class><upnp:artist>Artist</upnp:artist><res protocolInfo='http-get:*:audio/flac:*'>/wrong.flac</res><res protocolInfo='http-get:*:audio/mpeg:*' duration='0:03:12.345' size='4096'>/song.mp3</res></item></DIDL-Lite>";
 sbupnp_item *items=calloc(50,sizeof *items);assert(items);unsigned count;
 assert(sbupnp_didl(didl,strlen(didl),origin,items,50,&count));assert(count==2);assert(items[0].container&&items[0].searchable);assert(!strcmp(items[1].id,"x&y"));assert(items[1].playable);assert(items[1].duration_ms==192345);assert(strstr(items[1].uri,"/song.mp3"));
 assert(!sbupnp_didl(didl,strlen(didl),origin,items,1,&count));assert(count==0);free(items);
 const char *attack="<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><x>&x;</x>";assert(!sbupnp_xml(attack,strlen(attack)));
 assert(!sbupnp_xml("<x>\xc0\x80</x>",9));assert(!sbupnp_xml("<x><y></x>",10));
 char deep[512]="";for(unsigned i=0;i<40;i++)strcat(deep,"<x>");for(unsigned i=0;i<40;i++)strcat(deep,"</x>");assert(!sbupnp_xml(deep,strlen(deep)));
 char criteria[1024];assert(sbupnp_search("dc:title,upnp:artist","dc:title","a\" or * \\",criteria,sizeof criteria));assert(!strcmp(criteria,"dc:title contains \"a\\\" or * \\\\\""));
 assert(!sbupnp_search("upnp:album","dc:title","a",criteria,sizeof criteria));assert(!sbupnp_search("*","x injected","a",criteria,sizeof criteria));assert(sbupnp_capability(" dc:title , upnp:album ","dc:title"));assert(!sbupnp_capability("dc:titleExtra","dc:title"));
 sbupnp_registry *r=calloc(1,sizeof *r);assert(r);assert(sbupnp_registry_put(r,&server,1000,30));assert(r->revision==1);assert(sbupnp_registry_put(r,&server,2000,30));assert(r->revision==1);strcpy(server.location,"http://192.168.1.30/description.xml");assert(sbupnp_registry_put(r,&server,3000,30));assert(r->revision==2);assert(!*r->servers[1].udn);sbupnp_registry_expire(r,33000);assert(!*r->servers[0].udn);free(r);
 puts("core: URL, XML, description, DIDL, search, registry passed");return 0;
}
