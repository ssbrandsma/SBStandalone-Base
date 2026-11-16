/* Place unterminated mg_str inputs against an inaccessible page. */
#include "mongoose.h"
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
  long page = sysconf(_SC_PAGESIZE);
  assert(page > 0);
  char *area = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert(area != MAP_FAILED);
  assert(mprotect(area + page, (size_t)page, PROT_NONE) == 0);
  const char *cases[] = {".", "/.", "x/.", "..", "/..", "x/..", "ok", "/ok"};
  const bool expected[] = {true, true, true, false, false, false, true, true};
  assert(!mg_path_is_sane(mg_str_n(NULL, 0)));
  assert(!mg_path_is_sane(mg_str_n(area + page, 0)));
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    size_t n = strlen(cases[i]);
    char *p = area + page - n;
    memcpy(p, cases[i], n);
    assert(mg_path_is_sane(mg_str_n(p, n)) == expected[i]);
  }
  assert(munmap(area, (size_t)page * 2) == 0);
  return 0;
}
