/* WP0 isolated feasibility tool. Never linked into production Base. */
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <new>
#include <string>

static size_t calls, live, peak;
union Header { std::max_align_t alignment; size_t bytes; };
void *operator new(size_t n) {
  Header *p=static_cast<Header *>(std::malloc(sizeof(Header)+n));
  if(!p)throw std::bad_alloc();p->bytes=n;calls++;live+=n;if(live>peak)peak=live;return p+1;
}
void operator delete(void *p) noexcept { if(p){Header *h=static_cast<Header *>(p)-1;live-=h->bytes;std::free(h);} }
void *operator new[](size_t n){return ::operator new(n);}
void operator delete[](void *p) noexcept {::operator delete(p);}
void operator delete(void *p,size_t) noexcept {::operator delete(p);}
void operator delete[](void *p,size_t) noexcept {::operator delete(p);}
static void quoted(const std::string &s) {
  std::putchar('"');for(unsigned char c:s){if(c=='"'||c=='\\')std::putchar('\\');if(c<32)std::printf("\\u%04x",c);else std::putchar(c);}std::putchar('"');
}
int main(int argc,char **argv) {
  if(argc!=3)return 2;
  unsigned iterations=static_cast<unsigned>(std::strtoul(argv[2],NULL,10));
  if(!iterations || iterations>10000)return 2;
  struct rlimit cap={64u*1024u*1024u,64u*1024u*1024u};setrlimit(RLIMIT_AS,&cap);
  struct stat st;if(stat(argv[1],&st)||st.st_size>2*1024*1024)return 3;
  clock_t start=clock();std::string title,artist,album;int rate=0,channels=0,bitrate=0,duration=0;unsigned art=0;bool valid=false;
  try {
    for(unsigned i=0;i<iterations;i++) {
      TagLib::FileRef file(argv[1],true,TagLib::AudioProperties::Accurate);
      valid=!file.isNull() && file.file()->isValid();art=0;
      if(!valid)continue;
      title=file.tag()->title().to8Bit(true);artist=file.tag()->artist().to8Bit(true);album=file.tag()->album().to8Bit(true);
      if(file.audioProperties()){rate=file.audioProperties()->sampleRate();channels=file.audioProperties()->channels();bitrate=file.audioProperties()->bitrate();duration=file.audioProperties()->lengthInMilliseconds();}
      TagLib::MPEG::File *mp3=dynamic_cast<TagLib::MPEG::File *>(file.file());
      if(mp3 && mp3->ID3v2Tag()) {
        TagLib::ID3v2::FrameList pictures=mp3->ID3v2Tag()->frameListMap()["APIC"];
        if(!pictures.isEmpty())art=static_cast<TagLib::ID3v2::AttachedPictureFrame *>(pictures.front())->picture().size();
      }
      TagLib::FLAC::File *flac=dynamic_cast<TagLib::FLAC::File *>(file.file());
      if(flac && !flac->pictureList().isEmpty())art=flac->pictureList().front()->data().size();
      TagLib::Ogg::Vorbis::File *ogg=dynamic_cast<TagLib::Ogg::Vorbis::File *>(file.file());
      if(ogg && !ogg->tag()->pictureList().isEmpty())art=ogg->tag()->pictureList().front()->data().size();
    }
  } catch(const std::bad_alloc &) { return 4; }
  double cpu_us=(clock()-start)*1e6/CLOCKS_PER_SEC/iterations;
  long peak_rss=0;FILE *status=std::fopen("/proc/self/status","r");
  if(status){char line[128];while(std::fgets(line,sizeof line,status))if(std::sscanf(line,"VmHWM: %ld",&peak_rss)==1)break;std::fclose(status);}
  std::printf("{\"valid\":%s,\"title\":",valid?"true":"false");quoted(title.size()>256?title.substr(0,256):title);std::printf(",\"artist\":");quoted(artist);std::printf(",\"album\":");quoted(album);
  std::printf(",\"sample_rate\":%d,\"channels\":%d,\"bitrate_kbps\":%d,\"duration_ms\":%d,\"art_bytes\":%u,\"iterations\":%u,\"cpu_us_per_file\":%.3f,\"peak_rss_kib\":%ld,\"cpp_new_calls\":%zu,\"cpp_new_peak_live_bytes\":%zu}\n",rate,channels,bitrate,duration,art,iterations,cpu_us,peak_rss,calls,peak);
}
