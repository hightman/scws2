/* scws2 public API. See COPYING for the BSD license. */
#ifndef SCWS2_H
#define SCWS2_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "scws2_version.h"
typedef struct scws2 *scws2_t;
typedef struct scws2_result *scws2_result_t;
typedef enum {
	SCWS2_OK = 0, SCWS2_EINVAL, SCWS2_ENOMEM, SCWS2_EIO, SCWS2_EFORMAT,
	SCWS2_EUTF8, SCWS2_ELIMIT, SCWS2_ENOTSUP
} scws2_status_t;
typedef unsigned scws2_dict_flags_t;
enum { SCWS2_DICT_MEMORY = 1u << 0, SCWS2_DICT_TEXT = 1u << 1 };
typedef enum { SCWS2_STANDARD = 0, SCWS2_SEARCH = 1 } scws2_mode_t;
typedef enum { SCWS2_PRIMARY = 0, SCWS2_SECONDARY = 1, SCWS2_FALLBACK = 2 } scws2_kind_t;
typedef enum { SCWS2_DICTIONARY = 0, SCWS2_BUILTIN = 1, SCWS2_UNKNOWN = 2 } scws2_origin_t;
enum { SCWS2_KEEP_SYMBOLS = 1u, SCWS2_FALLBACK_BIGRAM = 2u };

typedef struct {
	size_t offset, length;             /* Original UTF-8 byte span. */
	size_t char_offset, char_length;   /* Unicode scalar span, not engine positions. */
	char pos[3];                      /* At most two ASCII characters plus NUL. */
	scws2_kind_t kind;
	scws2_origin_t origin;
} scws2_token_t;

/* NULL on allocation failure; starts with built-in rules and no dictionary. */
scws2_t scws2_new(void);
/* Frees the context and its dictionaries, not returned results; NULL is accepted. */
void scws2_free(scws2_t ctx);
/* Replace all dictionaries, or append an overlay. The context owns all loaded
 * data. Failure preserves existing dictionaries. Flags: 0 requests a read-only
 * mapping of the XDB; MEMORY forces fread; TEXT compiles/caches UTF-8 rows.
 * TEXT | MEMORY combines both. If mapping is unavailable, use memory instead. */
scws2_status_t scws2_set_dict(scws2_t ctx, const char *path, scws2_dict_flags_t flags);
scws2_status_t scws2_add_dict(scws2_t ctx, const char *path, scws2_dict_flags_t flags);
/* Replaces all flags; defaults to 0. Does not change scoring parameters. */
scws2_status_t scws2_set_options(scws2_t ctx, unsigned flags);
/* Advanced tuning only: cost must be nonzero; normally keep the default. */
scws2_status_t scws2_set_unknown_cost(scws2_t ctx, uint32_t cost);
/* Returns an independently owned result (including for empty input), or NULL
 * on error. Input is length-delimited UTF-8, may contain NUL separators, and is
 * not retained. Prior results survive later calls, errors and context free.
 * A context must not be used concurrently. */
scws2_result_t scws2_segment(scws2_t ctx, const char *text, size_t length, scws2_mode_t mode);
/* Token pointers live until result_free. NULL means end, not an error. */
const scws2_token_t *scws2_result_next(scws2_result_t result);
/* Total count, not remaining count; NULL result returns 0. */
size_t scws2_result_count(scws2_result_t result);
void scws2_result_free(scws2_result_t result);
/* Context operations replace the last error; success clears it. Queries and
 * result operations do not change it. NULL context reports SCWS2_EINVAL. */
scws2_status_t scws2_error_code(scws2_t ctx);
const char *scws2_strerror(scws2_t ctx);

#ifdef __cplusplus
}
#endif
#endif
