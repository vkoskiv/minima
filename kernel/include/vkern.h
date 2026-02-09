#include <stddef.h> // FIXME
#include "../stdint.h"
#include "../utils.h"
#include "../panic.h"

#define v_nostd_memcpy(dst, src, n) memcpy(dst, src, n)
#define v_nostd_memset(s, c, n) memset(s, c, n)
#define v_nostd_abort() panic()

#define V_NOSTDLIB
#include <v.h>
