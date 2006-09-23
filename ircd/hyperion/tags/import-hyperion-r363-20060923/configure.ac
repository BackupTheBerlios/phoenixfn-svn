dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)

dnl OK, autoconf stuff first
AC_INIT(hyperion, [1.0.2], bugs@freenode.net)
AC_COPYRIGHT([Copyright Andrew Suffield <asuffield@freenode.net>
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

See README.license for more details])

dnl Define where to find the input files

dnl This one should be pretty unique (the files in main/ are fairly generic)
AC_CONFIG_SRCDIR(include/config.h.fn)
AC_CONFIG_AUX_DIR(build-misc)
AC_CONFIG_MACRO_DIR(autoconf)

dnl Find the canonical names right away
AC_CANONICAL_HOST

dnl Now the automake init bit
AM_INIT_AUTOMAKE(1.8)
AM_MAINTAINER_MODE

AC_CONFIG_HEADERS(include/setup.h)

orig_CFLAGS="$CFLAGS"

dnl Checks for programs.

AC_PROG_CC
AC_ISC_POSIX
AC_PROG_INSTALL

debug_syms=""
AC_ARG_ENABLE(debug-syms,
[  --disable-debug-syms    Do not include debug symbols in executables ],
[if test "$enableval" = "yes"
then
	debug_syms='-g3 '
fi], debug_syms='-g3 ')

AC_ARG_ENABLE(tc-syms,
[  --enable-tc-syms        Enable test coverage data ],
[if test "$enableval" = "yes"
then
	debug_syms="${debug_syms}-ftest-coverage -fprofile-arcs "
fi])

AC_ARG_ENABLE(profile,
[  --enable-profile        Enable profiling data ],
[if test "$enableval" = "yes"
then
	debug_syms="${debug_syms}-pg "
fi])

AC_MSG_CHECKING(for suitable optimisation flags)
AC_ARG_ENABLE(optimise,
[  --enable-optimise       Enable optimisation ],
[if test "$enableval" = "yes"
then
	if test "$GCC" = "yes"
	then
		optimise='-Os '
	else
		optimise='-O '
	fi
else
	if test "$GCC" = "yes"
	then
		optimise='-O0 '
	else
		optimise=''
	fi
fi],[
	if test -f "$srcdir/development"
	then
		if test "$GCC" = "yes"
		then
			optimise='-O0 '
		else
			optimise=''
		fi
	else
		if test "$GCC" = "yes"
		then
			optimise='-O2 '
		else
			optimise='-O '
		fi
	fi
])
if test -z "${optimise}"
then
	AC_MSG_RESULT(none)
else
	AC_MSG_RESULT(${optimise})
fi

# Have to run this one after PROG_CC, because it uses CHECK_LIB and we
# want to pass arguments to PROG_CC
AC_ARG_ENABLE(profile-libc,
[  --enable-profile-libc   Try to link with profiling libc ],
[if test "$enableval" = "yes"
then
	AC_CHECK_LIB(c_p, open)
fi])

dnl This is the list of all system headers that are included in hyperion. If libc can
dnl  compile with all these included and errors on, then it is considered to be safe
dnl  to use errors
AC_MSG_CHECKING([for system headers used])
hyperion_headers_used=`(find ${srcdir}/src -name '*.c'; find ${srcdir}/include -name '*.h') | xargs grep -h '^\# *include <' | sed 's/>.*/>/' | sed 's/\# */\#/' | sed 's/include *</include </' | sort | uniq`
count_hyperion_headers_used=`printf "$hyperion_headers_used" | wc -l | sed -e 's/^ *//'`
AC_MSG_RESULT([found $count_hyperion_headers_used])

dnl OK, get the simple list to check for existance
hyperion_headers_list=`printf "$hyperion_headers_used" | sed 's/\#include <\(.*\)>/\1/'`
AC_CHECK_HEADERS($hyperion_headers_list)

dnl Now compose the working list of guarded headers...
hyperion_headers_guarded=`printf "$hyperion_headers_list\n" | sed "h;y%abcdefghijklmnopqrstuvwxyz./-%ABCDEFGHIJKLMNOPQRSTUVWXYZ___%;s/\(.*\)/\#ifdef HAVE_\1/;x;s/\(.*\)/\#include <\1>/;x;G;a\\\\
\#endif
"`

