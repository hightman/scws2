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
int main(int argc, char **argv)
{
	scws_dict *dict = NULL, *invalid = NULL;
	scws *ctx = NULL, *second = NULL;
	const scws_token *token;
	const char embedded[] = "中文\0测试";
	const char invalid_utf8[] = {'x', (char) 0xed, (char) 0xa0, (char) 0x80};
	pthread_t threads[4];
	task jobs[4];
	CHECK(argc == 2);
	CHECK(scws_dict_open(NULL, &invalid) == SCWS_EINVAL && invalid == NULL);
	CHECK(scws_dict_open(argv[1], NULL) == SCWS_EINVAL);
	CHECK(scws_dict_open(argv[1], &dict) == SCWS_OK);
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
