#ifndef MINIZ_H
#define MINIZ_H

// Minimal wrapper header to satisfy miniz_tinfl.c's include.
// Only the low-level inflate API is used by ESPAsyncWebClient.

#ifndef MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#ifndef MINIZ_NO_TIME
#define MINIZ_NO_TIME
#endif

#include "miniz_common.h"
#include "miniz_tinfl.h"

#endif // MINIZ_H

