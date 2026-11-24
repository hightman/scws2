#ifndef SCWS_RULES_H
#define SCWS_RULES_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
	size_t offset, atom_end;
	uint32_t ch;
	char atom_pos[3];
	size_t number_from, number_to, quantity_to, numeric_owner;
} scws_unit;
int scws_rule_symbol(uint32_t ch);
void scws_rule_numbers(scws_unit *units, size_t count);
#endif
