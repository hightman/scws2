#include "scws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *fp)
{
	fputs("Usage: scws [-d dictionary.xdb] [-d overlay.xdb] [--search] [--bigrams] [--symbols] [-i text]\n"
		"UTF-8 only. Without -i, read stdin. Output: byte_offset, byte_length, char_offset,\n"
		"char_length, POS, kind, origin, original token (tab-separated).\n", fp);
}
int main(int argc, char **argv)
{
	scws_t ctx = scws_new();
	scws_result_t tokens = NULL;
	size_t length = 0;
	unsigned flags = 0;
	scws_mode_t mode = SCWS_STANDARD;
	const char *input = NULL;
	char *owned = NULL;
	int result = EXIT_FAILURE;
	scws_status_t status = SCWS_OK;
	if (!ctx) { status = SCWS_ENOMEM; goto done; }
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); result = EXIT_SUCCESS; goto done; }
		if (!strcmp(argv[i], "--version")) { puts(SCWS_VERSION); result = EXIT_SUCCESS; goto done; }
		if (!strcmp(argv[i], "--search")) mode = SCWS_SEARCH;
		else if (!strcmp(argv[i], "--bigrams")) flags |= SCWS_FALLBACK_BIGRAM;
		else if (!strcmp(argv[i], "--symbols")) flags |= SCWS_KEEP_SYMBOLS;
		else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			status = scws_add_dict(ctx, argv[++i], SCWS_DICT_MEMORY);
			if (status) { fprintf(stderr, "%s: %s\n", argv[i], scws_strerror(ctx)); goto done; }
		} else { usage(stderr); goto done; }
	}
	status = scws_set_options(ctx, flags);
	if (status) goto done;
	if (input) length = strlen(input);
	else {
		size_t capacity = 8192;
		owned = malloc(capacity);
		if (!owned) { status = SCWS_ENOMEM; goto done; }
		for (;;) {
			size_t size = fread(owned + length, 1, capacity - length, stdin);
			length += size;
			if (ferror(stdin)) { status = SCWS_EIO; goto done; }
			if (feof(stdin)) break;
			if (length == capacity) {
				char *grown;
				if (capacity > SIZE_MAX / 2) { status = SCWS_ELIMIT; goto done; }
				capacity *= 2;
				grown = realloc(owned, capacity);
				if (!grown) { status = SCWS_ENOMEM; goto done; }
				owned = grown;
			}
		}
		input = owned;
	}
	tokens = scws_segment(ctx, input, length, mode);
	if (!tokens) { status = scws_error_code(ctx); goto done; }
	for (const scws_token_t *token; (token = scws_result_next(tokens)) != NULL;) {
		static const char *const kinds[] = {"primary", "secondary", "fallback"};
		static const char *const origins[] = {"dictionary", "builtin", "unknown"};
		printf("%zu\t%zu\t%zu\t%zu\t%s\t%s\t%s\t", token->offset, token->length,
			token->char_offset, token->char_length, token->pos, kinds[token->kind], origins[token->origin]);
		if (fwrite(input + token->offset, 1, token->length, stdout) != token->length || putchar('\n') == EOF) {
			status = SCWS_EIO; goto done;
		}
	}
	if (fflush(stdout)) { status = SCWS_EIO; goto done; }
	result = EXIT_SUCCESS;
done:
	if (status) {
		const char *message = ctx && scws_error_code(ctx) != SCWS_OK ? scws_strerror(ctx)
			: status == SCWS_ENOMEM ? "out of memory" : status == SCWS_EIO ? "I/O error" : "resource limit exceeded";
		fprintf(stderr, "scws: %s\n", message);
	}
	scws_result_free(tokens);
	scws_free(ctx);
	free(owned);
	return result;
}
