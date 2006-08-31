dnl hyperion-c.m4
dnl This file is copyright (C) 2001 Andrew Suffield
dnl                                  <asuffield@freenode.net>
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA

dnl Stolen from dpkg

dnl HYPERION_C_GCC_TRY_FLAGS(<warnings>,<cachevar>)
AC_DEFUN([HYPERION_C_GCC_TRY_FLAGS],[
 AC_MSG_CHECKING([GCC warning flag(s) $1])
 if test "${GCC-no}" = yes
 then
  AC_CACHE_VAL($2,[
   oldcflags="${CFLAGS-}"
   CFLAGS="${CFLAGS-} ${CWARNS} $1 -Werror"
   AC_TRY_COMPILE([
#include <string.h>
#include <stdio.h>
int main(void);
],[
    strcmp("a","b"); fprintf(stdout,"test ok\n");
], [$2=yes], [$2=no])
   CFLAGS="${oldcflags}"])
  if test "x$$2" = xyes; then
   CWARNS="${CWARNS}$1 "
   AC_MSG_RESULT(ok)
  else
   $2=''
   AC_MSG_RESULT(no)
  fi
 else
  AC_MSG_RESULT(no, not using GCC)
 fi
])

AC_DEFUN([HYPERION_CHECK_LONG_LONG],
[AC_MSG_CHECKING(for long long type)
oldcflags="${CFLAGS-}"
CFLAGS="${CFLAGS-} -Werror"
AC_TRY_COMPILE([
#include <stdio.h>
int main(void);
],[
long long int x;
x = 1;
x += 1;
printf("%lld",x);
],AC_DEFINE(HAVE_LONG_LONG,,[gcc extension type long long is available]) AC_MSG_RESULT(yes), AC_MSG_RESULT(no))
CFLAGS="${oldcflags}"
])

dnl We are only interested in __attribute__((format(printf,,))) if it can handle %zd
AC_DEFUN([HYPERION_CHECK_C_ATTRIBUTE_PRINTF_ZD],
[AC_MSG_CHECKING([for gcc __attribute__(()) format printf %zd support])
oldcflags="${CFLAGS-}"
CFLAGS="${CFLAGS-} -Werror"
AC_TRY_COMPILE([
extern  void foo(const char *, ...)
     __attribute__((format(printf,1,2)));
int main(void);
],[
foo("%zi",sizeof(int));
],AC_DEFINE(HAVE_ATTRIBUTE,,[gcc-style attribute declarations available]) AC_MSG_RESULT(yes), AC_MSG_RESULT(no))
CFLAGS="${oldcflags}"
])

dnl See if the named attribute flag is available and create a suitable #define if necessary
AC_DEFUN([HYPERION_CHECK_C_ATTRIBUTE],
[AC_MSG_CHECKING([for __attribute__(($1))])
oldcflags="${CFLAGS-}"
CFLAGS="${CFLAGS-} -Werror"
AC_TRY_COMPILE([
extern void *foo(void) __attribute__(($1));
int main(void);
],[
foo();
],
attribute_value="__attribute__(($1))"
AC_MSG_RESULT(yes),
attribute_value=""
AC_MSG_RESULT(no))
CFLAGS="${oldcflags}"
AC_DEFINE_UNQUOTED([$1_attribute], $attribute_value, [Support for __attribute__(($1))])
])

dnl Get a working va_copy() function, somehow
AC_DEFUN([HYPERION_VA_COPY],
[
AC_MSG_CHECKING([for va_copy])
AC_TRY_LINK(
[
#include <stdarg.h>
int main(void);
],
[
	va_list args, args2;
	va_copy(args2, args);
	return 0;
], AC_MSG_RESULT(found va_copy),AC_TRY_LINK(
[
#include <stdarg.h>
int main(void);
],
[
	va_list args, args2;
	__va_copy(args2, args);
	return 0;
], AC_DEFINE(va_copy,__va_copy,[va_copy macro]) AC_MSG_RESULT(found __va_copy),
AC_DEFINE(NEED_VA_COPY,1,[use local va_copy macro])
AC_MSG_RESULT(not found, using =)))
])
