/* SCWS 2 public API. See COPYING for the BSD license. */
#ifndef SCWS_H
#define SCWS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SCWS_VERSION "2.0.0-dev"
typedef struct scws *scws_t;
typedef struct scws_result *scws_result_t;
typedef enum {
	SCWS_OK = 0, SCWS_EINVAL, SCWS_ENOMEM, SCWS_EIO, SCWS_EFORMAT,
	SCWS_EUTF8, SCWS_ELIMIT, SCWS_ENOTSUP
} scws_status_t;
typedef enum { SCWS_DICT_MEMORY = 0, SCWS_DICT_MMAP = 1, SCWS_DICT_TEXT = 2 } scws_dict_mode_t;
typedef enum { SCWS_STANDARD = 0, SCWS_SEARCH = 1 } scws_mode_t;
typedef enum { SCWS_PRIMARY = 0, SCWS_SECONDARY = 1, SCWS_FALLBACK = 2 } scws_kind_t;
typedef enum { SCWS_DICTIONARY = 0, SCWS_BUILTIN = 1, SCWS_UNKNOWN = 2 } scws_origin_t;
enum { SCWS_KEEP_SYMBOLS = 1u, SCWS_FALLBACK_BIGRAM = 2u };

typedef struct {
	size_t offset, length;             /* Original UTF-8 byte span. */
	size_t char_offset, char_length;   /* Unicode scalar span, not engine positions. */
	char pos[3];                      /* At most two ASCII characters plus NUL. */
	scws_kind_t kind;
	scws_origin_t origin;
} scws_token_t;

/* NULL on allocation failure; starts with built-in rules and no dictionary. */
scws_t scws_new(void);
/* Frees the context and its dictionaries, not returned results; NULL is accepted. */
void scws_free(scws_t ctx);
/* Replace all dictionaries, or append an overlay. The context owns all loaded
 * data. Failure preserves the existing dictionaries. MEMORY is the default
 * mode (0); MMAP is read-only and never silently falls back to MEMORY.
 * TEXT reads UTF-8 term/frequency/POS rows into an owned memory dictionary. */
scws_status_t scws_set_dict(scws_t ctx, const char *path, scws_dict_mode_t mode);
scws_status_t scws_add_dict(scws_t ctx, const char *path, scws_dict_mode_t mode);
/* Replaces all flags; defaults to 0. Does not change scoring parameters. */
scws_status_t scws_set_options(scws_t ctx, unsigned flags);
/* Advanced tuning only: cost must be nonzero; normally keep the default. */
scws_status_t scws_set_unknown_cost(scws_t ctx, uint32_t cost);
/* Returns an independently owned result (including for empty input), or NULL
 * on error. Input is length-delimited UTF-8, may contain NUL separators, and is
 * not retained. Prior results survive later calls, errors and context free.
 * A context must not be used concurrently. */
scws_result_t scws_segment(scws_t ctx, const char *text, size_t length, scws_mode_t mode);
/* Token pointers live until result_free. NULL means end, not an error. */
const scws_token_t *scws_result_next(scws_result_t result);
/* Total count, not remaining count; NULL result returns 0. */
size_t scws_result_count(scws_result_t result);
void scws_result_free(scws_result_t result);
/* Context operations replace the last error; success clears it. Queries and
 * result operations do not change it. NULL context reports SCWS_EINVAL. */
scws_status_t scws_error_code(scws_t ctx);
const char *scws_strerror(scws_t ctx);

#ifdef __cplusplus
}
#endif
#endif
