/*
 *  
 */

#ifndef SPRINTF_IRC
#define SPRINTF_IRC

#include <stdarg.h>

/*=============================================================================
 * Proto types
 */

extern int vsnprintf_irc(register char *str, int, register const char *format,
    register va_list);
extern int ircsnprintf(register char *str, int, register const char *format, ...);

#endif /* SPRINTF_IRC */
