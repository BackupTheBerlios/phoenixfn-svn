/*
 * $Id: sprintf_irc.h,v 1.2 2001/06/16 09:27:52 kreator Exp $ 
 */

#ifndef SPRINTF_IRC
#define SPRINTF_IRC

#include <stdarg.h>

/* Prototypes */

extern int vsprintf_irc(register char *str, register const char *format,
    va_list);
extern int ircsprintf(register char *str, register const char *format, ...);

#endif /* SPRINTF_IRC */
