dnl Process this file with autoconf to produce a configure script.
AC_INIT(source/hybserv.c)

AC_CONFIG_AUX_DIR(autoconf)
AC_CONFIG_HEADER(include/defs.h)

CFLAGS=${CFLAGS:-"-g3 -O0 -Wall -W -Wcast-qual -pedantic -Wno-unused"}
CFLAGS="-std=c99 -D_GNU_SOURCE $CFLAGS"

dnl Set language and save it
AC_LANG_C
AC_LANG_SAVE

dnl Get system type
AC_CANONICAL_HOST
MYHOST=$host_os
case "$host_os" in
*sunos*)
  _SunOS=1
  ;;
*solaris*)
  _SunOS=1
  ;;
*cygwin)
  AC_DEFINE(HAVE_CYGWIN, 1, [Cygwin platform])
esac

dnl Package information
PACKAGE=dancer-services

dnl Standard installation path
AC_PREFIX_DEFAULT(/usr/local)

dnl Checks for programs.
AC_PROG_CC
AC_PATH_PROG(RM, rm)
AC_PATH_PROG(CP, cp)
AC_PATH_PROG(LN, ln)
AC_PATH_PROG(LS, ls)
AC_PATH_PROG(CHMOD, chmod)
AC_PATH_PROG(CHOWN, chown)
AC_PATH_PROG(PAX, pax)

dnl Check for a suitable install program
AC_PROG_INSTALL

WHOAMI=`whoami`
AC_SUBST(WHOAMI)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(sys/time.h)
AC_CHECK_HEADER(stdarg.h,, AC_MSG_ERROR(** You must have stdarg.h in order to compile HybServ2 **))
AC_HEADER_TIME

dnl Add standard prefix to fix broken environments
LIBS="${LIBS} -L/usr/local/lib"
CFLAGS="${CFLAGS} -I/usr/local/include"

dnl Checks for library functions.
AC_TYPE_SIGNAL
AC_CHECK_FUNCS(strdup strerror strncpy)
AC_CHECK_FUNC(gettimeofday, AC_DEFINE(HAVE_GETTIMEOFDAY, 1, [gettimeofday is present]))
AC_CHECK_FUNCS(gethostbyname, , [AC_CHECK_LIB(nsl, gethostbyname), , LIBS="$LIBS -lnsl"])
AC_CHECK_LIB(socket, socket, LIBS="$LIBS -lsocket")
dnl AC_CHECK_LIB(crypt, crypt, LIBS="$LIBS -lcrypt")

AC_C_BIGENDIAN

dnl Do not compile with pthread support if they give a
dnl --disable-threads or --enable-threads=no to configure
AC_ARG_ENABLE(threads, [  --enable-threads        Enable thread support [default=no]])

dnl We will use Solaris native threads if Solaris is present
if test "$enable_threads" = "yes"; then
  AC_CHECK_LIB(thread, thr_create,
    AC_DEFINE(HAVE_SOLARIS_THREADS, 1, [Using Solaris threads])
    CFLAGS="${CFLAGS} -D_REENTRANT"
    _have_thread=yes
    LIBP="-lthread",
    AC_MSG_WARN(No native Solaris threads - will try pthreads)
      AC_CHECK_LIB(pthread, pthread_create,
      AC_DEFINE(HAVE_PTHREADS, 1, [Using pthreads])
      _have_pthread=yes
      if test -n "$_SunOS"; then
        CFLAGS="${CFLAGS} -D_POSIX_PTHREAD_SEMANTICS"
      else
        CFLAGS="${CFLAGS} -D_REENTRANT"
      fi
      LIBP="-lpthread",
       AC_MSG_WARN(No libpth detected - trying libc_r)
       AC_CHECK_LIB(c_r, pthread_create,
         AC_DEFINE(HAVE_PTHREADS, 1, [Using pthreads])
         _have_pthread=yes
         CFLAGS="${CFLAGS} -D_REENTRANT"
         LIBP="-lc_r",
          AC_MSG_WARN(Compiling without multithread support))))
fi
AC_SUBST(LIBP)

dnl Use sed to grab the version from Makefile.in
VERSION=`sed -n -e 's/VERSION_MAJOR = *\(.*\)$/\1/p' Makefile.in`

dnl And subst-it.
AC_SUBST(VERSION)

AC_OUTPUT(              \
Makefile                \
source/Makefile         \
tools/Makefile          \
bin/settings.conf       \
bin/cleandb             \
include/config.h        \
)

echo "

Configuration:

  Detected OS:      ${host_os}
  Services version: ${VERSION}
  Install path:     ${prefix}/dancer-services
  Compiler:         ${CC}
  Compiler flags:   ${CFLAGS}
  Linker flags:     ${LIBS}"

if test -n "${LIBP}"; then
  echo "  Thread support:   enabled"
  echo "  Thread library:   ${LIBP}"
else
  echo "  Thread support:   disabled"
fi

echo
echo Please edit include/config.h to change the settings to match
echo your network before running make
echo
