/* Inspect storage without adding a public API. These sources share this TU. */
#define _POSIX_C_SOURCE 200809L
#include "../src/scws.c"
#include "../src/xdb.c"
#include <stdio.h>
int main(int argc, char **argv)
{
	scws_t ctx = scws_new();
	int failed = 0;
	if (argc < 5 || !ctx) return 2;
	if (strcmp(argv[2], "-") && scws_set_dict(ctx, argv[2], (scws_dict_flags_t) strtoul(argv[1], NULL, 10))) failed = 1;
	if (!failed && scws_add_dict(ctx, argv[2], 4) != SCWS_EINVAL) failed = 1;
	for (int i = 4; !failed && i < argc; i++) {
		if (scws_add_dict(ctx, argv[i], (scws_dict_flags_t) strtoul(argv[3], NULL, 10))) failed = 1;
		else printf("%u\n", ctx->dicts[ctx->dict_count - 1]->flags);
	}
	if (failed) fprintf(stderr, "%s\n", scws_strerror(ctx));
	scws_free(ctx);
	return failed;
}
