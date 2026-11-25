/* SCWS 2 public API. See COPYING for the BSD license. */
#ifndef SCWS_H
#define SCWS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SCWS_VERSION "2.0.0-dev"
typedef struct scws_dict scws_dict;
typedef struct scws scws;
typedef enum {
	SCWS_OK = 0, SCWS_EINVAL, SCWS_ENOMEM, SCWS_EIO, SCWS_EFORMAT,
	SCWS_EUTF8, SCWS_ELIMIT, SCWS_ENOTSUP
} scws_status;
typedef enum { SCWS_DICT_MEMORY = 0, SCWS_DICT_MMAP = 1 } scws_dict_mode;
typedef enum { SCWS_STANDARD = 0, SCWS_SEARCH = 1 } scws_mode;
typedef enum { SCWS_PRIMARY = 0, SCWS_SECONDARY = 1, SCWS_FALLBACK = 2 } scws_kind;
typedef enum { SCWS_DICTIONARY = 0, SCWS_BUILTIN = 1, SCWS_UNKNOWN = 2 } scws_origin;
enum { SCWS_KEEP_SYMBOLS = 1u, SCWS_FALLBACK_BIGRAM = 2u };

typedef struct {
	size_t offset, length;             /* Original UTF-8 byte span. */
	size_t char_offset, char_length;   /* Unicode scalar span, not engine positions. */
	char pos[3];                      /* At most two ASCII characters plus NUL. */
	scws_kind kind;
	scws_origin origin;
} scws_token;

/* Dictionaries are immutable after opening and may be shared by threads.
 * Close them only after all contexts using them have been freed. */
/* Default is a private, fully read memory buffer. */
scws_status scws_dict_open(const char *path, scws_dict **out);
/* MMAP is read-only and never silently falls back to MEMORY. The mapped file
 * must remain unchanged until close; deploy updates by replacing the file. */
scws_status scws_dict_open_ex(const char *path, scws_dict_mode mode, scws_dict **out);
void scws_dict_close(scws_dict *dict);
scws_status scws_new(const scws_dict *dict, scws **out);
void scws_free(scws *ctx);
/* Last added dictionary wins for a whole term. Prefixes are merged. */
scws_status scws_add_dict(scws *ctx, const scws_dict *dict);
scws_status scws_set_options(scws *ctx, unsigned flags, uint32_t unknown_cost);
/* Input is length-delimited; embedded NULs are separators. Invalid UTF-8
 * fails the entire call, clears previous results and never returns partial text.
 * The input is not retained. A context must not be used concurrently. */
scws_status scws_segment(scws *ctx, const char *text, size_t length, scws_mode mode);
/* Result storage belongs to the context until the next segment call or free. */
const scws_token *scws_next(scws *ctx);
size_t scws_count(const scws *ctx);
const char *scws_strerror(scws_status status);

#ifdef __cplusplus
}
#endif
#endif
