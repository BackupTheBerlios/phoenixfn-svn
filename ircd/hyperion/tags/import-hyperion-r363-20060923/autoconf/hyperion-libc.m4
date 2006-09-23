dnl hyperion-libc.m4
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

dnl Does the libc support %zd?
AC_DEFUN([HYPERION_CHECK_C_PRINTF_ZD],
[AC_MSG_CHECKING([for libc printf %zd conversion])
AC_TRY_RUN([
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
  char buf[64]; /* Should be enough for any current systems */

  /* First test: zero */
  snprintf(buf, 64, "%zd", 0);
  /* Must be "0" */
  if (strcmp(buf, "0") != 0)
    exit(1);

  /* Second test: malloc */
  snprintf(buf, 64, "%zd", malloc(1));
  /* Must not be equal to "zd" */
  if (strcmp(buf, "zd") == 0)
    exit(2);

  /* This handles all currently known systems, but may need
   *  extending later to handle more braindamaged code
   */
  exit(0);
}
],AC_DEFINE(HAVE_PRINTF_ZD,,[%zd printf conversion available]) AC_MSG_RESULT(yes), AC_MSG_RESULT(no),
AC_MSG_RESULT(cross-compiling, assuming no))
])
