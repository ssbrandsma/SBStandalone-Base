/* Host process-lifecycle fixture only. No decoder or firmware API. */
#include <unistd.h>
int main(void) { for (;;) pause(); }
