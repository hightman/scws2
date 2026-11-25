/* UTF-8 text dictionaries are compiled directly into an in-memory XDB2. */
#include "xdb.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BYTES (512u * 1024u * 1024u)
#define HEADER 64u
#define TRAILER 8u
#define MAX_KEY 240u
#define BASE 0xf422fu
#define REFERENCE_COST 18432u

typedef struct {
	char *key;
	uint32_t cost, flags, bucket, offset, sequence;
	char pos[3];
	unsigned char length;
} entry;
typedef struct { entry *entries; size_t count, capacity; } builder;

static uint32_t hash_key(const unsigned char *key, size_t length, uint32_t buckets)
{
	uint32_t h = BASE;
	while (length--) h = ((h * 33u) ^ key[length]) & 0x7fffffffu;
	return h % buckets;
}
/* ln(frequency) in the same 1/1024 scale as XDB costs, without libm.
 * Normalize to [1,2), then use ln(m)=2*(z+z^3/3+...), |z|<=1/3. */
static uint32_t frequency_cost(uint32_t frequency)
{
	unsigned exponent = 0;
	uint32_t reduced;
	double m, z, square, power, sum = 0, reduction;
	if (!frequency) frequency = 1;
	for (reduced = frequency; reduced > 1; reduced >>= 1) exponent++;
	m = (double) frequency / (double) ((uint32_t) 1 << exponent);
	z = (m - 1) / (m + 1);
	square = z * z; power = z;
	for (unsigned denominator = 1; denominator <= 15; denominator += 2) {
		sum += power / denominator;
		power *= square;
	}
	reduction = (exponent * 0.69314718055994530942 + 2 * sum) * 1024;
	return reduction >= REFERENCE_COST ? 0 : (uint32_t) (REFERENCE_COST - reduction + 0.5);
}
static scws_status_t add(builder *b, const char *key, size_t length, const scws_word *word)
{
	entry *item;
	if (b->count >= MAX_BYTES / 28u) return SCWS_ELIMIT;
	if (b->count == b->capacity) {
		size_t next = b->capacity ? b->capacity * 2 : 256;
		entry *grown;
		if (next < b->capacity || next > SIZE_MAX / sizeof(*grown)) return SCWS_ELIMIT;
		grown = realloc(b->entries, next * sizeof(*grown));
		if (!grown) return SCWS_ENOMEM;
		b->entries = grown; b->capacity = next;
	}
	item = &b->entries[b->count];
	memset(item, 0, sizeof(*item));
	item->key = malloc(length + 1);
	if (!item->key) return SCWS_ENOMEM;
	memcpy(item->key, key, length); item->key[length] = '\0';
	item->length = (unsigned char) length;
	item->cost = word->cost; item->flags = word->flags; memcpy(item->pos, word->pos, 3);
	item->sequence = (uint32_t) b->count++;
	return SCWS_OK;
}
static int parse_frequency(const char *s, uint32_t *out)
{
	uint32_t value = 0;
	if (!*s) return 0;
	for (; *s; s++) {
		unsigned digit = (unsigned char) *s - '0';
		if (digit > 9 || value > (UINT32_MAX - digit) / 10u) return 0;
		value = value * 10u + digit;
	}
	*out = value;
	return 1;
}
static int parse_pos(const char *s, char pos[3])
{
	size_t length;
	if (!strcmp(s, "nrt") || !strcmp(s, "nrfg")) s = "nr";
	length = strlen(s);
	if (!length || length > 2 || !scws_alpha((unsigned char) s[0])) return 0;
	if (length == 2 && !scws_alpha((unsigned char) s[1]) && !scws_digit((unsigned char) s[1])
		&& s[1] != '_' && s[1] != '-') return 0;
	memset(pos, 0, 3); memcpy(pos, s, length);
	return 1;
}
static scws_status_t parse_line(builder *b, char *line, size_t length, int first)
{
	char *fields[3], *s = line;
	size_t count = 0, ends[MAX_KEY], characters = 0;
	uint32_t frequency = 1;
	scws_word word = {0, SCWS_WORD_MAIN | SCWS_WORD_SEARCH, "un"};
	scws_word prefix = {0, SCWS_WORD_PREFIX, ""};
	scws_status_t status;
	if (length && line[length - 1] == '\r') length--;
	line[length] = '\0';
	if (first && length >= 3 && !memcmp(line, "\xef\xbb\xbf", 3)) s += 3;
	while (*s == ' ' || *s == '\t') s++;
	if (!*s || *s == '#' || *s == ';') return SCWS_OK;
	while (*s) {
		if (count == 3) return SCWS_EFORMAT;
		fields[count++] = s;
		while (*s && *s != ' ' && *s != '\t') s++;
		if (*s) *s++ = '\0';
		while (*s == ' ' || *s == '\t') s++;
	}
	if (count > 1) {
		if (fields[1][0] >= '0' && fields[1][0] <= '9') {
			if (!parse_frequency(fields[1], &frequency)) return SCWS_EFORMAT;
			if (count == 3 && !parse_pos(fields[2], word.pos)) return SCWS_EFORMAT;
		} else {
			if (!parse_pos(fields[1], word.pos)) return SCWS_EFORMAT;
			if (count == 3 && !parse_frequency(fields[2], &frequency)) return SCWS_EFORMAT;
		}
	}
	length = strlen(fields[0]);
	if (!length || length > MAX_KEY) return SCWS_EFORMAT;
	for (size_t i = 0, width; i < length; i += width) {
		uint32_t ch;
		width = scws_utf8_decode((const unsigned char *) fields[0] + i, length - i, &ch);
		if (!width || scws_space(ch) || (ch >= 0x7f && ch < 0xa0)) return SCWS_EFORMAT;
		ends[characters++] = i + width;
	}
	word.cost = frequency_cost(frequency);
	status = add(b, fields[0], length, &word);
	if (status) return status;
	for (size_t i = 0; i + 1 < characters; i++) {
		status = add(b, fields[0], ends[i], &prefix);
		if (status) return status;
	}
	return SCWS_OK;
}
static int compare(const void *left, const void *right)
{
	const entry *a = left, *b = right;
	int order;
	if (a->bucket != b->bucket) return a->bucket < b->bucket ? -1 : 1;
	order = strcmp(a->key, b->key);
	return order ? order : a->sequence < b->sequence ? -1 : a->sequence > b->sequence;
}
static void put32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char) value; p[1] = (unsigned char) (value >> 8);
	p[2] = (unsigned char) (value >> 16); p[3] = (unsigned char) (value >> 24);
}
static void pointer(unsigned char *p, const entry *entries, size_t lo, size_t hi)
{
	if (lo != hi) {
		const entry *e = &entries[lo + (hi - lo) / 2];
		put32(p, e->offset); put32(p + 4, 27u + e->length);
	}
}
static void tree(unsigned char *data, const entry *entries, size_t lo, size_t hi)
{
	size_t mid;
	unsigned char *node;
	if (lo == hi) return;
	mid = lo + (hi - lo) / 2; node = data + entries[mid].offset;
	pointer(node, entries, lo, mid); pointer(node + 8, entries, mid + 1, hi);
	tree(data, entries, lo, mid); tree(data, entries, mid + 1, hi);
}
static scws_status_t serialize(builder *b, unsigned char **out, uint32_t *size)
{
	uint32_t buckets = b->count <= 128 ? 17 : b->count <= 2048 ? 257 : b->count <= 32768 ? 4093 : 16381;
	uint64_t bytes = HEADER + buckets * 8u;
	size_t used = 0, position = 0;
	unsigned char *data;
	for (size_t i = 0; i < b->count; i++)
		b->entries[i].bucket = hash_key((const unsigned char *) b->entries[i].key, b->entries[i].length, buckets);
	qsort(b->entries, b->count, sizeof(*b->entries), compare);
	for (size_t i = 0; i < b->count; i++) {
		entry *current = &b->entries[i];
		if (used && !strcmp(b->entries[used - 1].key, current->key)) {
			entry *previous = &b->entries[used - 1];
			uint32_t flags = previous->flags | current->flags;
			if (current->flags & SCWS_WORD_SEARCH) {
				previous->cost = current->cost; memcpy(previous->pos, current->pos, 3);
			}
			previous->flags = flags;
			free(current->key);
		} else b->entries[used++] = *current;
	}
	b->count = used;
	for (size_t i = 0; i < used; i++) {
		b->entries[i].offset = (uint32_t) bytes;
		bytes += 27u + b->entries[i].length;
		if (bytes + TRAILER > MAX_BYTES) return SCWS_ELIMIT;
	}
	bytes += TRAILER;
	data = calloc(1, (size_t) bytes);
	if (!data) return SCWS_ENOMEM;
	memcpy(data, "XDB2", 4); put32(data + 4, 4); put32(data + 8, HEADER);
	put32(data + 16, BASE); put32(data + 20, buckets); put32(data + 24, (uint32_t) used);
	put32(data + 28, (uint32_t) bytes); put32(data + 32, 10);
	for (size_t i = 0; i < used; i++) {
		const entry *e = &b->entries[i];
		unsigned char *node = data + e->offset, *value = node + 17 + e->length;
		node[16] = e->length; memcpy(node + 17, e->key, e->length);
		put32(value, e->cost); put32(value + 4, e->flags); memcpy(value + 8, e->pos, 2);
	}
	for (uint32_t bucket = 0; bucket < buckets; bucket++) {
		size_t begin = position;
		while (position < used && b->entries[position].bucket == bucket) position++;
		pointer(data + HEADER + bucket * 8u, b->entries, begin, position);
		tree(data, b->entries, begin, position);
	}
	memcpy(data + bytes - TRAILER, "XDB2END\n", TRAILER);
	*out = data; *size = (uint32_t) bytes;
	return SCWS_OK;
}
scws_status_t scws_text_load(const char *path, unsigned char **data, uint32_t *size)
{
	FILE *input;
	builder b = {NULL, 0, 0};
	char line[4096];
	size_t length = 0, total = 0;
	int ch, first = 1;
	scws_status_t status = SCWS_OK;
	if (!path || !data || !size) return SCWS_EINVAL;
	*data = NULL; *size = 0;
	input = fopen(path, "rb");
	if (!input) return SCWS_EIO;
	while ((ch = fgetc(input)) != EOF) {
		if (++total > MAX_BYTES) { status = SCWS_ELIMIT; break; }
		if (!ch) { status = SCWS_EFORMAT; break; }
		if (ch == '\n') {
			status = parse_line(&b, line, length, first);
			if (status) break;
			length = 0; first = 0;
		} else {
			if (length == sizeof(line) - 1) { status = SCWS_ELIMIT; break; }
			line[length++] = (char) ch;
		}
	}
	if (!status && ferror(input)) status = SCWS_EIO;
	if (!status && length) status = parse_line(&b, line, length, first);
	if (fclose(input) && !status) status = SCWS_EIO;
	if (!status) status = b.count ? serialize(&b, data, size) : SCWS_EFORMAT;
	for (size_t i = 0; i < b.count; i++) free(b.entries[i].key);
	free(b.entries);
	return status;
}
