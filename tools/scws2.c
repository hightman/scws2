#include "scws2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text_path(const char *path)
{
	size_t length = strlen(path);
	const char *p;
	if (length < 4) return 0;
	p = path + length - 4;
	return p[0] == '.' && (p[1] == 't' || p[1] == 'T')
		&& (p[2] == 'x' || p[2] == 'X') && (p[3] == 't' || p[3] == 'T');
}
static void usage(FILE *fp)
{
	fputs("Usage: scws2 [--mmap] [-d dictionary.xdb|dictionary.txt] [--search] [--bigrams] [--symbols] [-i text]\n"
		"UTF-8 only. Without -i, read stdin. Output: byte_offset, byte_length, char_offset,\n"
		"char_length, POS, kind, origin, original token (tab-separated).\n", fp);
}
int main(int argc, char **argv)
{
	scws2_t ctx = scws2_new();
	scws2_result_t tokens = NULL;
	size_t length = 0;
	unsigned flags = 0;
	scws2_dict_flags_t dict_flags = 0;
	scws2_mode_t mode = SCWS2_STANDARD;
	const char *input = NULL;
	char *owned = NULL;
	int result = EXIT_FAILURE;
	scws2_status_t status = SCWS2_OK;
	if (!ctx) { status = SCWS2_ENOMEM; goto done; }
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); result = EXIT_SUCCESS; goto done; }
		if (!strcmp(argv[i], "--version")) { puts(SCWS2_VERSION); result = EXIT_SUCCESS; goto done; }
		if (!strcmp(argv[i], "--mmap")) dict_flags |= SCWS2_DICT_MMAP;
		else if (!strcmp(argv[i], "--search")) mode = SCWS2_SEARCH;
		else if (!strcmp(argv[i], "--bigrams")) flags |= SCWS2_FALLBACK_BIGRAM;
		else if (!strcmp(argv[i], "--symbols")) flags |= SCWS2_KEEP_SYMBOLS;
		else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			scws2_dict_flags_t loading = dict_flags;
			if (text_path(argv[i + 1])) loading |= SCWS2_DICT_TEXT;
			status = scws2_add_dict(ctx, argv[++i], loading);
			if (status) { fprintf(stderr, "%s: %s\n", argv[i], scws2_strerror(ctx)); goto done; }
		} else { usage(stderr); goto done; }
	}
	status = scws2_set_options(ctx, flags);
	if (status) goto done;
	if (input) length = strlen(input);
	else {
		size_t capacity = 8192;
		owned = malloc(capacity);
		if (!owned) { status = SCWS2_ENOMEM; goto done; }
		for (;;) {
			size_t size = fread(owned + length, 1, capacity - length, stdin);
			length += size;
			if (ferror(stdin)) { status = SCWS2_EIO; goto done; }
			if (feof(stdin)) break;
			if (length == capacity) {
				char *grown;
				if (capacity > SIZE_MAX / 2) { status = SCWS2_ELIMIT; goto done; }
				capacity *= 2;
				grown = realloc(owned, capacity);
				if (!grown) { status = SCWS2_ENOMEM; goto done; }
				owned = grown;
			}
		}
		input = owned;
	}
	tokens = scws2_segment(ctx, input, length, mode);
	if (!tokens) { status = scws2_error_code(ctx); goto done; }
	for (const scws2_token_t *token; (token = scws2_result_next(tokens)) != NULL;) {
		static const char *const kinds[] = {"primary", "secondary", "fallback"};
		static const char *const origins[] = {"dictionary", "builtin", "unknown"};
		printf("%zu\t%zu\t%zu\t%zu\t%s\t%s\t%s\t", token->offset, token->length,
			token->char_offset, token->char_length, token->pos, kinds[token->kind], origins[token->origin]);
		if (fwrite(input + token->offset, 1, token->length, stdout) != token->length || putchar('\n') == EOF) {
			status = SCWS2_EIO; goto done;
		}
	}
	if (fflush(stdout)) { status = SCWS2_EIO; goto done; }
	result = EXIT_SUCCESS;
done:
	if (status) {
		const char *message = ctx && scws2_error_code(ctx) != SCWS2_OK ? scws2_strerror(ctx)
			: status == SCWS2_ENOMEM ? "out of memory" : status == SCWS2_EIO ? "I/O error" : "resource limit exceeded";
		fprintf(stderr, "scws2: %s\n", message);
	}
	scws2_result_free(tokens);
	scws2_free(ctx);
	free(owned);
	return result;
}
