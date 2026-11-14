#include "lms_command.h"
#include "nowplaying.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

typedef struct { const unsigned char *p, *end; unsigned tokens; } cursor;
static void space(cursor *c) { while (c->p < c->end && *c->p && strchr(" \t\r\n", *c->p)) c->p++; }
static int hex(unsigned char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; }
static int unicode(cursor *c)
{
	int n = 0;
	for (unsigned i=0;i<4;i++) { if (c->p == c->end || hex(*c->p)<0) return -1; n=n*16+hex(*c->p++); }
	return n;
}
static int string(cursor *c, int key)
{
	if (c->p == c->end || *c->p++ != '"') return 0;
	while (c->p < c->end) {
		unsigned ch=*c->p++;
		if (ch=='"') return 1;
		if (ch<32) return 0;
		if (ch=='\\') {
			if (key || c->p == c->end) return 0; /* protocol keys are literal ASCII */
			ch=*c->p++;
			if (!ch) return 0;
			if (ch=='u') {
				int u=unicode(c); if (u<=0 || (u>=0xdc00 && u<=0xdfff)) return 0;
				if (u>=0xd800 && u<=0xdbff) {
					if (c->end-c->p<6 || c->p[0]!='\\' || c->p[1]!='u') return 0;
					c->p+=2;u=unicode(c);if(u<0xdc00 || u>0xdfff)return 0;
				}
			} else if (!strchr("\"\\/bfnrt",ch)) return 0;
		} else if (ch>=128) {
			unsigned n=ch>=0xc2 && ch<=0xdf ? 1 : ch>=0xe0 && ch<=0xef ? 2 : ch>=0xf0 && ch<=0xf4 ? 3 : 0;
			if (key || !n || (size_t)(c->end-c->p)<n) return 0;
			if ((ch==0xe0 && c->p[0]<0xa0) || (ch==0xed && c->p[0]>=0xa0) ||
			    (ch==0xf0 && c->p[0]<0x90) || (ch==0xf4 && c->p[0]>=0x90)) return 0;
			for(unsigned i=0;i<n;i++)if((*c->p++ & 0xc0)!=0x80)return 0;
		}
	}
	return 0;
}
static int value(cursor *c, unsigned depth, unsigned budget)
{
	space(c); if (c->p==c->end || depth>16 || ++c->tokens>budget) return 0;
	if (*c->p=='"') return string(c,0);
	if (*c->p=='{' || *c->p=='[') {
		unsigned char open=*c->p++, close=open=='{'?'}':']';
		const unsigned char *keys[32];size_t lengths[32];unsigned count=0;
		space(c);if(c->p<c->end && *c->p==close){c->p++;return 1;}
		for (;;) {
			if(open=='{') {
				const unsigned char *start=c->p;
				if(count==32 || !string(c,1))return 0;
				size_t n=(size_t)(c->p-start);
				for(unsigned i=0;i<count;i++)if(n==lengths[i] && !memcmp(start,keys[i],n))return 0;
				keys[count]=start;lengths[count++]=n;
				space(c);if(c->p==c->end || *c->p++!=':')return 0;
			}
			if(!value(c,depth+1,budget))return 0;
			space(c);if(c->p==c->end)return 0;
			if(*c->p==close){c->p++;return 1;}
			if(*c->p++!=',')return 0;
			space(c);
		}
	}
	for(unsigned i=0;i<3;i++) {
		const char *literal=i==0?"true":i==1?"false":"null";size_t n=strlen(literal);
		if((size_t)(c->end-c->p)>=n && !memcmp(c->p,literal,n)){c->p+=n;return 1;}
	}
	if(*c->p=='-')c->p++;
	if(c->p==c->end || !isdigit(*c->p))return 0;
	if(*c->p=='0')c->p++;else while(c->p<c->end && isdigit(*c->p))c->p++;
	if(c->p<c->end && *c->p=='.'){c->p++;if(c->p==c->end || !isdigit(*c->p))return 0;while(c->p<c->end && isdigit(*c->p))c->p++;}
	if(c->p<c->end && (*c->p=='e'||*c->p=='E')){c->p++;if(c->p<c->end && (*c->p=='+'||*c->p=='-'))c->p++;if(c->p==c->end || !isdigit(*c->p))return 0;while(c->p<c->end && isdigit(*c->p))c->p++;}
	return 1;
}
int sb_lms_json_valid_budget(const char *json, size_t length, unsigned budget)
{
	if(!json || !length || length>131072u)return 0;
	cursor c={(const unsigned char *)json,(const unsigned char *)json+length,0};
	if(!value(&c,0,budget))return 0;
	space(&c);return c.p==c.end;
}
/* Persisted repository state has a larger bound than network commands. */
int sb_lms_json_valid_catalog(const char *json, size_t length)
{
 if(!json || !length || length>512u*1024u)return 0;
 cursor c={(const unsigned char *)json,(const unsigned char *)json+length,0};
 if(!value(&c,0,32768))return 0;
 space(&c);return c.p==c.end;
}
int sb_lms_json_valid(const char *json, size_t length)
{
	if(!json || !length || length>131072u)return 0;
	cursor c={(const unsigned char *)json,(const unsigned char *)json+length,0};
	if(!value(&c,0,4096))return 0;
	space(&c);return c.p==c.end;
}
const char *sb_lms_tag(const sb_lms_command *cmd,const char *key)
{
	size_t n=strlen(key);
	for(unsigned i=1;i<cmd->count;i++)if(!strncmp(cmd->arguments[i],key,n) && cmd->arguments[i][n]==':')return cmd->arguments[i]+n+1;
	return NULL;
}
sb_command_result sb_lms_command_parse(const char *json,size_t length,sb_lms_command *cmd)
{
	static const char *supported[]={"power","mixer","connect","sleep","sleepsettings","alarm","alarms","alarmsettings","jiveupdatealarm","jiveupdatealarmdays","jivealarmvolume","jivealarm","status","songinfo","contextmenu","trackinfo","serverstatus","firmwareupgrade","date","jiveapplets","menustatus","menu","pause","play","stop","playlist","jiveblankcommand"};
	static const char *future[]={"browselibrary","artists","albums","titles","tracks","genres","musicfolder","search","playlists","playlistcontrol","rescan","time","mixer","info"};
	if(!cmd)return SB_COMMAND_MALFORMED;
	memset(cmd,0,sizeof *cmd);
	if(length>SB_LMS_MESSAGE_MAX)return SB_COMMAND_TOO_LARGE;
	if(!sb_lms_json_valid(json,length))return SB_COMMAND_MALFORMED;
	int count=np_lms_arguments(json,length,cmd->player,sizeof cmd->player,cmd->arguments,SB_LMS_ARGUMENTS_MAX);
	if(count<1 || !cmd->arguments[0][0])return SB_COMMAND_MALFORMED;
	cmd->count=(unsigned)count;
	if(!strcmp(cmd->arguments[0],"power")||!strcmp(cmd->arguments[0],"mixer")){
	 int power=!strcmp(cmd->arguments[0],"power");
	 if(power&&count>1&&strcmp(cmd->arguments[1],"0")&&strcmp(cmd->arguments[1],"1")&&strcmp(cmd->arguments[1],"?"))return SB_COMMAND_MALFORMED;
	 if(!power&&(count<2||strcmp(cmd->arguments[1],"volume")))return SB_COMMAND_UNSUPPORTED;
	 if(!power&&count>2&&strcmp(cmd->arguments[2],"?")){
	  char *end;errno=0;long volume=strtol(cmd->arguments[2],&end,10);
	  if(errno||!cmd->arguments[2][0]||*end||volume < -100||volume>100)return SB_COMMAND_MALFORMED;
	 }
	 const char *seq=sb_lms_tag(cmd,"seq_no");
	 if(seq){char *end;errno=0;unsigned long long value=strtoull(seq,&end,10);
	  if(errno||!isdigit((unsigned char)*seq)||*end||value>UINT32_MAX)return SB_COMMAND_MALFORMED;
	 }
	}
	for(const char *p=cmd->arguments[0];*p;p++)if(!islower((unsigned char)*p) && *p!='_')return SB_COMMAND_MALFORMED;
	for(unsigned i=1;i<cmd->count;i++) {
		const char *colon=strchr(cmd->arguments[i],':');
		if(!colon)continue;
		if(colon==cmd->arguments[i])return SB_COMMAND_MALFORMED;
		for(const char *p=cmd->arguments[i];p<colon;p++)if(!isalnum((unsigned char)*p) && !strchr("_.-",*p))return SB_COMMAND_MALFORMED;
		for(unsigned j=1;j<i;j++)if(!strncmp(cmd->arguments[j],cmd->arguments[i],(size_t)(colon-cmd->arguments[i])+1))return SB_COMMAND_MALFORMED;
	}
	if(!strcmp(cmd->arguments[0],"playlist") && (count!=3 || strcmp(cmd->arguments[1],"index") ||
	   (strcmp(cmd->arguments[2],"-1") && strcmp(cmd->arguments[2],"+1") && strcmp(cmd->arguments[2],"1"))))return SB_COMMAND_UNSUPPORTED;
	for(size_t i=0;i<sizeof supported/sizeof *supported;i++)if(!strcmp(cmd->arguments[0],supported[i]))return SB_COMMAND_OK;
	for(size_t i=0;i<sizeof future/sizeof *future;i++)if(!strcmp(cmd->arguments[0],future[i]))return SB_COMMAND_UNSUPPORTED;
	return SB_COMMAND_UNKNOWN;
}
const char *sb_lms_command_error(sb_command_result r)
{
	return r==SB_COMMAND_TOO_LARGE?"command_too_large":r==SB_COMMAND_MALFORMED?"malformed_command":r==SB_COMMAND_UNSUPPORTED?"unsupported_command":r==SB_COMMAND_UNKNOWN?"unknown_command":"";
}
