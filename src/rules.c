#include "rules.h"
#include "rules_data.h"
#include "utf8.h"

static int member(const uint32_t *set, uint32_t ch)
{
	for (; *set; set++) if (*set == ch) return 1;
	return 0;
}
int scws_rule_symbol(uint32_t ch) { return member(rule_symbol, ch); }
static int numeric(uint32_t ch)
{
	return member(rule_chnum0, ch) || member(rule_chnum1, ch)
		|| member(rule_chnum2, ch) || member(rule_chnum4, ch);
}
static int digit(uint32_t ch)
{
	if (!numeric(ch)) return -1;
	if (scws_digit(ch)) return (int) (ch - '0');
	switch (ch) {
		case 0x96f6: case 0x3007: return 0; /* 零〇 */
		case 0x4e00: case 0x58f9: return 1; /* 一壹 */
		case 0x4e8c: case 0x8d30: case 0x8cb3: case 0x4e24: case 0x5169: return 2;
		case 0x4e09: case 0x53c1: case 0x53c3: return 3;
		case 0x56db: case 0x8086: return 4;
		case 0x4e94: case 0x4f0d: return 5;
		case 0x516d: case 0x9646: case 0x9678: return 6;
		case 0x4e03: case 0x67d2: return 7;
		case 0x516b: case 0x634c: return 8;
		case 0x4e5d: case 0x7396: return 9;
		default: return -1;
	}
}
/* Rank exponents suffice for grammar validation; no numeric conversion. */
static int rank(uint32_t ch)
{
	if (!numeric(ch)) return 0;
	switch (ch) {
		case 0x5341: case 0x62fe: return 1; /* 十拾 */
		case 0x767e: case 0x4f70: return 2;
		case 0x5343: case 0x4edf: return 3;
		case 0x4e07: case 0x842c: return 4;
		case 0x4ebf: case 0x5104: return 8;
		case 0x5146: return 12;
		default: return 0;
	}
}
static size_t measure(const scws_unit *units, size_t count, size_t from)
{
	size_t best = from;
	for (size_t i = 0; rule_units[i][0]; i++) {
		size_t j = 0;
		while (rule_units[i][j] && from + j < count && rule_units[i][j] == units[from + j].ch) j++;
		if (!rule_units[i][j] && from + j > best) best = from + j;
	}
	return best;
}
static int integer(const scws_unit *units, size_t from, size_t to)
{
	int all_digits = 1, arabic = scws_digit(units[from].ch);
	int pending = 0, zero = 0, section = 0, any = 0, small = 4, big = 13;
	if (arabic) {
		size_t i = from;
		while (i < to && scws_digit(units[i].ch)) i++;
		return i == to || (i + 1 == to && rank(units[i].ch) >= 4);
	}
	for (size_t i = from; i < to; i++) {
		if (scws_digit(units[i].ch)) return 0;
		if (digit(units[i].ch) < 0) all_digits = 0;
	}
	if (all_digits) {
		for (size_t i = from; i < to; i++)
			if (to - from > 1 && (units[i].ch == 0x4e24 || units[i].ch == 0x5169)) return 0; /* 一两/两三 are approximate. */
		return 1;
	}
	for (size_t i = from; i < to; i++) {
		int value = digit(units[i].ch), power = rank(units[i].ch);
		if (value >= 0) {
			if (!value) {
				if (!any || pending || zero) return 0;
				zero = 1;
			} else {
				if (pending) return 0;
				pending = 1; zero = 0;
			}
		} else if (power && power < 4) {
			if (power >= small || zero || (!pending && !(power == 1 && !section))) return 0;
			small = power; pending = 0; section = any = 1;
		} else if (power >= 4) {
			if (power >= big || zero || (!section && !pending)) return 0;
			big = power; small = 4; pending = section = 0; any = 1;
		} else return 0;
	}
	return !zero && (any || pending);
}
void scws_rule_numbers(scws_unit *units, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		size_t from = i, end, integer_end, quantity;
		int prefix = member(rule_chnum5, units[i].ch), decimal = 0;
		if (i && (numeric(units[i - 1].ch) || member(rule_chnum5, units[i - 1].ch)
			|| scws_alpha(units[i - 1].ch))) continue;
		if (i > 1 && member(rule_chnum3, units[i - 1].ch) && numeric(units[i - 2].ch)) continue;
		if (prefix) from++;
		if (from == count || !numeric(units[from].ch)) continue;
		end = from;
		while (end < count && numeric(units[end].ch)) {
			/* Prefer 千克/千米 as units over consuming their 千 as a rank. */
			if (end > from && measure(units, count, end) > end) break;
			end++;
		}
		integer_end = end;
		if (end < count && member(rule_chnum3, units[end].ch) && end + 1 < count && digit(units[end + 1].ch) >= 0) {
			int arabic = scws_digit(units[from].ch);
			if (arabic && rank(units[integer_end - 1].ch)) continue; /* 3万.5 is not a decimal. */
			size_t start = ++end;
			while (end < count && digit(units[end].ch) >= 0 && !!scws_digit(units[end].ch) == arabic) end++;
			if (end == start) continue;
			decimal = 1;
			if (end < count && rank(units[end].ch) >= 4) end++;
		}
		if (!integer(units, from, integer_end)) continue;
		quantity = measure(units, count, end);
		/* Reject malformed runs rather than recognizing a valid-looking prefix. */
		if (end < count && ((numeric(units[end].ch) && quantity == end) || scws_alpha(units[end].ch))) continue;
		if (end < count && member(rule_chnum3, units[end].ch) && end + 1 < count && numeric(units[end + 1].ch)) continue;
		if (prefix && units[i].ch == 0x6bcf && quantity == end) continue; /* 每 requires a unit. */
		if (!prefix && !decimal && end == from + 1 && !scws_digit(units[from].ch) && quantity == end) continue;
		units[i].number_from = from;
		units[i].number_to = end;
		units[i].quantity_to = quantity;
	}
}
