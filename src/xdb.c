#include "xdb.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 64u
#define MAX_DICTIONARY_BYTES (512u * 1024u * 1024u)
struct scws_dict { unsigned char *data; uint32_t size, buckets, base; };
typedef struct { uint32_t offset, length, bucket; } record;

static uint32_t u32(const unsigned char *p)
{
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint32_t hash(const scws_dict *dict, const unsigned char *key, size_t length)
{
	uint32_t h = dict->base;
	while (length--) h = ((h * 33u) ^ key[length]) & 0x7fffffffu;
	return h % dict->buckets;
}
static int key_compare(const unsigned char *a, size_t an, const unsigned char *b, size_t bn)
{
	int order = memcmp(a, b, an < bn ? an : bn);
	return order ? order : an < bn ? -1 : an > bn;
}
static uint32_t find(const scws_dict *dict, const unsigned char *key, size_t length)
{
	uint32_t offset = u32(dict->data + HEADER + hash(dict, key, length) * 8u);
	while (offset) {
		const unsigned char *node = dict->data + offset;
		int order = key_compare(key, length, node + 17, node[16]);
		if (!order) return offset;
		offset = u32(node + (order > 0 ? 8 : 0));
	}
	return 0;
}
int scws_dict_lookup(const scws_dict *dict, const unsigned char *key, size_t length, scws_word *word)
{
	uint32_t offset;
	const unsigned char *value;
	if (!dict || !key || !length || length > 240) return 0;
	offset = find(dict, key, length);
	if (!offset) return 0;
	value = dict->data + offset + 17 + length;
	word->cost = u32(value);
	word->flags = u32(value + 4);
	memcpy(word->pos, value + 8, 2);
	word->pos[2] = '\0';
	return 1;
}
static int pos_valid(const unsigned char *p)
{
	if (!p[0]) return !p[1];
	if (!scws_alpha(p[0])) return 0;
	return !p[1] || scws_alpha(p[1]) || scws_digit(p[1]) || p[1] == '_' || p[1] == '-';
}
static int key_valid(const unsigned char *key, size_t length)
{
	for (size_t i = 0, size; i < length; i += size) {
		uint32_t ch;
		size = scws_utf8_decode(key + i, length - i, &ch);
		if (!size || scws_space(ch)) return 0;
	}
	return 1;
}
static int pointer_matches(const unsigned char *p, const record *records, size_t lo, size_t hi)
{
	if (lo == hi) return !u32(p) && !u32(p + 4);
	lo += (hi - lo) / 2;
	return u32(p) == records[lo].offset && u32(p + 4) == records[lo].length;
}
static int tree_valid(const scws_dict *dict, const record *records, size_t lo, size_t hi)
{
	size_t mid;
	const unsigned char *node;
	if (lo == hi) return 1;
	mid = lo + (hi - lo) / 2;
	node = dict->data + records[mid].offset;
	return pointer_matches(node, records, lo, mid)
		&& pointer_matches(node + 8, records, mid + 1, hi)
		&& tree_valid(dict, records, lo, mid) && tree_valid(dict, records, mid + 1, hi);
}
static size_t record_index(const record *records, size_t count, uint32_t offset)
{
	size_t lo = 0, hi = count;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (records[mid].offset < offset) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}
static scws_status validate(scws_dict *dict)
{
	const unsigned char *data = dict->data;
	uint32_t count, offset;
	record *records = NULL;
	unsigned char *prefixes = NULL;
	scws_status status = SCWS_EFORMAT;
	size_t position = 0;
	if (dict->size < HEADER || memcmp(data, "XDB2", 4) || u32(data + 4) != 3
		|| u32(data + 8) != HEADER || u32(data + 12) || u32(data + 16) != 0xf422f
		|| u32(data + 28) != dict->size || u32(data + 32) != 10) return status;
	for (size_t i = 36; i < HEADER; i++) if (data[i]) return status;
	dict->base = u32(data + 16);
	dict->buckets = u32(data + 20);
	count = u32(data + 24);
	if (!dict->buckets || dict->buckets > 1000000 || (uint64_t) HEADER + dict->buckets * 8u > dict->size)
		return status;
	offset = HEADER + dict->buckets * 8u;
	/* The 512 MiB file cap and minimum record size also bound this allocation. */
	if (!count || count > (dict->size - offset) / 28u) return status;
	records = malloc((size_t) count * sizeof(*records));
	prefixes = calloc(count, 1);
	if (!records || !prefixes) { status = SCWS_ENOMEM; goto done; }
	for (size_t i = 0; i < count; i++) {
		const unsigned char *node, *value;
		uint32_t length, flags;
		if (offset > dict->size || dict->size - offset < 28) goto done;
		node = data + offset;
		length = 27u + node[16];
		if (!node[16] || node[16] > 240 || length > dict->size - offset || !key_valid(node + 17, node[16])) goto done;
		value = node + 17 + node[16];
		flags = u32(value + 4);
		if (!flags || flags & ~7u || ((flags & 1u) && !(flags & 2u))
			|| (!(flags & 1u) && u32(value)) || !pos_valid(value + 8)
			|| (!(flags & 2u) && (value[8] || value[9]))) goto done;
		records[i] = (record) {offset, length, hash(dict, node + 17, node[16])};
		if (i) {
			const unsigned char *previous = data + records[i - 1].offset;
			if (records[i - 1].bucket > records[i].bucket || (records[i - 1].bucket == records[i].bucket
				&& key_compare(previous + 17, previous[16], node + 17, node[16]) >= 0)) goto done;
		}
		offset += length;
	}
	if (offset != dict->size) goto done;
	for (uint32_t bucket = 0; bucket < dict->buckets; bucket++) {
		size_t begin = position;
		while (position < count && records[position].bucket == bucket) position++;
		if (!pointer_matches(data + HEADER + bucket * 8u, records, begin, position)
			|| !tree_valid(dict, records, begin, position)) goto done;
	}
	/* All links are now known safe; validate the exact proper-prefix closure. */
	for (size_t i = 0; i < count; i++) {
		const unsigned char *node = data + records[i].offset, *key = node + 17;
		size_t length = node[16];
		if (!(u32(key + length + 4) & SCWS_WORD_SEARCH)) continue;
		for (size_t j = 0, size; j < length; j += size) {
			uint32_t ch, found;
			size_t index;
			size = scws_utf8_decode(key + j, length - j, &ch);
			if (j + size == length) break;
			found = find(dict, key, j + size);
			if (!found) goto done;
			index = record_index(records, count, found);
			if (index >= count || !(u32(data + found + 17 + j + size + 4) & SCWS_WORD_PREFIX)) goto done;
			prefixes[index] = 1;
		}
	}
	for (size_t i = 0; i < count; i++) {
		const unsigned char *node = data + records[i].offset;
		if (!!(u32(node + 17 + node[16] + 4) & SCWS_WORD_PREFIX) != prefixes[i]) goto done;
	}
	status = SCWS_OK;
done:
	free(prefixes);
	free(records);
	return status;
}

scws_status scws_dict_open(const char *path, scws_dict **out)
{
	FILE *fp;
	long length;
	scws_dict *dict;
	scws_status status;
	if (!out) return SCWS_EINVAL;
	*out = NULL;
	if (!path) return SCWS_EINVAL;
	fp = fopen(path, "rb");
	if (!fp) return SCWS_EIO;
	if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET)) {
		fclose(fp); return SCWS_EIO;
	}
	if (length < (long) HEADER || (unsigned long) length > MAX_DICTIONARY_BYTES) {
		fclose(fp); return length < (long) HEADER ? SCWS_EFORMAT : SCWS_ELIMIT;
	}
	dict = calloc(1, sizeof(*dict));
	if (!dict) { fclose(fp); return SCWS_ENOMEM; }
	dict->size = (uint32_t) length;
	dict->data = malloc(dict->size);
	if (!dict->data) { fclose(fp); free(dict); return SCWS_ENOMEM; }
	status = fread(dict->data, 1, dict->size, fp) == dict->size ? SCWS_OK : SCWS_EIO;
	if (fclose(fp)) status = SCWS_EIO;
	if (!status) status = validate(dict);
	if (status) { scws_dict_close(dict); return status; }
	*out = dict;
	return SCWS_OK;
}
void scws_dict_close(scws_dict *dict)
{
	if (dict) { free(dict->data); free(dict); }
}
