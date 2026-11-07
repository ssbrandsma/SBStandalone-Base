/* Existing parser comparator: intentionally only ID3v2.3/v2.4 live fields. */
#include "../../native/sbproxy/hls_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
static unsigned events;
static hls_metadata output;
static void emit(void *unused,const hls_metadata *m){(void)unused;output=*m;events++;}
int main(int argc,char **argv)
{
	if(argc!=3)return 2;
	unsigned iterations=(unsigned)strtoul(argv[2],NULL,10);if(!iterations||iterations>10000)return 2;
	hls_id3 *parser=calloc(1,sizeof *parser);if(!parser)return 3;
	unsigned char bytes[4096];clock_t start=clock();
	for(unsigned i=0;i<iterations;i++) {
		FILE *f=fopen(argv[1],"rb");if(!f){free(parser);return 3;}
		hls_id3_init(parser,emit,NULL);size_t n;
		while((n=fread(bytes,1,sizeof bytes,f)))hls_id3_feed(parser,bytes,n);
		fclose(f);
	}
	long peak_rss=0;FILE *status=fopen("/proc/self/status","r");
	if(status){char line[128];while(fgets(line,sizeof line,status))if(sscanf(line,"VmHWM: %ld",&peak_rss)==1)break;fclose(status);}
	printf("{\"events\":%u,\"title_matches_unicode\":%s,\"art_bytes\":%zu,\"iterations\":%u,\"cpu_us_per_file\":%.3f,\"peak_rss_kib\":%ld,\"explicit_heap_allocations\":1,\"explicit_heap_bytes\":%zu}\n",events,!strcmp(output.title,"WP0 Caf\xc3\xa9 \xf0\x9f\x8e\xb5")?"true":"false",(size_t)output.art_size,iterations,(clock()-start)*1e6/CLOCKS_PER_SEC/iterations,peak_rss,sizeof *parser);
	free(parser);return 0;
}
