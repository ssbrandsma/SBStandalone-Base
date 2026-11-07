#include "lms_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static sb_command_result parse(const char *args,sb_lms_command *c)
{
	char json[16384];int n=snprintf(json,sizeof json,"{\"data\":{\"request\":[\"\",%s]}}",args);
	assert(n>0 && (size_t)n<sizeof json);return sb_lms_command_parse(json,(size_t)n,c);
}
int main(int argc,char **argv)
{
	sb_lms_command c;
	if(argc==2){printf("%s\n",sb_lms_command_error(sb_lms_command_parse(argv[1],strlen(argv[1]),&c)));return 0;}
	assert(parse("[\"date\",\"?\"]",&c)==SB_COMMAND_OK);
	assert(parse("[\"jiveapplets\",0,50,\"target:fab4\"]",&c)==SB_COMMAND_OK);
	assert(!strcmp(sb_lms_tag(&c,"target"),"fab4"));
	assert(parse("[\"notplay\",\"stop\",\"status\"]",&c)==SB_COMMAND_UNKNOWN);
	assert(parse("[\"browselibrary\",\"items\"]",&c)==SB_COMMAND_UNSUPPORTED);
	assert(parse("[\"playlist\",\"index\",-1]",&c)==SB_COMMAND_OK);
	assert(parse("[\"playlist\",\"index\",0]",&c)==SB_COMMAND_UNSUPPORTED);
	assert(parse("[\"date\",\"target:x\",\"target:y\"]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",\":x\"]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",{}]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",true]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\" \"?\"]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",01]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",\"\\u0000stop\"]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",\"\\ud800\"]",&c)==SB_COMMAND_MALFORMED);
	assert(parse("[\"date\",\"\\ud83c\\udfb5\"]",&c)==SB_COMMAND_OK);
	assert(!sb_lms_json_valid("{\"data\":1,\"data\":2}",19));
	assert(!sb_lms_json_valid("{}\0",3));
	assert(sb_lms_command_parse("",SB_LMS_MESSAGE_MAX+1,&c)==SB_COMMAND_TOO_LARGE);
	puts("typed command parsing: passed");return 0;
}
