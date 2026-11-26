PHP_ARG_WITH(scws2, installed scws2 library prefix,
  [  --with-scws2=DIR        Link the installed shared libscws2 in DIR], no)

if test "$PHP_SCWS2" != "no"; then
  if test "$PHP_SCWS2" = "yes"; then
    AC_MSG_ERROR([Specify --with-scws2=/absolute/installation/prefix])
  fi
  SCWS2_PREFIX="$PHP_SCWS2"
  if test ! -f "$SCWS2_PREFIX/include/scws2.h" || test ! -f "$SCWS2_PREFIX/include/scws2_version.h"; then
    AC_MSG_ERROR([Installed scws2 headers not found in $SCWS2_PREFIX/include])
  fi
  if test ! -f "$SCWS2_PREFIX/lib/libscws2.so" && test ! -f "$SCWS2_PREFIX/lib/libscws2.dylib"; then
    AC_MSG_ERROR([Shared libscws2 not found in $SCWS2_PREFIX/lib])
  fi
  AC_MSG_CHECKING([for PHP 7.0 or newer])
  SCWS2_PHP_VERSION=`$PHP_CONFIG --vernum`
  if test "$SCWS2_PHP_VERSION" -lt 70000; then AC_MSG_ERROR([scws2 requires PHP 7.0+]); fi
  AC_MSG_RESULT([yes])
  PHP_ADD_INCLUDE([$SCWS2_PREFIX/include])
  PHP_CHECK_LIBRARY(scws2, scws2_new, [], [AC_MSG_ERROR([Cannot link the installed libscws2])], [-L$SCWS2_PREFIX/lib])
  PHP_ADD_LIBRARY_WITH_PATH(scws2, [$SCWS2_PREFIX/lib], SCWS2_SHARED_LIBADD)
  PHP_SUBST(SCWS2_SHARED_LIBADD)
  PHP_NEW_EXTENSION(scws2, scws2.c, $ext_shared,, [-std=c11 -Wall -Wextra])
fi
