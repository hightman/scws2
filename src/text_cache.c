#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
/* Optional POSIX disk cache for compiled text dictionaries. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "xdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SCWS2_HAVE_TEXT_CACHE) && !defined(SCWS2_NO_TEXT_CACHE)
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Bump when text parsing, POS mapping, scoring or emitted schema changes. */
#define TEXT_CACHE_VERSION 1
static uint64_t hash_bytes(const char *s)
{
	uint64_t h = UINT64_C(14695981039346656037);
	for (; *s; s++) { h ^= (unsigned char) *s; h *= UINT64_C(1099511628211); }
	return h;
}
static uint64_t stamp(const struct stat *st)
{
	char fields[256];
#ifdef __APPLE__
	long mns = st->st_mtimespec.tv_nsec, cns = st->st_ctimespec.tv_nsec;
#else
	long mns = st->st_mtim.tv_nsec, cns = st->st_ctim.tv_nsec;
#endif
	snprintf(fields, sizeof(fields), "%ju:%ju:%jd:%jd:%ld:%jd:%ld",
		(uintmax_t) st->st_dev, (uintmax_t) st->st_ino, (intmax_t) st->st_size,
		(intmax_t) st->st_mtime, mns, (intmax_t) st->st_ctime, cns);
	return hash_bytes(fields);
}
static char *cache_path(const char *source, const struct stat *st)
{
	const char *temporary = getenv("TMPDIR");
	char *directory, *path;
	struct stat info;
	size_t capacity;
	if (!temporary || !*temporary) temporary = "/tmp";
	capacity = strlen(temporary) + 192;
	directory = malloc(capacity); path = malloc(capacity);
	if (!directory || !path) { free(directory); free(path); return NULL; }
	snprintf(directory, capacity, "%s/scws2-text-%ju", temporary, (uintmax_t) geteuid());
	if ((mkdir(directory, 0700) && errno != EEXIST) || lstat(directory, &info)
		|| !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 077)) {
		free(directory); free(path); return NULL;
	}
	snprintf(path, capacity, "%s/v%d-%016" PRIx64 "-%zu-%016" PRIx64 ".xdb",
		directory, TEXT_CACHE_VERSION, hash_bytes(source), strlen(source), stamp(st));
	free(directory);
	return path;
}
static scws2_status_t open_cache(const char *path, scws2_dict_flags_t flags, scws2_dict_t **out)
{
	struct stat st;
	if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid()
		|| (st.st_mode & 077)) return SCWS2_EIO;
	return scws2_dict_open_ex(path, flags, out);
}
static int lock_cache(const char *path)
{
	size_t length = strlen(path);
	char *name = malloc(length + 6);
	struct stat st;
	int fd;
	if (!name) return -1;
	memcpy(name, path, length); memcpy(name + length, ".lock", 6);
	fd = open(name, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
	free(name);
	if (fd < 0) return -1;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) {
		close(fd); return -1;
	}
	while (flock(fd, LOCK_EX)) {
		if (errno != EINTR) { close(fd); return -1; }
	}
	return fd;
}
static int publish(const char *path, const unsigned char *data, uint32_t size)
{
	size_t length = strlen(path);
	char *name = malloc(length + 12);
	FILE *fp;
	int fd, failed;
	if (!name) return 0;
	memcpy(name, path, length); memcpy(name + length, ".tmp-XXXXXX", 12);
	fd = mkstemp(name);
	if (fd < 0) { free(name); return 0; }
	fp = fdopen(fd, "wb");
	if (!fp) { close(fd); unlink(name); free(name); return 0; }
	failed = fwrite(data, 1, size, fp) != size;
	if (fclose(fp)) failed = 1;
	if (!failed && rename(name, path)) failed = 1;
	if (failed) unlink(name);
	free(name);
	return !failed;
}
#endif

scws2_status_t scws2_text_open(const char *path, scws2_dict_flags_t flags, scws2_dict_t **out)
{
	unsigned char *data = NULL;
	uint32_t size = 0;
	scws2_status_t status;
	if (!path || !out || (flags & ~SCWS2_DICT_MEMORY))
		return SCWS2_EINVAL;
	*out = NULL;
#if defined(SCWS2_HAVE_TEXT_CACHE) && !defined(SCWS2_NO_TEXT_CACHE)
	char *source = realpath(path, NULL), *cache = NULL;
	struct stat before, after;
	int locked = -1;
	/* The source must still exist and be readable even on a cache hit. */
	if (source && !stat(source, &before) && S_ISREG(before.st_mode) && !access(source, R_OK))
		cache = cache_path(source, &before);
	if (cache) {
		status = open_cache(cache, flags, out);
		if (!status) { free(cache); free(source); return SCWS2_OK; }
		locked = lock_cache(cache);
		if (locked >= 0 && !open_cache(cache, flags, out)) {
			close(locked); free(cache); free(source); return SCWS2_OK;
		}
	}
	status = scws2_text_load(source ? source : path, &data, &size);
	if (!status && cache && (stat(source, &after) || stamp(&before) != stamp(&after))) {
		free(data); data = NULL; status = SCWS2_EIO;
	}
	if (!status && locked >= 0 && publish(cache, data, size)) {
		scws2_status_t loaded = open_cache(cache, flags, out);
		if (!loaded) {
			free(data); close(locked); free(cache); free(source); return SCWS2_OK;
		}
	}
	if (locked >= 0) close(locked);
	free(cache); free(source);
#else
	status = scws2_text_load(path, &data, &size);
#endif
	if (status) { free(data); return status; }
	return scws2_dict_from_data(data, size, out);
}
