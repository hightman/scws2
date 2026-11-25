#include "scws2.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "API check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct { const char *filename; scws2_dict_flags_t mode; int failed; } task;
static void *worker(void *argument)
{
	task *job = argument;
	scws2_t ctx = scws2_new();
	const char *text = "研究生命起源";
	if (!ctx) { job->failed = 1; return NULL; }
	if (scws2_set_dict(ctx, job->filename, job->mode)) {
		job->failed = 1; scws2_free(ctx); return NULL;
	}
	for (int i = 0; i < 100; i++) {
		scws2_result_t result = scws2_segment(ctx, text, strlen(text), SCWS2_STANDARD);
		if (!result || scws2_result_count(result) != 3) job->failed = 1;
		scws2_result_free(result);
	}
	scws2_free(ctx);
	return NULL;
}
static int expect_result(scws2_t ctx, const char *text, size_t length, scws2_mode_t mode,
	size_t count, size_t first_length)
{
	scws2_result_t result = scws2_segment(ctx, text, length, mode);
	int failed = !result || scws2_result_count(result) != count || scws2_error_code(ctx) != SCWS2_OK;
	if (!failed && first_length) {
		const scws2_token_t *token = scws2_result_next(result);
		failed = !token || token->length != first_length;
	}
	scws2_result_free(result);
	return failed;
}
static int exercise(const char *path, scws2_dict_flags_t mode)
{
	scws2_t ctx = scws2_new(), second = scws2_new();
	scws2_result_t a, b, empty;
	const scws2_token_t *token, *first;
	const char embedded[] = "中文\0测试";
	const char invalid_utf8[] = {'x', (char) 0xed, (char) 0xa0, (char) 0x80};
	pthread_t threads[4];
	task jobs[4];
	CHECK(ctx && second && scws2_error_code(ctx) == SCWS2_OK);
	CHECK(scws2_set_dict(NULL, path, mode) == SCWS2_EINVAL);
	CHECK(scws2_set_dict(ctx, NULL, mode) == SCWS2_EINVAL && scws2_error_code(ctx) == SCWS2_EINVAL);
	CHECK(scws2_error_code(second) == SCWS2_OK);
	CHECK(scws2_set_dict(ctx, path, mode) == SCWS2_OK && scws2_error_code(ctx) == SCWS2_OK);
	CHECK(scws2_set_dict(second, path, mode) == SCWS2_OK);
	CHECK(scws2_set_options(ctx, 0x80) == SCWS2_EINVAL && scws2_error_code(ctx) == SCWS2_EINVAL);
	CHECK(scws2_set_options(ctx, 0) == SCWS2_OK);
	CHECK(scws2_set_unknown_cost(ctx, 0) == SCWS2_EINVAL);
	CHECK(scws2_set_unknown_cost(NULL, 100) == SCWS2_EINVAL);
	CHECK(scws2_set_unknown_cost(ctx, 18432) == SCWS2_OK);
	CHECK(scws2_add_dict(ctx, NULL, mode) == SCWS2_EINVAL);
	CHECK(scws2_set_dict(ctx, path, (scws2_dict_flags_t) 99) == SCWS2_EINVAL);
	CHECK(scws2_add_dict(ctx, path, (scws2_dict_flags_t) 99) == SCWS2_EINVAL);
	a = scws2_segment(ctx, embedded, sizeof(embedded) - 1, SCWS2_STANDARD);
	CHECK(a && scws2_result_count(a) == 2);
	first = scws2_result_next(a);
	CHECK(first && first->offset == 0 && first->length == 6 && first->char_length == 2 && !strcmp(first->pos, "n"));
	token = scws2_result_next(a);
	CHECK(token && token->offset == 7 && token->char_offset == 3 && token->pos[2] == '\0');
	CHECK(scws2_result_next(a) == NULL && scws2_result_count(a) == 2);
	b = scws2_segment(second, "中国", 6, SCWS2_STANDARD);
	CHECK(b && scws2_result_count(b) == 1 && scws2_result_count(a) == 2);
	CHECK(scws2_segment(ctx, invalid_utf8, sizeof(invalid_utf8), SCWS2_STANDARD) == NULL);
	CHECK(scws2_error_code(ctx) == SCWS2_EUTF8 && !strcmp(scws2_strerror(ctx), "invalid UTF-8"));
	CHECK(scws2_result_next(a) == NULL && scws2_result_count(a) == 2);
	CHECK(scws2_error_code(ctx) == SCWS2_EUTF8 && scws2_error_code(second) == SCWS2_OK);
	empty = scws2_segment(ctx, NULL, 0, SCWS2_STANDARD);
	CHECK(empty && scws2_result_count(empty) == 0 && !scws2_result_next(empty));
	CHECK(scws2_error_code(ctx) == SCWS2_OK && !strcmp(scws2_strerror(ctx), "success"));
	scws2_result_free(empty);
	empty = scws2_segment(ctx, " \t！", strlen(" \t！"), SCWS2_STANDARD);
	CHECK(empty && !scws2_result_count(empty));
	scws2_result_free(empty);
	CHECK(scws2_segment(ctx, NULL, 1, SCWS2_STANDARD) == NULL && scws2_error_code(ctx) == SCWS2_EINVAL);
	CHECK(scws2_segment(ctx, "中国", 6, (scws2_mode_t) 9) == NULL && scws2_error_code(ctx) == SCWS2_EINVAL);
	CHECK(scws2_segment(ctx, "", SIZE_MAX, SCWS2_STANDARD) == NULL && scws2_error_code(ctx) == SCWS2_ELIMIT);
	CHECK(scws2_segment(NULL, "", 0, SCWS2_STANDARD) == NULL);
	CHECK(scws2_error_code(NULL) == SCWS2_EINVAL && !strcmp(scws2_strerror(NULL), "invalid argument"));
	CHECK(scws2_result_count(NULL) == 0 && scws2_result_next(NULL) == NULL);
	scws2_result_free(NULL);
	for (int i = 0; i < 4; i++) {
		jobs[i] = (task) {path, mode, 0};
		CHECK(pthread_create(&threads[i], NULL, worker, &jobs[i]) == 0);
	}
	for (int i = 0; i < 4; i++) CHECK(pthread_join(threads[i], NULL) == 0 && !jobs[i].failed);
	scws2_free(ctx); scws2_free(second);
	CHECK(first->length == 6 && !strcmp(first->pos, "n"));
	CHECK(scws2_result_count(a) == 2 && scws2_result_count(b) == 1);
	scws2_result_free(a); scws2_result_free(b); scws2_free(NULL);
	return 0;
}

