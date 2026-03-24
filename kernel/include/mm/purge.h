//
// purge.h - register purgeable memory
//

#ifndef _PURGE_H_
#define _PURGE_H_

#include <stddef.h>

struct purgeable;

// If on_purge is not NULL, it is called with on_purge_ctx as its argument just before
// freeing this buffer during a purge. The purgeable region is locked being purged is locked
// during the callback, so cleanup code can run.
struct purgeable *purgeable_alloc(size_t bytes, void (*on_purge)(void *ctx), void *on_purge_ctx);
void purgeable_free(struct purgeable *);

// acquire/release lock on purgeable region.
// purgeable_get() sets the value pointed to by was_purged to indicate
// if the region was purged since it was last accessed. Pass NULL if you
// don't care about that.
void *purgeable_get(struct purgeable *, int *was_purged);
void purgeable_put(void *);

// Purge everything if bytes == 0. Otherwise purge at least `bytes` number of bytes.
// Returns number of bytes purged, which is guaranteed to be >= bytes.
size_t purgeable_purge(size_t bytes);

#endif
