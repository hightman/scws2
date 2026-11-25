/* Native, single-thread segmentation benchmark for scws2. */
#define _POSIX_C_SOURCE 200809L
#include "scws2.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { char *text; size_t size; } sentence;
static void fail(const char *message)
{
	fprintf(stderr, "%s\n", message);
	exit(2);
}
static double now(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts)) fail("clock_gettime failed");
	return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}
static scws2_mode_t output_mode = SCWS2_STANDARD;

static size_t segment(scws2_t ctx, const sentence *line, int dump)
{
	size_t count = 0;
	const scws2_token_t *token;
	scws2_result_t result = scws2_segment(ctx, line->text, line->size, output_mode);
	if (!result) fail(scws2_strerror(ctx));
	while ((token = scws2_result_next(result)) != NULL) {
		if (dump) {
			if (count) putchar(' ');
			if (dump == 2) printf("%zu:%zu", token->offset, token->length);
			else fwrite(line->text + token->offset, 1, token->length, stdout);
		}
		count++;
	}
	scws2_result_free(result);
	if (dump) putchar('\n');
	return count;
}
int main(int argc, char **argv)
{
	FILE *fp;
	sentence *lines = NULL;
	char *buffer = NULL, *end;
	size_t count = 0, capacity = 0, bytes = 0, tokens = 0;
	long iterations;
	double start, elapsed;
	scws2_t ctx;
	if (argc != 5 && argc != 6) fail("Usage: bench dict corpus iterations memory|mmap [standard|search]");
	if (argc == 6) {
		if (!strcmp(argv[5], "search")) output_mode = SCWS2_SEARCH;
		else if (strcmp(argv[5], "standard")) fail("invalid segmentation mode");
	}
	iterations = strtol(argv[3], &end, 10);
	if (*end || iterations < -1 || iterations > 1000000) fail("invalid iterations");
	if (strcmp(argv[4], "memory") && strcmp(argv[4], "mmap")) fail("invalid loading mode");
	fp = fopen(argv[2], "rb");
	if (!fp) fail("cannot open corpus");
	for (;;) {
		long length = (long) getline(&buffer, &capacity, fp);
		sentence *grown;
		if (length < 0) break;
		while (length && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) length--;
		if (!length || length > INT_MAX) fail("empty or oversized sentence");
		grown = realloc(lines, (count + 1) * sizeof(*lines));
		if (!grown) fail("out of memory");
		lines = grown;
		lines[count].text = malloc((size_t) length + 1);
		if (!lines[count].text) fail("out of memory");
		memcpy(lines[count].text, buffer, (size_t) length);
		lines[count].text[length] = '\0';
		lines[count].size = (size_t) length;
		bytes += (size_t) length; count++;
	}
	if (ferror(fp) || !count) fail("cannot read corpus");
	fclose(fp); free(buffer);
	ctx = scws2_new();
	if (!ctx) fail("out of memory");
	if (scws2_set_dict(ctx, argv[1], !strcmp(argv[4], "memory") ? SCWS2_DICT_MEMORY : 0)
		|| scws2_set_options(ctx, SCWS2_KEEP_SYMBOLS)) fail("cannot configure dictionary");
	/* One warm-up; corpus/dictionary loading and output formatting excluded. */
	if (iterations > 0) for (size_t i = 0; i < count; i++) segment(ctx, &lines[i], 0);
	start = now();
	for (long repeat = 0; repeat < (iterations > 0 ? iterations : 1); repeat++)
		for (size_t i = 0; i < count; i++) tokens += segment(ctx, &lines[i], iterations > 0 ? 0 : iterations < 0 ? 2 : 1);
	elapsed = now() - start;
	if (iterations > 0) printf("{\"seconds\":%.9f,\"iterations\":%ld,\"bytes\":%zu,\"tokens\":%zu}\n",
		elapsed, iterations, bytes, tokens);
	scws2_free(ctx);
	for (size_t i = 0; i < count; i++) free(lines[i].text);
	free(lines);
	return ferror(stdout) ? 2 : 0;
}
