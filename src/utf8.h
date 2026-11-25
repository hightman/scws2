#ifndef SCWS2_UTF8_H
#define SCWS2_UTF8_H
#include <stddef.h>
#include <stdint.h>
size_t scws2_utf8_decode(const unsigned char *text, size_t length, uint32_t *scalar);
uint32_t scws2_ascii_width(uint32_t ch);
int scws2_space(uint32_t ch);
int scws2_symbol(uint32_t ch);
int scws2_han(uint32_t ch);
int scws2_alpha(uint32_t ch);
int scws2_digit(uint32_t ch);
#endif
