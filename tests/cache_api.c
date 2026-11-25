/* Inspect storage without adding a public API. These sources share this TU. */
#define _POSIX_C_SOURCE 200809L
#include "../src/scws2.c"
#ifdef SCWS2_TEST_MMAP_FAILURE
#include <sys/mman.h>
#define mmap(...) MAP_FAILED
#endif
#include "../src/xdb.c"
#include <stdio.h>
int main(int argc, char **argv)
{
	scws2_t ctx = scws2_new();
	int failed = 0;
	if (argc < 5 || !ctx) return 2;
	if (strcmp(argv[2], "-") && scws2_set_dict(ctx, argv[2], (scws2_dict_flags_t) strtoul(argv[1], NULL, 10))) failed = 1;
	if (!failed && scws2_add_dict(ctx, argv[2], 4) != SCWS2_EINVAL) failed = 1;
	for (int i = 4; !failed && i < argc; i++) {
		if (scws2_add_dict(ctx, argv[i], (scws2_dict_flags_t) strtoul(argv[3], NULL, 10))) failed = 1;
		else printf("%d\n", ctx->dicts[ctx->dict_count - 1]->mapped);
	}
	if (failed) fprintf(stderr, "%s\n", scws2_strerror(ctx));
	scws2_free(ctx);
	return failed;
}
