#include <stdio.h>
#include "debug.h"

void
__debug_printf (const char *fmt, ...)
{
	va_list args;

	fputs("[D]: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	fflush(stderr);
}

void
__debug_print (const char *str)
{
	char *p;

	if (str && (p = strrchr(str, '\n')) != NULL && *(p + 1) == '\0')
		*p = '\0';
	fprintf(stderr, "[D]: %s\n", str);
	fflush(stderr);
}

/*
 * vim: ts=8 sw=8 noet fdm=marker tw=80
 */
