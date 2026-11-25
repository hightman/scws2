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
typedef enum {
	SCWS_OK = 0, SCWS_EINVAL, SCWS_ENOMEM, SCWS_EIO, SCWS_EFORMAT,
	SCWS_EUTF8, SCWS_ELIMIT, SCWS_ENOTSUP
} scws_status_t;
typedef enum { SCWS_DICT_MEMORY = 0, SCWS_DICT_MMAP = 1 } scws_dict_mode_t;
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
/* Frees the context, its dictionaries and results; NULL is accepted. */
void scws_free(scws_t ctx);
/* Replace all dictionaries, or append an overlay. The context owns all loaded
 * data. Failure preserves the existing dictionaries. MEMORY is the default
 * mode (0); MMAP is read-only and never silently falls back to MEMORY. */
scws_status_t scws_set_dict(scws_t ctx, const char *path, scws_dict_mode_t mode);
scws_status_t scws_add_dict(scws_t ctx, const char *path, scws_dict_mode_t mode);
/* Replaces all flags; defaults to 0. Does not change scoring parameters. */
scws_status_t scws_set_options(scws_t ctx, unsigned flags);
/* Advanced tuning only: cost must be nonzero; normally keep the default. */
scws_status_t scws_set_unknown_cost(scws_t ctx, uint32_t cost);
/* Input is length-delimited; embedded NULs are separators. Invalid UTF-8
 * fails the entire call, clears previous results and never returns partial text.
 * The input is not retained. A context must not be used concurrently. */
scws_status_t scws_segment(scws_t ctx, const char *text, size_t length, scws_mode_t mode);
/* Result storage belongs to the context until the next segment call or free. */
const scws_token_t *scws_next(scws_t ctx);
size_t scws_count(scws_t ctx);
const char *scws_strerror(scws_status_t status);

#ifdef __cplusplus
}
#endif
#endif
