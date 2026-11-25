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
		scws_result_t result = scws_segment(ctx, text, strlen(text), SCWS_STANDARD);
		if (!result || scws_result_count(result) != 3) job->failed = 1;
		scws_result_free(result);
	}
	scws_free(ctx);
	return NULL;
}
static int expect_result(scws_t ctx, const char *text, size_t length, scws_mode_t mode,
	size_t count, size_t first_length)
{
	scws_result_t result = scws_segment(ctx, text, length, mode);
	int failed = !result || scws_result_count(result) != count || scws_error_code(ctx) != SCWS_OK;
	if (!failed && first_length) {
		const scws_token_t *token = scws_result_next(result);
		failed = !token || token->length != first_length;
	}
	scws_result_free(result);
	return failed;
}
static int exercise(const char *path, scws_dict_mode_t mode)
{
	scws_t ctx = scws_new(), second = scws_new();
	scws_result_t a, b, empty;
	const scws_token_t *token, *first;
	const char embedded[] = "中文\0测试";
	const char invalid_utf8[] = {'x', (char) 0xed, (char) 0xa0, (char) 0x80};
	pthread_t threads[4];
	task jobs[4];
	CHECK(ctx && second && scws_error_code(ctx) == SCWS_OK);
	CHECK(scws_set_dict(NULL, path, mode) == SCWS_EINVAL);
	CHECK(scws_set_dict(ctx, NULL, mode) == SCWS_EINVAL && scws_error_code(ctx) == SCWS_EINVAL);
	CHECK(scws_error_code(second) == SCWS_OK);
	CHECK(scws_set_dict(ctx, path, mode) == SCWS_OK && scws_error_code(ctx) == SCWS_OK);
	CHECK(scws_set_dict(second, path, mode) == SCWS_OK);
	CHECK(scws_set_options(ctx, 0x80) == SCWS_EINVAL && scws_error_code(ctx) == SCWS_EINVAL);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_set_unknown_cost(ctx, 0) == SCWS_EINVAL);
	CHECK(scws_set_unknown_cost(NULL, 100) == SCWS_EINVAL);
	CHECK(scws_set_unknown_cost(ctx, 18432) == SCWS_OK);
	CHECK(scws_add_dict(ctx, NULL, mode) == SCWS_EINVAL);
	CHECK(scws_set_dict(ctx, path, (scws_dict_mode_t) 99) == SCWS_EINVAL);
	CHECK(scws_add_dict(ctx, path, (scws_dict_mode_t) 99) == SCWS_EINVAL);
	a = scws_segment(ctx, embedded, sizeof(embedded) - 1, SCWS_STANDARD);
	CHECK(a && scws_result_count(a) == 2);
	first = scws_result_next(a);
	CHECK(first && first->offset == 0 && first->length == 6 && first->char_length == 2 && !strcmp(first->pos, "n"));
	token = scws_result_next(a);
	CHECK(token && token->offset == 7 && token->char_offset == 3 && token->pos[2] == '\0');
	CHECK(scws_result_next(a) == NULL && scws_result_count(a) == 2);
	b = scws_segment(second, "中国", 6, SCWS_STANDARD);
	CHECK(b && scws_result_count(b) == 1 && scws_result_count(a) == 2);
	CHECK(scws_segment(ctx, invalid_utf8, sizeof(invalid_utf8), SCWS_STANDARD) == NULL);
	CHECK(scws_error_code(ctx) == SCWS_EUTF8 && !strcmp(scws_strerror(ctx), "invalid UTF-8"));
	CHECK(scws_result_next(a) == NULL && scws_result_count(a) == 2);
	CHECK(scws_error_code(ctx) == SCWS_EUTF8 && scws_error_code(second) == SCWS_OK);
	empty = scws_segment(ctx, NULL, 0, SCWS_STANDARD);
	CHECK(empty && scws_result_count(empty) == 0 && !scws_result_next(empty));
	CHECK(scws_error_code(ctx) == SCWS_OK && !strcmp(scws_strerror(ctx), "success"));
	scws_result_free(empty);
	empty = scws_segment(ctx, " \t！", strlen(" \t！"), SCWS_STANDARD);
	CHECK(empty && !scws_result_count(empty));
	scws_result_free(empty);
	CHECK(scws_segment(ctx, NULL, 1, SCWS_STANDARD) == NULL && scws_error_code(ctx) == SCWS_EINVAL);
	CHECK(scws_segment(ctx, "中国", 6, (scws_mode_t) 9) == NULL && scws_error_code(ctx) == SCWS_EINVAL);
	CHECK(scws_segment(ctx, "", SIZE_MAX, SCWS_STANDARD) == NULL && scws_error_code(ctx) == SCWS_ELIMIT);
	CHECK(scws_segment(NULL, "", 0, SCWS_STANDARD) == NULL);
	CHECK(scws_error_code(NULL) == SCWS_EINVAL && !strcmp(scws_strerror(NULL), "invalid argument"));
	CHECK(scws_result_count(NULL) == 0 && scws_result_next(NULL) == NULL);
	scws_result_free(NULL);
	for (int i = 0; i < 4; i++) {
		jobs[i] = (task) {path, mode, 0};
		CHECK(pthread_create(&threads[i], NULL, worker, &jobs[i]) == 0);
	}
	for (int i = 0; i < 4; i++) CHECK(pthread_join(threads[i], NULL) == 0 && !jobs[i].failed);
	scws_free(ctx); scws_free(second);
	CHECK(first->length == 6 && !strcmp(first->pos, "n"));
	CHECK(scws_result_count(a) == 2 && scws_result_count(b) == 1);
	scws_result_free(a); scws_result_free(b); scws_free(NULL);
	return 0;
}

