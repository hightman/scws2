/* SCWS 2 dictionary compiler. Distributed under the repository BSD license.
 * No third-party dependencies. See dict/FORMAT.md for the byte-level format.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN 1u
#define SEARCH 2u
#define PREFIX 4u
#define HEADER_SIZE 64u
#define BUCKETS 16381u
#define BASE 0xf422fu
#define MAX_KEY 240u
#define MAX_POS 2u

typedef struct {
	char *key;
	char pos[MAX_POS + 1];
	uint32_t cost, flags, bucket, offset, length;
} entry;

static entry *entries;
static size_t count, capacity;
static FILE *output;

_Noreturn static void fail(const char *message)
{
	fprintf(stderr, "scws2-dict: %s\n", message);
	exit(EXIT_FAILURE);
}

/* Validate scalar UTF-8; return character boundary byte offsets. */
static size_t boundaries(const unsigned char *key, size_t length, size_t *ends)
{
	size_t i = 0, chars = 0;
	while (i < length) {
		uint32_t ch = key[i++], minimum;
		size_t extra;
		if (ch < 0x80) {
			if (ch <= 0x20 || ch == 0x7f) fail("control/space in term");
			ends[chars++] = i;
			continue;
		}
		if (ch >= 0xc2 && ch <= 0xdf) { extra = 1; minimum = 0x80; ch &= 0x1f; }
		else if (ch >= 0xe0 && ch <= 0xef) { extra = 2; minimum = 0x800; ch &= 0x0f; }
		else if (ch >= 0xf0 && ch <= 0xf4) { extra = 3; minimum = 0x10000; ch &= 0x07; }
		else fail("invalid UTF-8 leading byte");
		if (i + extra > length) fail("truncated UTF-8 term");
		while (extra--) {
			if ((key[i] & 0xc0) != 0x80) fail("invalid UTF-8 continuation");
			ch = (ch << 6) | (key[i++] & 0x3f);
		}
		if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff))
			fail("invalid Unicode scalar value");
		ends[chars++] = i;
	}
	return chars;
}

static uint32_t hash_key(const unsigned char *key, size_t length)
{
	uint32_t hash = BASE;
	while (length--) hash = ((hash * 33u) ^ key[length]) & 0x7fffffffu;
	return hash % BUCKETS;
}

static void add(const char *key, size_t length, uint32_t cost, uint32_t flags, const char *pos)
{
	entry *item;
	if (count == capacity) {
		size_t next = capacity ? capacity * 2 : 4096;
		entry *grown;
		if (next < capacity || next > SIZE_MAX / sizeof(entry)) fail("too many entries");
		grown = realloc(entries, next * sizeof(entry));
		if (!grown) fail("out of memory");
		entries = grown;
		capacity = next;
	}
	item = &entries[count++];
	memset(item, 0, sizeof(*item));
	item->key = malloc(length + 1);
	if (!item->key) fail("out of memory");
	memcpy(item->key, key, length);
	item->key[length] = '\0';
	item->cost = cost;
	item->flags = flags;
	strcpy(item->pos, pos);
	item->bucket = hash_key((const unsigned char *) key, length);
	item->length = (uint32_t) (27 + length);
}

