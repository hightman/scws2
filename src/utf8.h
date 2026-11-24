#ifndef SCWS_UTF8_H
#define SCWS_UTF8_H
#include <stddef.h>
#include <stdint.h>
size_t scws_utf8_decode(const unsigned char *text, size_t length, uint32_t *scalar);
uint32_t scws_ascii_width(uint32_t ch);
int scws_space(uint32_t ch);
int scws_symbol(uint32_t ch);
int scws_han(uint32_t ch);
int scws_alpha(uint32_t ch);
int scws_digit(uint32_t ch);
#endif