static int ownership(const char *base, const char *overlay, scws2_dict_flags_t mode)
{
	scws2_t ctx = scws2_new();
	CHECK(ctx);
	CHECK(expect_result(ctx, "中文 C++ 2026年", strlen("中文 C++ 2026年"), SCWS2_STANDARD, 4, 0) == 0);
	CHECK(expect_result(ctx, "中文！", 9, SCWS2_STANDARD, 2, 0) == 0);
	CHECK(scws2_set_options(ctx, SCWS2_KEEP_SYMBOLS) == SCWS2_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS2_STANDARD, 3, 0) == 0);
	CHECK(scws2_set_options(ctx, SCWS2_FALLBACK_BIGRAM) == SCWS2_OK);
	CHECK(expect_result(ctx, "中文测试", 12, SCWS2_SEARCH, 7, 0) == 0);
	CHECK(scws2_set_options(ctx, 0) == SCWS2_OK);
	CHECK(scws2_set_dict(ctx, base, mode) == SCWS2_OK);
	CHECK(scws2_set_unknown_cost(ctx, 1) == SCWS2_OK);
	CHECK(scws2_set_options(ctx, SCWS2_KEEP_SYMBOLS) == SCWS2_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS2_STANDARD, 3, 0) == 0);
	CHECK(scws2_set_unknown_cost(ctx, 18432) == SCWS2_OK);
	CHECK(expect_result(ctx, "中文！", 9, SCWS2_STANDARD, 2, 0) == 0);
	CHECK(scws2_set_options(ctx, 0) == SCWS2_OK);
	CHECK(scws2_add_dict(ctx, overlay, mode) == SCWS2_OK);
	CHECK(expect_result(ctx, "研究生命起源", 18, SCWS2_STANDARD, 3, 9) == 0);
	CHECK(scws2_set_dict(ctx, base, mode) == SCWS2_OK);
	CHECK(expect_result(ctx, "研究生命起源", 18, SCWS2_STANDARD, 3, 6) == 0);
	CHECK(scws2_set_dict(ctx, overlay, mode) == SCWS2_OK);
	CHECK(expect_result(ctx, "中文", 6, SCWS2_STANDARD, 2, 0) == 0);
	CHECK(scws2_add_dict(ctx, base, mode) == SCWS2_OK);
	CHECK(scws2_set_dict(ctx, "", mode) == SCWS2_EIO && scws2_error_code(ctx) == SCWS2_EIO);
	CHECK(scws2_add_dict(ctx, "", mode) == SCWS2_EIO && scws2_error_code(ctx) == SCWS2_EIO);
	CHECK(expect_result(ctx, "中文", 6, SCWS2_STANDARD, 1, 0) == 0);
	scws2_free(ctx);
	return 0;
}

