dnl hyperion-socket.m4
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

AC_DEFUN([HYPERION_CHECK_NBLOCK_SOCKET_POSIX],
[
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
void alarmed(int);
void alarmed(int signal)
{
	exit(1);
}
int main(void)
{
#if defined(O_NONBLOCK)
	char b[12], x[32];
	int f;
	socklen_t l = sizeof(x);
	f = socket(AF_INET, SOCK_DGRAM, 0);
	if (f >= 0 && !(fcntl(f, F_SETFL, O_NONBLOCK))) {
		signal(SIGALRM, alarmed);
		alarm(3);
		recvfrom(f, b, 12, 0, (struct sockaddr *)x, &l);
		alarm(0);
		exit(0);
	}
#endif
	exit(1);
}
], [$1],[$2],[$3])
])

AC_DEFUN([HYPERION_CHECK_NBLOCK_SOCKET_BSD],
[
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
void alarmed(int);
void alarmed(int signal)
{
	exit(1);
}
int main(void)
{
#if defined( O_NDELAY ) && !defined( NBLOCK_POSIX )
	char b[12], x[32];
	int f;
	socklen_t l = sizeof(x);
	f = socket(AF_INET, SOCK_DGRAM, 0);
	if (f >= 0 && !(fcntl(f, F_SETFL, O_NDELAY))) {
		signal(SIGALRM, alarmed);
		alarm(3);
		recvfrom(f, b, 12, 0, (struct sockaddr *)x, &l);
		alarm(0);
		exit(0);
	}
#endif
	exit(1);
}
], [$1],[$2],[$3])
])

AC_DEFUN([HYPERION_CHECK_NBLOCK_SOCKET_SYSV],
[
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
void alarmed(int);
void alarmed(int signal)
{
	exit(1);
}
int main(void)
{
#if !defined(NBLOCK_BSD) && !defined(NBLOCK_POSIX) && defined(FIONBIO)
	char b[12], x[32];
	int f;
	socklen_t l = sizeof(x);
	f = socket(AF_INET, SOCK_DGRAM, 0);
	if (f >= 0 && !(fcntl(f, F_SETFL, FIONBIO))) {
		signal(SIGALRM, alarmed);
		alarm(3);
		recvfrom(f, b, 12, 0, (struct sockaddr *)x, &l);
		alarm(0);
		exit(0);
	}
#endif /* !NBLOCK_POSIX && !NBLOCK_BSD && FIONBIO */
	exit(1);
}
], [$1],[$2],[$3])
])