static int letter(unsigned char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static void validate_pos(const char *pos)
{
	size_t length = strlen(pos);
	if (length > MAX_POS || (length && !letter((unsigned char) pos[0])))
		fail("POS tag must be empty or an ASCII identifier of at most 2 bytes");
	for (size_t i = 1; i < length; i++) {
		unsigned char ch = (unsigned char) pos[i];
		if (!letter(ch) && !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-')
			fail("invalid POS tag character");
	}
}

static uint32_t number(const char *text)
{
	char *end;
	unsigned long long value;
	if (*text < '0' || *text > '9') fail("expected unsigned decimal integer");
	errno = 0;
	value = strtoull(text, &end, 10);
	if (errno || *end || value > UINT32_MAX) fail("invalid uint32 value");
	return (uint32_t) value;
}

static void read_lexicon(const char *path)
{
	FILE *input = fopen(path, "rb");
	char line[1024];
	if (!input) fail("cannot open input lexicon");
	while (fgets(line, sizeof(line), input)) {
		size_t length = strlen(line), ends[MAX_KEY], chars;
		char *cost_text, *flags_text, *pos;
		uint32_t cost, flags;
		if (!length || line[length - 1] != '\n') fail("oversized, NUL-containing or unterminated line");
		line[--length] = '\0';
		if (length && line[length - 1] == '\r') line[--length] = '\0';
		cost_text = strchr(line, '\t');
		if (!cost_text) fail("expected term/cost/flags/pos TSV");
		*cost_text++ = '\0';
		flags_text = strchr(cost_text, '\t');
		if (!flags_text) fail("missing flags column");
		*flags_text++ = '\0';
		pos = strchr(flags_text, '\t');
		if (!pos) fail("missing POS column (empty is allowed)");
		*pos++ = '\0';
		validate_pos(pos);
		length = strlen(line);
		if (!length || length > MAX_KEY) fail("term must contain 1..240 UTF-8 bytes");
		cost = number(cost_text);
		flags = number(flags_text);
		if ((flags != SEARCH && flags != (MAIN | SEARCH)) || (flags == SEARCH && cost))
			fail("invalid input flags or search-only cost");
		chars = boundaries((const unsigned char *) line, length, ends);
		add(line, length, cost, flags, pos);
		for (size_t i = 0; i + 1 < chars; i++) add(line, ends[i], 0, PREFIX, "");
	}
	if (ferror(input)) fail("input read failed");
	if (fclose(input)) fail("input close failed");
	if (!count) fail("empty dictionary");
}

static int compare(const void *left, const void *right)
{
	const entry *a = left, *b = right;
	if (a->bucket != b->bucket) return a->bucket < b->bucket ? -1 : 1;
	return strcmp(a->key, b->key);
}

static void deduplicate(void)
{
	size_t used = 0;
	qsort(entries, count, sizeof(entry), compare);
	for (size_t i = 0; i < count; i++) {
		if (used && !strcmp(entries[used - 1].key, entries[i].key)) {
			entry *previous = &entries[used - 1];
			if ((previous->flags & SEARCH) && (entries[i].flags & SEARCH))
				fail("duplicate lexicon term");
			if (entries[i].flags & SEARCH) {
				previous->cost = entries[i].cost;
				strcpy(previous->pos, entries[i].pos);
				previous->length = entries[i].length;
			}
			previous->flags |= entries[i].flags;
			free(entries[i].key);
		} else {
			entries[used++] = entries[i];
		}
	}
	count = used;
}

static void bytes(const void *data, size_t size)
{
	if (fwrite(data, 1, size, output) != size) fail("output write failed");
}

static void u32(uint32_t value)
{
	unsigned char data[4] = {(unsigned char) value, (unsigned char) (value >> 8),
		(unsigned char) (value >> 16), (unsigned char) (value >> 24)};
	bytes(data, sizeof(data));
}

static void pointer(size_t begin, size_t end)
{
	if (begin == end) { u32(0); u32(0); }
	else {
		const entry *root = &entries[begin + (end - begin) / 2];
		u32(root->offset);
		u32(root->length);
	}
}

/* Records have assigned offsets, so preorder traversal can patch tree links. */
static void tree(size_t begin, size_t end)
{
	size_t middle;
	entry *item;
	unsigned char length;
	if (begin == end) return;
	middle = begin + (end - begin) / 2;
	item = &entries[middle];
	if (fseek(output, (long) item->offset, SEEK_SET)) fail("output seek failed");
	pointer(begin, middle);
	pointer(middle + 1, end);
	length = (unsigned char) strlen(item->key);
	bytes(&length, 1);
	bytes(item->key, length);
	u32(item->cost);
	u32(item->flags);
	bytes(item->pos, MAX_POS);
	tree(begin, middle);
	tree(middle + 1, end);
}

int main(int argc, char **argv)
{
	uint64_t file_size = HEADER_SIZE + BUCKETS * 8u;
	unsigned char reserved[28] = {0};
	size_t position = 0;
	if (argc != 3) {
		fprintf(stderr, "Usage: scws2-dict lexicon.tsv output.xdb\n");
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[1], argv[2])) fail("input and output must differ");
	read_lexicon(argv[1]);
	deduplicate();
	if (count > UINT32_MAX) fail("too many nodes");
	for (size_t i = 0; i < count; i++) {
		entries[i].offset = (uint32_t) file_size;
		file_size += entries[i].length;
		/* fseek uses long; cap the prototype at 2 GiB for portability. */
		if (file_size > INT32_MAX) fail("dictionary exceeds 2 GiB format limit");
	}
	output = fopen(argv[2], "wb");
	if (!output) fail("cannot open output");
	bytes("XDB2", 4);
	u32(3); u32(HEADER_SIZE); u32(0); u32(BASE); u32(BUCKETS);
	u32((uint32_t) count); u32((uint32_t) file_size); u32(10);
	bytes(reserved, sizeof(reserved));
	for (uint32_t bucket = 0; bucket < BUCKETS; bucket++) {
		size_t begin = position;
		while (position < count && entries[position].bucket == bucket) position++;
		pointer(begin, position);
	}
	position = 0;
	for (uint32_t bucket = 0; bucket < BUCKETS; bucket++) {
		size_t begin = position;
		while (position < count && entries[position].bucket == bucket) position++;
		tree(begin, position);
	}
	if (fclose(output)) fail("output close failed");
	for (size_t i = 0; i < count; i++) free(entries[i].key);
	free(entries);
	printf("XDB2: %zu nodes, %u bytes\n", count, (unsigned) file_size);
	return EXIT_SUCCESS;
}