dnl Checks for libraries.
AC_ARG_WITH(zlib, [  --without-zlib          Do not link with zlib for ziplinks ])
if test "$with_zlib" != "no"
then
	AC_CHECK_LIB(z, inflate)
fi

dnl Replace `main' with a function in -lnsl:
AC_CHECK_LIB(nsl, gethostbyname)
AC_CHECK_FUNC(res_mkquery,, AC_CHECK_LIB(resolv, res_mkquery,, AC_CHECK_LIB(resolv, __res_mkquery)))
dnl Pop quiz: what systems is this used on? Does hyperion build on any of them?
AC_CHECK_LIB(socket, socket)

dnl Checks for header files.
AC_HEADER_STDC

dnl Check for stdarg.h - if we can't find it, halt configure
AC_CHECK_HEADER(stdarg.h,, AC_MSG_ERROR(stdarg.h could not be found))

dnl Checks for library functions.
AC_PROG_GCC_TRADITIONAL

dnl check for poll() call
AC_CHECK_FUNC(poll, AC_DEFINE(USE_POLL,,[poll() system call is available]),)

dnl check for u_int32_t
AC_CHECK_TYPE(u_int32_t, unsigned long)

AC_C_BIGENDIAN
HYPERION_CHECK_C_PRINTF_ZD
HYPERION_VA_COPY

dnl This should be a little easier to grok than the prior mess
dnl But only a little
dnl Basically, try POSIX, then BSD, then SysV
AC_MSG_CHECKING(for non-blocking socket implementation)
HYPERION_CHECK_NBLOCK_SOCKET_POSIX(
nblock='POSIX'
AC_DEFINE(NBLOCK_POSIX,,[Using POSIX non-blocking sockets (O_NONBLOCK)])
AC_MSG_RESULT(O_NONBLOCK),
nblock='UNKNOWN',
nblock='CROSS')

if test "$nblock" = "UNKNOWN"
then
dnl Not POSIX...
	HYPERION_CHECK_NBLOCK_SOCKET_BSD(
	nblock='BSD'
	AC_DEFINE(NBLOCK_BSD,,[Using BSD non-blocking sockets (O_NDELAY)])
	AC_MSG_RESULT(O_NDELAY),
	nblock='UNKNOWN',
	nblock='CROSS')
fi

if test "$nblock" = "UNKNOWN"
then
dnl Not BSD either...
	HYPERION_CHECK_NBLOCK_SOCKET_SYSV(
	nblock='SYSV'
	AC_DEFINE(NBLOCK_SYSV,,[Using SysV non-blocking sockets (FIONBIO)])
	AC_MSG_RESULT(FIONBIO),
	nblock='UNKNOWN',
	nblock='CROSS')
fi

case "$nblock" in
	UNKNOWN)	AC_MSG_ERROR("could not find a non-blocking socket implementation") ;;
	CROSS)		AC_MSG_ERROR("cross-compiling") ;;
esac

AC_FUNC_MMAP

dnl OK, efence?
AC_ARG_WITH(efence, [  --with-efence           Link with electric fence ])
if test "$with_efence" = "yes"
then
	AC_CHECK_LIB(efence, EF_ALIGNMENT, LIBS="${LIBS} -lefence", AC_MSG_ERROR(libefence not found))
fi

dnl See what warnings we can get away with
HYPERION_C_GCC_TRY_FLAGS([-Wall], hyperion_cv_c_gcc_w_all)
HYPERION_C_GCC_TRY_FLAGS([-Wwrite-strings], hyperion_cv_c_gcc_w_strings)
HYPERION_C_GCC_TRY_FLAGS([-Wpointer-arith], hyperion_cv_c_gcc_w_pointer_arith)
HYPERION_C_GCC_TRY_FLAGS([-Wcast-qual], hyperion_cv_c_gcc_w_cast_qual)
HYPERION_C_GCC_TRY_FLAGS([-Wimplicit -Wnested-externs], hyperion_cv_c_gcc_w_implicit)
HYPERION_C_GCC_TRY_FLAGS([-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations], hyperion_cv_c_gcc_prototypes)
HYPERION_C_GCC_TRY_FLAGS([-W -Wno-unused], hyperion_cv_c_gcc_w)
dnl For a day when I'm very very bored...
dnl HYPERION_C_GCC_TRY_FLAGS([-Wshadow], hyperion_cv_c_gcc_w_shadow)
dnl HYPERION_C_GCC_TRY_FLAGS([-Wconversion], hyperion_cv_c_gcc_w_conversion)
HYPERION_C_GCC_TRY_FLAGS([-Wmissing-noreturn], hyperion_cv_c_gcc_w_missing_noreturn)
HYPERION_C_GCC_TRY_FLAGS([-Wundef], hyperion_cv_c_gcc_w_undef)
HYPERION_C_GCC_TRY_FLAGS([-Wpacked], hyperion_cv_c_gcc_w_packed)
HYPERION_C_GCC_TRY_FLAGS([-Wnested-externs], hyperion_cv_c_gcc_w_nested_externs)
dnl Not yet good enough to be useful (gcc 3.1.0)
dnl HYPERION_C_GCC_TRY_FLAGS([-Wunreachable-code], hyperion_cv_c_gcc_w_unreachable_code)
HYPERION_C_GCC_TRY_FLAGS([-Wbad-function-cast], hyperion_cv_c_gcc_w_bad_function_cast)
HYPERION_C_GCC_TRY_FLAGS([-Wunused-function -Wunused-label -Wunused-value -Wunused-variable], hyperion_cv_c_gcc_w_unused)
HYPERION_C_GCC_TRY_FLAGS([-Wredundant-decls], hyperion_cv_c_gcc_w_redundant_decls)
HYPERION_C_GCC_TRY_FLAGS([-Wfloat-equal], hyperion_cv_c_gcc_w_float_equal)
HYPERION_C_GCC_TRY_FLAGS([-std=gnu9x], hyperion_cv_c_gcc_std_gnu9x)
ac_save_CFLAGS="${debug_syms}${optimise}${CWARNS}${orig_CFLAGS}"
CFLAGS=${ac_save_CFLAGS}

AC_MSG_CHECKING([for pedantry])
warn='no'
AC_ARG_ENABLE(pedantic,
[  --disable-pedantic=type Do not compile with -pedantic (for those with broken libc)
                          If present, type can be "warn" or "err" ],
[case "$enableval" in
	yes)	pedantic='-pedantic-errors ' ;;
	no)	pedantic='' ;;
	warn*)	pedantic='-pedantic ' ;;
	err*)	pedantic='-pedantic-errors ' ;;
esac], 
dnl OK, can we compile with -pedantic-errors, or is libc broken?
dnl hyperion_save_CFLAGS="$CFLAGS"
dnl CFLAGS="$CFLAGS -pedantic-errors"
dnl AC_TRY_COMPILE([
dnl $hyperion_headers_guarded
dnl int main(void);
dnl ],[
dnl   return 0;
dnl ], pedantic='-pedantic-errors ',
dnl warn='yes'
dnl pedantic='-pedantic ')
dnl CFLAGS="$hyperion_save_CFLAGS $pedantic")
# disable compiling with -pedantic-errors by default, not worth
# the support hassle -- jilles
pedantic='-pedantic ')
if test -z "$pedantic"
then
	AC_MSG_RESULT(none)
else
	AC_MSG_RESULT($pedantic)
fi
if test "$warn" = "yes"
then
	AC_MSG_WARN([Can't use -pedantic-errors, is libc buggy?])
fi

ac_save_CFLAGS="${debug_syms}${optimise}${pedantic}${CWARNS}${orig_CFLAGS}"
CFLAGS=${ac_save_CFLAGS}

AC_MSG_CHECKING([-Werror])
warn='no'
AC_ARG_ENABLE(errors,
[  --disable-errors        Do not compile with -Werror (for those with broken headers) ],
[case "$enableval" in
	yes)	errors='-Werror ' ;;
	no)	errors='' ;;
esac],
dnl <sigh> Can we compile with -Werror, or is libc broken?
dnl hyperion_save_CFLAGS="$CFLAGS"
dnl CFLAGS="$CFLAGS -Werror"
dnl AC_TRY_COMPILE([
dnl $hyperion_headers_guarded
dnl int main(void);
dnl ],[
dnl   return 0;
dnl ],errors='-Werror ',
dnl warn='yes'
dnl errors='')
dnl CFLAGS="$hyperion_save_CFLAGS")
# disable compiling with -Werrors by default, not worth
# the support hassle -- jilles
errors='')
if test -z "$errors"
then
	AC_MSG_RESULT([not used])
else
	AC_MSG_RESULT([used])
fi
if test "$warn" = "yes"
then
	AC_MSG_WARN([Can't use -Werror, is libc buggy?])
fi

ac_save_CFLAGS="${debug_syms}${optimise}${pedantic}${CWARNS}${errors}${orig_CFLAGS}"
CFLAGS=${ac_save_CFLAGS}

dnl Check this _after_ warning flags are determined
HYPERION_CHECK_LONG_LONG
HYPERION_CHECK_C_ATTRIBUTE_PRINTF_ZD
HYPERION_CHECK_C_ATTRIBUTE(malloc)
HYPERION_CHECK_C_ATTRIBUTE(pure)
HYPERION_CHECK_C_ATTRIBUTE(noreturn)

dnl These bits are used to store build-time information in version.c for
dnl  display in m_info()
BUILD_UNAME=`uname -a`
BUILD_TIME=`date -u`
AC_DEFINE_UNQUOTED(BUILD_UNAME, "$BUILD_UNAME", [uname of build system])
AC_DEFINE_UNQUOTED(BUILD_TIME, "$BUILD_TIME", [./configure run time])

AC_ARG_WITH(serno,
[  --with-serno="..."      Add a build description (shows in revision field) ],
SERIAL_ADDENDUM="_$withval",
SERIAL_ADDENDUM="")
AC_DEFINE_UNQUOTED(SERIAL_ADDENDUM, "$SERIAL_ADDENDUM", [Description of build])

AC_ARG_WITH(config,
[  --with-config=type      Select a default configuration to use
                          Some configurations:
                           dist - default distribution
                           fn  - as used on the Freenode Network
                           tn  - freenode testnet
                          For full list: ls include|sed -ne 's/^config.h.//p'],
[case "$withval" in
	# allow these alternate names for now -- jilles
	freenode-large)			config="config.h.fn-large" ;;
	freenode)			config="config.h.fn" ;;
	freenode-small)			config="config.h.fn-small" ;;
	opn)				config="config.h.fn" ;;
	opn-small)			config="config.h.fn-small" ;;
	testnet-small)			config="config.h.tn-small" ;;
	testnet)			config="config.h.tn" ;;
	testnet-large)			config="config.h.tn-large" ;;
	*)				config="config.h.$withval" ;;
esac
cp "include/$config" include/config.h || AC_MSG_ERROR(invalid config)],
[test -f include/config.h || cp include/config.h.dist include/config.h]
)

AC_ARG_ENABLE(documentation,
[  --enable-documentation  Build the documentation ],
[case "$enableval" in
	yes)	documentation="yes" ;;
	no)	documentation="no" ;;
esac],
[documentation="no"])

AM_CONDITIONAL(DOCUMENTATION, test "$documentation" = "yes")

AC_CONFIG_FILES(
Makefile
autoconf/Makefile
build-misc/Makefile
doc/Makefile
doc/sgml/Makefile
doc/sgml/dancer-oper-guide/Makefile
doc/sgml/dancer-user-guide/Makefile
include/Makefile
lib/Dancer/Makefile
lib/Dancer/Proxy/Makefile
lib/Makefile
src/Makefile
tools/Makefile
patches/Makefile
)

AC_OUTPUT

echo "
Configuration:

  Detected OS:      ${host_os}
  Hyperion version: ${VERSION}
  Install prefix:   ${prefix}
  CFLAGS:           ${CFLAGS}
"
