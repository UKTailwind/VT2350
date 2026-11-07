/* heap.h: what -Dmalloc=h_malloc needs declared, before anything else is included */
#include <stddef.h>
void *h_malloc(size_t n);
void h_free(void *p);