static int ownership(const char *base, const char *overlay, scws_dict_mode_t mode)
{
	scws_t ctx = scws_new();
	CHECK(ctx);
	CHECK(expect_result(ctx, "中文 C++ 2026年", strlen("中文 C++ 2026年"), SCWS_STANDARD, 4, 0) == 0);
	CHECK(expect_result(ctx, "中文！", 9, SCWS_STANDARD, 2, 0) == 0);
	CHECK(scws_set_options(ctx, SCWS_KEEP_SYMBOLS) == SCWS_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS_STANDARD, 3, 0) == 0);
	CHECK(scws_set_options(ctx, SCWS_FALLBACK_BIGRAM) == SCWS_OK);
	CHECK(expect_result(ctx, "中文测试", 12, SCWS_SEARCH, 7, 0) == 0);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_set_dict(ctx, base, mode) == SCWS_OK);
	CHECK(scws_set_unknown_cost(ctx, 1) == SCWS_OK);
	CHECK(scws_set_options(ctx, SCWS_KEEP_SYMBOLS) == SCWS_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS_STANDARD, 3, 0) == 0);
	CHECK(scws_set_unknown_cost(ctx, 18432) == SCWS_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS_STANDARD, 2, 0) == 0);
	CHECK(scws_set_options(ctx, 0) == SCWS_OK);
	CHECK(scws_add_dict(ctx, overlay, mode) == SCWS_OK);
	CHECK(expect_result(ctx, "研究生命起源", 18, SCWS_STANDARD, 3, 9) == 0);
	CHECK(scws_set_dict(ctx, base, mode) == SCWS_OK);
	CHECK(expect_result(ctx, "研究生命起源", 18, SCWS_STANDARD, 3, 6) == 0);
	CHECK(scws_set_dict(ctx, overlay, mode) == SCWS_OK);
	CHECK(expect_result(ctx, "中文", 6, SCWS_STANDARD, 2, 0) == 0);
	CHECK(scws_add_dict(ctx, base, mode) == SCWS_OK);
	CHECK(scws_set_dict(ctx, "", mode) == SCWS_EIO && scws_error_code(ctx) == SCWS_EIO);
	CHECK(scws_add_dict(ctx, "", mode) == SCWS_EIO && scws_error_code(ctx) == SCWS_EIO);
	CHECK(expect_result(ctx, "中文", 6, SCWS_STANDARD, 1, 0) == 0);
	scws_free(ctx);
	return 0;
}

