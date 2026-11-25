#include "scws.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "API check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct { const scws_dict *dict; int failed; } task;
static void *worker(void *argument)
{
	task *job = argument;
	scws *ctx = NULL;
	const char *text = "研究生命起源";
	if (scws_new(job->dict, &ctx)) { job->failed = 1; return NULL; }
	for (int i = 0; i < 100; i++) {
		if (scws_segment(ctx, text, strlen(text), SCWS_STANDARD) || scws_count(ctx) != 3) job->failed = 1;
	}
	scws_free(ctx);
	return NULL;
}
static int exercise(const char *path, scws_dict_mode mode)
{
	scws_dict *dict = NULL, *invalid = NULL;
	scws *ctx = NULL, *second = NULL;
	const scws_token *token;
	const char embedded[] = "中文\0测试";
	const char invalid_utf8[] = {'x', (char) 0xed, (char) 0xa0, (char) 0x80};
	pthread_t threads[4];
	task jobs[4];
	CHECK(scws_dict_open_ex(NULL, mode, &invalid) == SCWS_EINVAL && invalid == NULL);
	CHECK(scws_dict_open_ex(path, mode, NULL) == SCWS_EINVAL);
	CHECK(scws_dict_open_ex(path, mode, &dict) == SCWS_OK);
	CHECK(scws_new(NULL, &ctx) == SCWS_EINVAL && ctx == NULL);
	CHECK(scws_new(dict, &ctx) == SCWS_OK);
	CHECK(scws_new(dict, &second) == SCWS_OK);
	CHECK(scws_set_options(ctx, 0x80, 18432) == SCWS_EINVAL);
	CHECK(scws_set_options(ctx, 0, 0) == SCWS_EINVAL);
	CHECK(scws_set_options(ctx, 0, 18432) == SCWS_OK);
	CHECK(scws_add_dict(ctx, NULL) == SCWS_EINVAL);
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
	CHECK(scws_segment(ctx, "中国", 6, (scws_mode) 9) == SCWS_EINVAL);
	CHECK(scws_count(NULL) == 0 && scws_next(NULL) == NULL);
	for (int i = 0; i < 4; i++) {
		jobs[i] = (task) {dict, 0};
		CHECK(pthread_create(&threads[i], NULL, worker, &jobs[i]) == 0);
	}
	for (int i = 0; i < 4; i++) {
		CHECK(pthread_join(threads[i], NULL) == 0 && !jobs[i].failed);
	}
	scws_free(ctx); scws_free(second); scws_dict_close(dict);
	scws_free(NULL); scws_dict_close(NULL);
	puts("C API lifecycle, errors, offsets and shared-dictionary threads passed.");
	return 0;
}

#ifndef SCWS_NO_MMAP
static int compare_modes(const char *path, const char *overlay)
{
	scws_dict *memory = NULL, *mapped = NULL, *extra = NULL;
	scws *a = NULL, *b = NULL;
	const char *texts[] = {"研究生命起源南京市长江大桥", "中文 C++ ２０２６年三点五公斤", "𠀀词未知词人工智能"};
	CHECK(scws_dict_open(path, &memory) == SCWS_OK);
	CHECK(scws_dict_open_ex(path, SCWS_DICT_MMAP, &mapped) == SCWS_OK);
	CHECK(scws_dict_open_ex(overlay, SCWS_DICT_MMAP, &extra) == SCWS_OK);
	CHECK(scws_new(memory, &a) == SCWS_OK && scws_new(mapped, &b) == SCWS_OK);
	for (int layer = 0; layer < 2; layer++) {
		if (layer) {
			CHECK(scws_add_dict(a, extra) == SCWS_OK);
			CHECK(scws_add_dict(b, extra) == SCWS_OK);
		}
		for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
			for (int mode = SCWS_STANDARD; mode <= SCWS_SEARCH; mode++) {
				CHECK(scws_segment(a, texts[i], strlen(texts[i]), (scws_mode) mode) == SCWS_OK);
				CHECK(scws_segment(b, texts[i], strlen(texts[i]), (scws_mode) mode) == SCWS_OK);
				CHECK(scws_count(a) == scws_count(b));
				for (size_t j = 0; j < scws_count(a); j++) {
					const scws_token *x = scws_next(a), *y = scws_next(b);
					CHECK(x && y && x->offset == y->offset && x->length == y->length
						&& x->char_offset == y->char_offset && x->char_length == y->char_length
						&& x->kind == y->kind && x->origin == y->origin && !strcmp(x->pos, y->pos));
				}
			}
		}
	}
	scws_free(a); scws_free(b);
	scws_dict_close(extra); scws_dict_close(mapped); scws_dict_close(memory);
	return 0;
}

static int replace_file(const char *path, const char *replacement)
{
	scws_dict *dicts[3] = {NULL, NULL, NULL};
	CHECK(scws_dict_open(path, &dicts[0]) == SCWS_OK);
	CHECK(scws_dict_open_ex(path, SCWS_DICT_MMAP, &dicts[1]) == SCWS_OK);
	CHECK(rename(replacement, path) == 0);
	CHECK(scws_dict_open_ex(path, SCWS_DICT_MMAP, &dicts[2]) == SCWS_OK);
	for (int i = 0; i < 3; i++) {
		scws *ctx = NULL;
		CHECK(scws_new(dicts[i], &ctx) == SCWS_OK);
		CHECK(scws_segment(ctx, "中文", 6, SCWS_STANDARD) == SCWS_OK);
		CHECK(scws_count(ctx) == (i == 2 ? 2u : 1u));
		scws_free(ctx);
		scws_dict_close(dicts[i]);
	}
	return 0;
}

#endif

int main(int argc, char **argv)
{
	scws_dict *dict = NULL;
	CHECK(argc >= 2);
	CHECK(scws_dict_open_ex(argv[1], (scws_dict_mode) 99, &dict) == SCWS_EINVAL && !dict);
	CHECK(!strcmp(scws_strerror(SCWS_ENOTSUP), "operation not supported"));
#ifndef SCWS_NO_MMAP
	if (argc == 4 && !strcmp(argv[2], "replace")) return replace_file(argv[1], argv[3]);
#endif
	/* Used by Python to check malformed files in either loading mode. */
	if (argc == 4) {
		scws_dict_mode mode = !strcmp(argv[2], "mmap") ? SCWS_DICT_MMAP : SCWS_DICT_MEMORY;
		scws_status expected = !strcmp(argv[3], "format") ? SCWS_EFORMAT : SCWS_EIO;
		CHECK(scws_dict_open_ex(argv[1], mode, &dict) == expected && !dict);
		return 0;
	}
	CHECK(argc == 3);
	CHECK(exercise(argv[1], SCWS_DICT_MEMORY) == 0);
#ifdef SCWS_NO_MMAP
	CHECK(scws_dict_open_ex(argv[1], SCWS_DICT_MMAP, &dict) == SCWS_ENOTSUP && !dict);
#else
	CHECK(exercise(argv[1], SCWS_DICT_MMAP) == 0);
	CHECK(compare_modes(argv[1], argv[2]) == 0);
#endif
	return 0;
}
