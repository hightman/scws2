#ifndef SCWS2_XDB_H
#define SCWS2_XDB_H
#include "scws2.h"
typedef struct scws2_dict scws2_dict_t;
scws2_status_t scws2_dict_open_ex(const char *path, scws2_dict_flags_t mode, scws2_dict_t **out);
scws2_status_t scws2_text_open(const char *path, scws2_dict_flags_t mode, scws2_dict_t **out);
scws2_status_t scws2_dict_from_data(unsigned char *data, uint32_t size, scws2_dict_t **out);
void scws2_dict_close(scws2_dict_t *dict);
scws2_status_t scws2_text_load(const char *path, unsigned char **data, uint32_t *size);
enum { SCWS2_WORD_MAIN = 1u, SCWS2_WORD_SEARCH = 2u, SCWS2_WORD_PREFIX = 4u };
typedef struct { uint32_t cost, flags; char pos[3]; } scws2_word;
int scws2_dict_lookup(const scws2_dict_t *dict, const unsigned char *key, size_t length, scws2_word *word);
#endif
