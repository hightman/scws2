#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "xdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(SCWS2_NO_MMAP) && (defined(__unix__) || defined(__APPLE__))
#define SCWS2_HAVE_MMAP 1
#include <sys/mman.h>
#endif

#define HEADER 64u
#define TRAILER 8u
#define MAX_DICTIONARY_BYTES (512u * 1024u * 1024u)
struct scws2_dict { unsigned char *data; uint32_t size, buckets, base; int mapped; };

static uint32_t u32(const unsigned char *p)
{
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint32_t hash(const scws2_dict_t *dict, const unsigned char *key, size_t length)
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
static uint32_t find(const scws2_dict_t *dict, const unsigned char *key, size_t length)
{
	uint32_t offset = u32(dict->data + HEADER + hash(dict, key, length) * 8u);
	/* Compiler trees are balanced; 64 steps also stop accidental link cycles. */
	uint32_t floor = HEADER + dict->buckets * 8u, end = dict->size - TRAILER;
	for (unsigned steps = 0; offset && steps < 64; steps++) {
		const unsigned char *node;
		int order;
		if (offset < floor || offset > end || end - offset < 28) return 0;
		node = dict->data + offset;
		if (!node[16] || node[16] > 240 || 27u + node[16] > end - offset) return 0;
		order = key_compare(key, length, node + 17, node[16]);
		if (!order) return offset;
		offset = u32(node + (order > 0 ? 8 : 0));
	}
	return 0;
}
int scws2_dict_lookup(const scws2_dict_t *dict, const unsigned char *key, size_t length, scws2_word *word)
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
/* Trusted compiler output: check framing, not every dictionary record. */
static scws2_status_t validate(scws2_dict_t *dict)
{
	const unsigned char *data = dict->data;
	uint32_t count, floor;
	if (dict->size < HEADER + TRAILER || memcmp(data, "XDB2", 4) || u32(data + 4) != 4
		|| u32(data + 8) != HEADER || u32(data + 12) || u32(data + 16) != 0xf422f
		|| u32(data + 28) != dict->size || u32(data + 32) != 10
		|| memcmp(data + dict->size - TRAILER, "XDB2END\n", TRAILER)) return SCWS2_EFORMAT;
	for (size_t i = 36; i < HEADER; i++) if (data[i]) return SCWS2_EFORMAT;
	dict->base = u32(data + 16);
	dict->buckets = u32(data + 20);
	count = u32(data + 24);
	if (!dict->buckets || dict->buckets > 1000000
		|| (uint64_t) HEADER + dict->buckets * 8u > dict->size - TRAILER) return SCWS2_EFORMAT;
	floor = HEADER + dict->buckets * 8u;
	if (!count || count > (dict->size - TRAILER - floor) / 28u) return SCWS2_EFORMAT;
	return SCWS2_OK;
}

/* Takes ownership of data, including on failure. */
scws2_status_t scws2_dict_from_data(unsigned char *data, uint32_t size, scws2_dict_t **out)
{
	scws2_dict_t *dict = calloc(1, sizeof(*dict));
	scws2_status_t status;
	if (!dict) { free(data); return SCWS2_ENOMEM; }
	dict->data = data; dict->size = size;
	status = validate(dict);
	if (status) { scws2_dict_close(dict); return status; }
	*out = dict;
	return SCWS2_OK;
}

scws2_status_t scws2_dict_open_ex(const char *path, scws2_dict_flags_t flags, scws2_dict_t **out)
{
	FILE *fp;
	long length;
	scws2_dict_t *dict;
	scws2_status_t status;
	if (!out) return SCWS2_EINVAL;
	*out = NULL;
	if (!path || flags & ~(SCWS2_DICT_TEXT | SCWS2_DICT_MEMORY)) return SCWS2_EINVAL;
	if (flags & SCWS2_DICT_TEXT) return scws2_text_open(path, flags & SCWS2_DICT_MEMORY, out);
	fp = fopen(path, "rb");
	if (!fp) return SCWS2_EIO;
	if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET)) {
		fclose(fp); return SCWS2_EIO;
	}
	if (length < (long) (HEADER + TRAILER) || (unsigned long) length > MAX_DICTIONARY_BYTES) {
		fclose(fp); return length < (long) (HEADER + TRAILER) ? SCWS2_EFORMAT : SCWS2_ELIMIT;
	}
	dict = calloc(1, sizeof(*dict));
	if (!dict) { fclose(fp); return SCWS2_ENOMEM; }
	dict->size = (uint32_t) length;
#ifdef SCWS2_HAVE_MMAP
	if (!(flags & SCWS2_DICT_MEMORY)) {
		void *mapping = mmap(NULL, dict->size, PROT_READ, MAP_SHARED, fileno(fp), 0);
		if (mapping != MAP_FAILED) { dict->data = mapping; dict->mapped = 1; }
	}
#endif
	status = SCWS2_OK;
	if (!dict->mapped) {
		dict->data = malloc(dict->size);
		if (!dict->data) { fclose(fp); free(dict); return SCWS2_ENOMEM; }
		status = fread(dict->data, 1, dict->size, fp) == dict->size ? SCWS2_OK : SCWS2_EIO;
	}
	if (fclose(fp)) status = SCWS2_EIO;
	if (!status) status = validate(dict);
	if (status) { scws2_dict_close(dict); return status; }
	*out = dict;
	return SCWS2_OK;
}
void scws2_dict_close(scws2_dict_t *dict)
{
	if (!dict) return;
#ifdef SCWS2_HAVE_MMAP
	if (dict->mapped) munmap(dict->data, dict->size);
	else
#endif
		free(dict->data);
	free(dict);
}