static int nested_results(const char *base, const char *overlay, scws2_dict_flags_t mode)
{
	scws2_t ctx = scws2_new();
	scws2_result_t outer, retained, large_result;
	const scws2_token_t *token, *saved;
	char large[128 * 3];
	size_t visited = 0;
	CHECK(ctx && scws2_set_dict(ctx, base, mode) == SCWS2_OK);
	outer = scws2_segment(ctx, "研究生命起源", 18, SCWS2_STANDARD);
	CHECK(outer && scws2_result_count(outer) == 3);
	while ((token = scws2_result_next(outer)) != NULL) {
		scws2_token_t copy = *token;
		scws2_result_t inner = scws2_segment(ctx, "中文测试", 12, SCWS2_STANDARD);
		CHECK(inner && scws2_result_count(inner) == 2);
		CHECK(token->offset == copy.offset && token->length == copy.length && !strcmp(token->pos, copy.pos));
		scws2_result_free(inner);
		CHECK(scws2_segment(ctx, "\xff", 1, SCWS2_STANDARD) == NULL);
		CHECK(scws2_error_code(ctx) == SCWS2_EUTF8);
		CHECK(token->length == copy.length);
		visited++;
	}
	CHECK(visited == 3 && scws2_result_count(outer) == 3);
	retained = scws2_segment(ctx, "中文测试", 12, SCWS2_STANDARD);
	CHECK(retained && (saved = scws2_result_next(retained)) != NULL);
	for (size_t i = 0; i < 128; i++) memcpy(large + i * 3, "中", 3);
	large_result = scws2_segment(ctx, large, sizeof(large), SCWS2_SEARCH);
	CHECK(large_result && scws2_result_count(large_result) == 128);
	CHECK(scws2_set_options(ctx, SCWS2_KEEP_SYMBOLS) == SCWS2_OK);
	CHECK(scws2_set_dict(ctx, overlay, mode) == SCWS2_OK);
	scws2_free(ctx);
	CHECK(saved->offset == 0 && saved->length == 6 && !strcmp(saved->pos, "n"));
	CHECK(scws2_result_count(retained) == 2 && scws2_result_next(retained)->offset == 6);
	CHECK(scws2_result_count(outer) == 3 && !scws2_result_next(outer));
	CHECK(scws2_result_count(large_result) == 128 && scws2_result_next(large_result)->length == 3);
	scws2_result_free(outer); scws2_result_free(large_result); scws2_result_free(retained);
	return 0;
}

static int text_ownership(const char *base, const char *text, const char *malformed)
{
	scws2_t ctx = scws2_new();
	scws2_result_t before, after;
	CHECK(ctx && scws2_set_dict(ctx, base, 0) == SCWS2_OK);
	before = scws2_segment(ctx, "中国", 6, SCWS2_STANDARD);
	CHECK(before && !strcmp(scws2_result_next(before)->pos, "ns"));
	CHECK(scws2_add_dict(ctx, text, SCWS2_DICT_TEXT) == SCWS2_OK);
	after = scws2_segment(ctx, "中国素词新词甲", strlen("中国素词新词甲"), SCWS2_STANDARD);
	CHECK(after && scws2_result_count(after) == 3);
	CHECK(!strcmp(scws2_result_next(after)->pos, "nz"));
	CHECK(!strcmp(scws2_result_next(after)->pos, "un"));
	CHECK(!strcmp(scws2_result_next(after)->pos, "n"));
	CHECK(scws2_set_dict(ctx, malformed, SCWS2_DICT_TEXT) == SCWS2_EFORMAT);
	CHECK(scws2_add_dict(ctx, malformed, SCWS2_DICT_TEXT) == SCWS2_EFORMAT);
	CHECK(expect_result(ctx, "中国", 6, SCWS2_STANDARD, 1, 6) == 0);
	CHECK(scws2_set_dict(ctx, text, SCWS2_DICT_TEXT) == SCWS2_OK);
	CHECK(expect_result(ctx, "研究生命起源", 18, SCWS2_STANDARD, 6, 3) == 0);
	scws2_free(ctx);
	CHECK(scws2_result_count(before) == 1 && scws2_result_count(after) == 3);
	scws2_result_free(before); scws2_result_free(after);
	return 0;
}

