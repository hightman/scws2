AC_PREREQ([2.69])
AC_INIT([scws2], m4_esyscmd_s([cat VERSION]), [https://github.com/hightman/scws2/issues])
AC_CONFIG_SRCDIR([src/scws2.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AM_PROG_AR
LT_INIT
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdatomic.h>
#include <stddef.h>]], [[
_Static_assert(sizeof(int) >= 2, "C11 required");
atomic_size_t refs;
size_t expected = 1;
atomic_init(&refs, 1);
atomic_compare_exchange_weak_explicit(&refs, &expected, 2, memory_order_relaxed, memory_order_relaxed);
return atomic_fetch_sub_explicit(&refs, 1, memory_order_acq_rel) == 0;
]])], [], [AC_MSG_ERROR([A C11 compiler with size_t atomic operations is required])])
AC_ARG_ENABLE([mmap], AS_HELP_STRING([--disable-mmap], [always use private memory dictionaries]), [], [enable_mmap=yes])
AC_CHECK_HEADERS([sys/mman.h sys/file.h sys/stat.h fcntl.h unistd.h])
AC_CHECK_FUNCS([mmap munmap flock realpath mkstemp fdopen])
AS_IF([test "x$enable_mmap" != xno && test "x$ac_cv_header_sys_mman_h" = xyes && test "x$ac_cv_func_mmap" = xyes && test "x$ac_cv_func_munmap" = xyes], [
 AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/mman.h>]], [[void *p = mmap(0, 1, PROT_READ, MAP_SHARED, -1, 0); munmap(p, 1);]])],
  [AC_DEFINE([SCWS2_HAVE_MMAP], [1], [System supports read-only file mappings])])
])
AC_ARG_ENABLE([text-cache], AS_HELP_STRING([--disable-text-cache], [disable persistent text dictionary cache]), [], [enable_text_cache=yes])
AS_IF([test "x$enable_text_cache" != xno && test "x$ac_cv_func_flock" = xyes && test "x$ac_cv_func_realpath" = xyes && test "x$ac_cv_func_mkstemp" = xyes && test "x$ac_cv_func_fdopen" = xyes], [
 AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
]], [[struct stat s = {0};
int flags = O_NOFOLLOW | O_CLOEXEC | LOCK_EX;
#ifdef __APPLE__
long ns = s.st_mtimespec.tv_nsec + s.st_ctimespec.tv_nsec;
#else
long ns = s.st_mtim.tv_nsec + s.st_ctim.tv_nsec;
#endif
(void) flags; (void) ns;
]])], [AC_DEFINE([SCWS2_HAVE_TEXT_CACHE], [1], [System supports safe persistent text cache])])
])
AC_CONFIG_FILES([Makefile include/scws2_version.h scws2.pc])
AC_OUTPUT
