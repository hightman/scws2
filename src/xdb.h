#ifndef SCWS_XDB_H
#define SCWS_XDB_H
#include "scws.h"
typedef struct scws_dict scws_dict_t;
scws_status_t scws_dict_open_ex(const char *path, scws_dict_mode_t mode, scws_dict_t **out);
void scws_dict_close(scws_dict_t *dict);
scws_status_t scws_text_load(const char *path, unsigned char **data, uint32_t *size);
enum { SCWS_WORD_MAIN = 1u, SCWS_WORD_SEARCH = 2u, SCWS_WORD_PREFIX = 4u };
typedef struct { uint32_t cost, flags; char pos[3]; } scws_word;
int scws_dict_lookup(const scws_dict_t *dict, const unsigned char *key, size_t length, scws_word *word);
#endif
