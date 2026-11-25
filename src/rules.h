#ifndef SCWS2_RULES_H
#define SCWS2_RULES_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
	size_t offset, atom_end;
	uint32_t ch;
	char atom_pos[3];
	size_t number_from, number_to, quantity_to, numeric_owner;
} scws2_unit;
int scws2_rule_symbol(uint32_t ch);
void scws2_rule_numbers(scws2_unit *units, size_t count);
#endif
