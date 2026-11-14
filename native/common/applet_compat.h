#ifndef SB_APPLET_COMPAT_H
#define SB_APPLET_COMPAT_H
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Numeric runs are compared by magnitude, never machine integer conversion. */
static inline int sb_applet_version_compare(const char *a, const char *b) {
 while (*a || *b) {
  while (*a && strchr("._-+",*a)) a++;
  while (*b && strchr("._-+",*b)) b++;
  if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
   const char *ae=a,*be=b;while(isdigit((unsigned char)*ae))ae++;while(isdigit((unsigned char)*be))be++;
   while(a<ae&&*a=='0')a++;
   while(b<be&&*b=='0')b++;
   if(ae-a!=be-b)return ae-a>be-b?1:-1;
   int n=memcmp(a,b,(size_t)(ae-a));if(n)return n>0?1:-1;a=ae;b=be;
  } else {
   if(!*a||!*b)return *a?1:*b?-1:0;
   if(*a!=*b)return (unsigned char)*a>(unsigned char)*b?1:-1;
   a++;b++;
  }
 }
 return 0;
}
static inline int sb_applet_bound(const char *v,const char *bound,int upper) {
 if(!*bound||!strcmp(bound,"*"))return 1;
 const char *wild=strchr(bound,'*');
 if(wild) {size_t n=(size_t)(wild-bound);if(n&&bound[n-1]=='.')n--;
  char prefix[64],vp[64];if(n>=sizeof prefix)return 0;memcpy(prefix,bound,n);prefix[n]=0;
  size_t j=0;unsigned dots=0,need=0;for(size_t i=0;i<n;i++)if(bound[i]=='.')need++;
  while(v[j]&&j+1<sizeof vp){if(v[j]=='.'&&dots++==need)break;vp[j]=v[j];j++;}vp[j]=0;
  int c=sb_applet_version_compare(vp,prefix);return upper?c<=0:c>=0;
 }
 int c=sb_applet_version_compare(v,bound);return upper?c<=0:c>=0;
}
static inline int sb_applet_compatible(const char *target,const char *device,const char *min,const char *max,const char *fw) {
 int ok=!*target||!strcmp(target,"*");const char *p=target;
 while(*p){const char *e=strchr(p,'|');size_t n=e?(size_t)(e-p):strlen(p);if(n==strlen(device)&&!memcmp(p,device,n))ok=1;if(!e)break;p=e+1;}
 return ok&&(!*fw||(sb_applet_bound(fw,min,0)&&sb_applet_bound(fw,max,1)));
}
static inline void sb_applet_snapshot_path(const char *config,char *out,size_t cap) {
 const char *s=strrchr(config,'/');snprintf(out,cap,"%.*s%sapplet-sources.json",s?(int)(s-config):0,config,s?"/":"");
}
static inline int sb_applet_config_stamp(const char *path,char *out,size_t cap) {
 struct stat s;if(stat(path,&s))return -1;
 snprintf(out,cap,"%llu:%llu:%llu:%llu",(unsigned long long)s.st_ino,(unsigned long long)s.st_size,(unsigned long long)s.st_mtime,(unsigned long long)s.st_ctime);return 0;
}
#endif
