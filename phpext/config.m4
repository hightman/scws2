PHP_ARG_ENABLE(scws2, whether to enable scws2,
  [  --enable-scws2         Enable the scws2 UTF-8 segmenter], no)

if test "$PHP_SCWS2" != "no"; then
  SCWS2_ROOT=`cd "$srcdir/.." && pwd`
  if test ! -f "$SCWS2_ROOT/include/scws2.h" || test ! -f "$SCWS2_ROOT/src/rules.ini"; then
    AC_MSG_ERROR([Build phpext from the complete scws2 repository])
  fi
  AC_PATH_PROG([SCWS2_PYTHON], [python3], [no])
  if test "$SCWS2_PYTHON" = "no"; then
    AC_MSG_ERROR([Python 3.9+ is required to generate the embedded rule tables])
  fi
  "$SCWS2_PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || AC_MSG_ERROR([Python 3.9+ is required])
  AC_MSG_CHECKING([for PHP 7.0 or newer])
  SCWS2_PHP_VERSION=`$PHP_CONFIG --vernum`
  if test "$SCWS2_PHP_VERSION" -lt 70000; then
    AC_MSG_ERROR([scws2 requires PHP 7.0 or newer])
  fi
  AC_MSG_RESULT([yes])
  "$SCWS2_PYTHON" -B "$SCWS2_ROOT/tools/gen_rules.py" --output rules_data.h || AC_MSG_ERROR([Cannot generate rule tables])
  PHP_ADD_INCLUDE([$SCWS2_ROOT/include])
  PHP_ADD_INCLUDE([$SCWS2_ROOT/src])
  PHP_ADD_INCLUDE([$abs_builddir])
  PHP_NEW_EXTENSION(scws2, scws2.c engine/scws2.c engine/utf8.c engine/xdb.c engine/text.c engine/text_cache.c engine/rules.c,
    $ext_shared,, [-std=c11 -Wall -Wextra -fvisibility=hidden])
  PHP_ADD_BUILD_DIR([$ext_builddir/engine])
  PHP_SUBST(SCWS2_ROOT)
  PHP_SUBST(SCWS2_PYTHON)
  PHP_ADD_MAKEFILE_FRAGMENT
fi