static int nested_results(const char *base, const char *overlay, scws_dict_mode_t mode)
{
	scws_t ctx = scws_new();
	scws_result_t outer, retained, large_result;
	const scws_token_t *token, *saved;
	char large[128 * 3];
	size_t visited = 0;
	CHECK(ctx && scws_set_dict(ctx, base, mode) == SCWS_OK);
	outer = scws_segment(ctx, "研究生命起源", 18, SCWS_STANDARD);
	CHECK(outer && scws_result_count(outer) == 3);
	while ((token = scws_result_next(outer)) != NULL) {
		scws_token_t copy = *token;
		scws_result_t inner = scws_segment(ctx, "中文测试", 12, SCWS_STANDARD);
		CHECK(inner && scws_result_count(inner) == 2);
		CHECK(token->offset == copy.offset && token->length == copy.length && !strcmp(token->pos, copy.pos));
		scws_result_free(inner);
		CHECK(scws_segment(ctx, "\xff", 1, SCWS_STANDARD) == NULL);
		CHECK(scws_error_code(ctx) == SCWS_EUTF8);
		CHECK(token->length == copy.length);
		visited++;
	}
	CHECK(visited == 3 && scws_result_count(outer) == 3);
	retained = scws_segment(ctx, "中文测试", 12, SCWS_STANDARD);
	CHECK(retained && (saved = scws_result_next(retained)) != NULL);
	for (size_t i = 0; i < 128; i++) memcpy(large + i * 3, "中", 3);
	large_result = scws_segment(ctx, large, sizeof(large), SCWS_SEARCH);
	CHECK(large_result && scws_result_count(large_result) == 128);
	CHECK(scws_set_options(ctx, SCWS_KEEP_SYMBOLS) == SCWS_OK);
	CHECK(scws_set_dict(ctx, overlay, mode) == SCWS_OK);
	scws_free(ctx);
	CHECK(saved->offset == 0 && saved->length == 6 && !strcmp(saved->pos, "n"));
	CHECK(scws_result_count(retained) == 2 && scws_result_next(retained)->offset == 6);
	CHECK(scws_result_count(outer) == 3 && !scws_result_next(outer));
	CHECK(scws_result_count(large_result) == 128 && scws_result_next(large_result)->length == 3);
	scws_result_free(outer); scws_result_free(large_result); scws_result_free(retained);
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
				scws_result_t ar = scws_segment(a, texts[i], strlen(texts[i]), (scws_mode_t) mode);
				scws_result_t br = scws_segment(b, texts[i], strlen(texts[i]), (scws_mode_t) mode);
				CHECK(ar && br);
				CHECK(scws_result_count(ar) == scws_result_count(br));
				for (size_t j = 0; j < scws_result_count(ar); j++) {
					const scws_token_t *x = scws_result_next(ar), *y = scws_result_next(br);
					CHECK(x && y && x->offset == y->offset && x->length == y->length
						&& x->char_offset == y->char_offset && x->char_length == y->char_length
						&& x->kind == y->kind && x->origin == y->origin && !strcmp(x->pos, y->pos));
				}
				scws_result_free(ar); scws_result_free(br);
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
		CHECK(expect_result(contexts[i], "中文", 6, SCWS_STANDARD, i == 2 ? 2u : 1u, 0) == 0);
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
	CHECK(scws_error_code(ctx) == SCWS_EINVAL && !strcmp(scws_strerror(ctx), "invalid argument"));
#ifndef SCWS_NO_MMAP
	if (argc == 4 && !strcmp(argv[2], "replace")) {
		scws_free(ctx); return replace_file(argv[1], argv[3]);
	}
#endif
	/* Used by Python to check malformed files in either loading mode. */
	if (argc == 4) {
		scws_dict_mode_t mode = !strcmp(argv[2], "mmap") ? SCWS_DICT_MMAP : SCWS_DICT_MEMORY;
		scws_status_t expected = !strcmp(argv[3], "format") ? SCWS_EFORMAT : SCWS_EIO;
		CHECK(scws_set_dict(ctx, argv[1], mode) == expected && scws_error_code(ctx) == expected);
		scws_free(ctx); return 0;
	}
	CHECK(argc == 3);
	CHECK(exercise(argv[1], SCWS_DICT_MEMORY) == 0);
	CHECK(ownership(argv[1], argv[2], SCWS_DICT_MEMORY) == 0);
	CHECK(nested_results(argv[1], argv[2], SCWS_DICT_MEMORY) == 0);
#ifdef SCWS_NO_MMAP
	CHECK(scws_set_dict(ctx, argv[1], SCWS_DICT_MMAP) == SCWS_ENOTSUP);
	CHECK(scws_error_code(ctx) == SCWS_ENOTSUP && !strcmp(scws_strerror(ctx), "operation not supported"));
#else
	CHECK(exercise(argv[1], SCWS_DICT_MMAP) == 0);
	CHECK(ownership(argv[1], argv[2], SCWS_DICT_MMAP) == 0);
	CHECK(nested_results(argv[1], argv[2], SCWS_DICT_MMAP) == 0);
	CHECK(compare_modes(argv[1], argv[2]) == 0);
#endif
	scws_free(ctx);
	return 0;
}
