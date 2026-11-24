#ifndef SCWS_XDB_H
#define SCWS_XDB_H
#include "scws.h"
enum { SCWS_WORD_MAIN = 1u, SCWS_WORD_SEARCH = 2u, SCWS_WORD_PREFIX = 4u };
typedef struct { uint32_t cost, flags; char pos[3]; } scws_word;
int scws_dict_lookup(const scws_dict *dict, const unsigned char *key, size_t length, scws_word *word);
#endif
