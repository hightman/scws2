#include "scws.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "API check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct { const char *filename; scws_dict_mode_t mode; int failed; } task;
static void *worker(void *argument)
{
	task *job = argument;
	scws_t ctx = scws_new();
	const char *text = "研究生命起源";
	if (!ctx) { job->failed = 1; return NULL; }
	if (scws_set_dict(ctx, job->filename, job->mode)) {
		job->failed = 1; scws_free(ctx); return NULL;
	}
	for (int i = 0; i < 100; i++) {
		if (scws_segment(ctx, text, strlen(text), SCWS_STANDARD) || scws_count(ctx) != 3) job->failed = 1;
	}
	scws_free(ctx);
	return NULL;
}
static int exercise(const char *path, scws_dict_mode_t mode)
{
	scws_t ctx = scws_new(), second = scws_new();
	const scws_token_t *token;
	const char embedded[] = "中文\0测试";
	const char invalid_utf8[] = {'x', (char) 0xed, (char) 0xa0, (char) 0x80};
	pthread_t threads[4];
	task jobs[4];
	CHECK(ctx && second);
	CHECK(scws_set_dict(NULL, path, mode) == SCWS_EINVAL);
	CHECK(scws_set_dict(ctx, NULL, mode) == SCWS_EINVAL);
	CHECK(scws_set_dict(ctx, path, mode) == SCWS_OK);
	CHECK(scws_set_dict(second, path, mode) == SCWS_OK);
	CHECK(scws_set_options(ctx, 0x80) == SCWS_EINVAL);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_set_unknown_cost(ctx, 0) == SCWS_EINVAL);
	CHECK(scws_set_unknown_cost(NULL, 100) == SCWS_EINVAL);
	CHECK(scws_set_unknown_cost(ctx, 18432) == SCWS_OK);
	CHECK(scws_add_dict(ctx, NULL, mode) == SCWS_EINVAL);
	/* Failed replacement/append leave existing dictionaries usable. */
	CHECK(scws_set_dict(ctx, path, (scws_dict_mode_t) 99) == SCWS_EINVAL);
	CHECK(scws_add_dict(ctx, path, (scws_dict_mode_t) 99) == SCWS_EINVAL);
	CHECK(scws_segment(ctx, embedded, sizeof(embedded) - 1, SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_count(ctx) == 2);
	token = scws_next(ctx);
	CHECK(token && token->offset == 0 && token->length == 6 && token->char_length == 2 && !strcmp(token->pos, "n"));
	token = scws_next(ctx);
	CHECK(token && token->offset == 7 && token->char_offset == 3 && token->pos[2] == '\0');
	CHECK(scws_next(ctx) == NULL && scws_count(ctx) == 2);
	CHECK(scws_segment(second, "中国", 6, SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_count(second) == 1 && scws_count(ctx) == 2);
	CHECK(scws_segment(ctx, invalid_utf8, sizeof(invalid_utf8), SCWS_STANDARD) == SCWS_EUTF8);
	CHECK(scws_count(ctx) == 0 && scws_next(ctx) == NULL);
	CHECK(scws_segment(ctx, NULL, 0, SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_segment(ctx, NULL, 1, SCWS_STANDARD) == SCWS_EINVAL);
	CHECK(scws_segment(ctx, "中国", 6, (scws_mode_t) 9) == SCWS_EINVAL);
	CHECK(scws_count(NULL) == 0 && scws_next(NULL) == NULL);
	for (int i = 0; i < 4; i++) {
		jobs[i] = (task) {path, mode, 0};
		CHECK(pthread_create(&threads[i], NULL, worker, &jobs[i]) == 0);
	}
	for (int i = 0; i < 4; i++) {
		CHECK(pthread_join(threads[i], NULL) == 0 && !jobs[i].failed);
	}
	scws_free(ctx); scws_free(second);
	scws_free(NULL);
	puts("C API lifecycle, errors, offsets and independent-context threads passed.");
	return 0;
}

static int ownership(const char *base, const char *overlay, scws_dict_mode_t mode)
{
	scws_t ctx = scws_new();
	CHECK(ctx);
	/* No dictionary: rules and unknown-character fallback work immediately. */
	CHECK(scws_segment(ctx, "中文 C++ 2026年", strlen("中文 C++ 2026年"), SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_count(ctx) == 4);
	CHECK(scws_segment(ctx, "中文！", 9, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 2);
	CHECK(scws_set_options(ctx, SCWS_KEEP_SYMBOLS) == SCWS_OK);
	CHECK(scws_segment(ctx, "中文！", 9, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 3);
	CHECK(scws_set_options(ctx, SCWS_FALLBACK_BIGRAM) == SCWS_OK);
	CHECK(scws_segment(ctx, "中文测试", 12, SCWS_SEARCH) == SCWS_OK && scws_count(ctx) == 7);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_set_dict(ctx, base, mode) == SCWS_OK);
	CHECK(scws_set_unknown_cost(ctx, 1) == SCWS_OK);
	CHECK(scws_set_options(ctx, SCWS_KEEP_SYMBOLS) == SCWS_OK);
	CHECK(scws_segment(ctx, "中文！", 9, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 3);
	CHECK(scws_set_unknown_cost(ctx, 18432) == SCWS_OK);
	CHECK(scws_segment(ctx, "中文！", 9, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 2);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_add_dict(ctx, overlay, mode) == SCWS_OK);
	CHECK(scws_segment(ctx, "研究生命起源", 18, SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_next(ctx)->length == 9); /* Overlay raises the cost of 研究. */
	CHECK(scws_set_dict(ctx, base, mode) == SCWS_OK);
	CHECK(scws_segment(ctx, "研究生命起源", 18, SCWS_STANDARD) == SCWS_OK);
	CHECK(scws_next(ctx)->length == 6); /* Replacement removes the overlay. */
	CHECK(scws_set_dict(ctx, overlay, mode) == SCWS_OK);
	CHECK(scws_segment(ctx, "中文", 6, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 2);
	CHECK(scws_add_dict(ctx, base, mode) == SCWS_OK);
	CHECK(scws_set_dict(ctx, "", mode) == SCWS_EIO);
	CHECK(scws_add_dict(ctx, "", mode) == SCWS_EIO);
	CHECK(scws_segment(ctx, "中文", 6, SCWS_STANDARD) == SCWS_OK && scws_count(ctx) == 1);
	scws_free(ctx);
	return 0;
}

#ifndef SCWS_NO_MMAP
static int compare_modes(const char *path, const char *overlay)
{
	scws_t a = scws_new(), b = scws_new();
	const char *texts[] = {"研究生命起源南京市长江大桥", "中文 C++ ２０２６年三点五公斤", "𠀀词未知词人工智能"};
	CHECK(a && b);
	CHECK(scws_set_dict(a, path, SCWS_DICT_MEMORY) == SCWS_OK);
	CHECK(scws_set_dict(b, path, SCWS_DICT_MMAP) == SCWS_OK);
	for (int layer = 0; layer < 2; layer++) {
		if (layer) {
			CHECK(scws_add_dict(a, overlay, SCWS_DICT_MMAP) == SCWS_OK);
			CHECK(scws_add_dict(b, overlay, SCWS_DICT_MEMORY) == SCWS_OK);
		}
		for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
			for (int mode = SCWS_STANDARD; mode <= SCWS_SEARCH; mode++) {
				CHECK(scws_segment(a, texts[i], strlen(texts[i]), (scws_mode_t) mode) == SCWS_OK);
				CHECK(scws_segment(b, texts[i], strlen(texts[i]), (scws_mode_t) mode) == SCWS_OK);
				CHECK(scws_count(a) == scws_count(b));
				for (size_t j = 0; j < scws_count(a); j++) {
					const scws_token_t *x = scws_next(a), *y = scws_next(b);
					CHECK(x && y && x->offset == y->offset && x->length == y->length
						&& x->char_offset == y->char_offset && x->char_length == y->char_length
						&& x->kind == y->kind && x->origin == y->origin && !strcmp(x->pos, y->pos));
				}
			}
		}
	}
	scws_free(a); scws_free(b);
	return 0;
}

static int replace_file(const char *path, const char *replacement)
{
	scws_t contexts[3] = {scws_new(), scws_new(), scws_new()};
	CHECK(contexts[0] && contexts[1] && contexts[2]);
	CHECK(scws_set_dict(contexts[0], path, SCWS_DICT_MEMORY) == SCWS_OK);
	CHECK(scws_set_dict(contexts[1], path, SCWS_DICT_MMAP) == SCWS_OK);
	CHECK(rename(replacement, path) == 0);
	CHECK(scws_set_dict(contexts[2], path, SCWS_DICT_MMAP) == SCWS_OK);
	for (int i = 0; i < 3; i++) {
		CHECK(scws_segment(contexts[i], "中文", 6, SCWS_STANDARD) == SCWS_OK);
		CHECK(scws_count(contexts[i]) == (i == 2 ? 2u : 1u));
		scws_free(contexts[i]);
	}
	return 0;
}

#endif

int main(int argc, char **argv)
{
	scws_t ctx = scws_new();
	CHECK(ctx);
	CHECK(argc >= 2);
	CHECK(scws_set_dict(ctx, argv[1], (scws_dict_mode_t) 99) == SCWS_EINVAL);
	CHECK(!strcmp(scws_strerror(SCWS_ENOTSUP), "operation not supported"));
#ifndef SCWS_NO_MMAP
	if (argc == 4 && !strcmp(argv[2], "replace")) {
		scws_free(ctx); return replace_file(argv[1], argv[3]);
	}
#endif
	/* Used by Python to check malformed files in either loading mode. */
	if (argc == 4) {
		scws_dict_mode_t mode = !strcmp(argv[2], "mmap") ? SCWS_DICT_MMAP : SCWS_DICT_MEMORY;
		scws_status_t expected = !strcmp(argv[3], "format") ? SCWS_EFORMAT : SCWS_EIO;
		CHECK(scws_set_dict(ctx, argv[1], mode) == expected);
		scws_free(ctx); return 0;
	}
	scws_free(ctx);
	CHECK(argc == 3);
	CHECK(exercise(argv[1], SCWS_DICT_MEMORY) == 0);
	CHECK(ownership(argv[1], argv[2], SCWS_DICT_MEMORY) == 0);
#ifdef SCWS_NO_MMAP
	CHECK(scws_set_dict(ctx, argv[1], SCWS_DICT_MMAP) == SCWS_ENOTSUP);
#else
	CHECK(exercise(argv[1], SCWS_DICT_MMAP) == 0);
	CHECK(ownership(argv[1], argv[2], SCWS_DICT_MMAP) == 0);
	CHECK(compare_modes(argv[1], argv[2]) == 0);
#endif
	return 0;
}