static int compare_modes(const char *path, const char *overlay)
{
	scws2_t a = scws2_new(), b = scws2_new();
	const char *texts[] = {"研究生命起源南京市长江大桥", "中文 C++ ２０２６年三点五公斤", "𠀀词未知词人工智能"};
	CHECK(a && b);
	CHECK(scws2_set_dict(a, path, 0) == SCWS2_OK);
	CHECK(scws2_set_dict(b, path, SCWS2_DICT_MMAP) == SCWS2_OK);
	for (int layer = 0; layer < 2; layer++) {
		if (layer) {
			CHECK(scws2_add_dict(a, overlay, SCWS2_DICT_MMAP) == SCWS2_OK);
			CHECK(scws2_add_dict(b, overlay, 0) == SCWS2_OK);
		}
		for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
			for (int mode = SCWS2_STANDARD; mode <= SCWS2_SEARCH; mode++) {
				scws2_result_t ar = scws2_segment(a, texts[i], strlen(texts[i]), (scws2_mode_t) mode);
				scws2_result_t br = scws2_segment(b, texts[i], strlen(texts[i]), (scws2_mode_t) mode);
				CHECK(ar && br);
				CHECK(scws2_result_count(ar) == scws2_result_count(br));
				for (size_t j = 0; j < scws2_result_count(ar); j++) {
					const scws2_token_t *x = scws2_result_next(ar), *y = scws2_result_next(br);
					CHECK(x && y && x->offset == y->offset && x->length == y->length
						&& x->char_offset == y->char_offset && x->char_length == y->char_length
						&& x->kind == y->kind && x->origin == y->origin && !strcmp(x->pos, y->pos));
				}
				scws2_result_free(ar); scws2_result_free(br);
			}
		}
	}
	scws2_free(a); scws2_free(b);
	return 0;
}

#ifndef SCWS2_NO_MMAP
static int replace_file(const char *path, const char *replacement)
{
	scws2_t contexts[3] = {scws2_new(), scws2_new(), scws2_new()};
	CHECK(contexts[0] && contexts[1] && contexts[2]);
	CHECK(scws2_set_dict(contexts[0], path, 0) == SCWS2_OK);
	CHECK(scws2_set_dict(contexts[1], path, SCWS2_DICT_MMAP) == SCWS2_OK);
	CHECK(rename(replacement, path) == 0);
	CHECK(scws2_set_dict(contexts[2], path, SCWS2_DICT_MMAP) == SCWS2_OK);
	for (int i = 0; i < 3; i++) {
		CHECK(expect_result(contexts[i], "中文", 6, SCWS2_STANDARD, i == 2 ? 2u : 1u, 0) == 0);
		scws2_free(contexts[i]);
	}
	return 0;
}

#endif

int main(int argc, char **argv)
{
	scws2_t ctx = scws2_new();
	CHECK(ctx);
	CHECK(argc >= 2);
	if (argc == 5 && !strcmp(argv[2], "text-result")) {
		scws2_free(ctx); return text_ownership(argv[1], argv[3], argv[4]);
	}
	CHECK(scws2_set_dict(ctx, argv[1], (scws2_dict_flags_t) 99) == SCWS2_EINVAL);
	CHECK(scws2_error_code(ctx) == SCWS2_EINVAL && !strcmp(scws2_strerror(ctx), "invalid argument"));
#ifndef SCWS2_NO_MMAP
	if (argc == 4 && !strcmp(argv[2], "replace")) {
		scws2_free(ctx); return replace_file(argv[1], argv[3]);
	}
#endif
	/* Used by Python to check malformed files in either loading mode. */
	if (argc == 4) {
		scws2_dict_flags_t mode = !strcmp(argv[2], "mmap") ? SCWS2_DICT_MMAP : 0;
		scws2_status_t expected = !strcmp(argv[3], "format") ? SCWS2_EFORMAT : SCWS2_EIO;
		CHECK(scws2_set_dict(ctx, argv[1], mode) == expected && scws2_error_code(ctx) == expected);
		scws2_free(ctx); return 0;
	}
	CHECK(argc == 3);
	CHECK(exercise(argv[1], 0) == 0);
	CHECK(ownership(argv[1], argv[2], 0) == 0);
	CHECK(nested_results(argv[1], argv[2], 0) == 0);
	CHECK(exercise(argv[1], SCWS2_DICT_MMAP) == 0);
	CHECK(ownership(argv[1], argv[2], SCWS2_DICT_MMAP) == 0);
	CHECK(nested_results(argv[1], argv[2], SCWS2_DICT_MMAP) == 0);
	CHECK(compare_modes(argv[1], argv[2]) == 0);
	scws2_free(ctx);
	return 0;
}
