#ifndef THEIA_DEBUG_H
# define THEIA_DEBUG_H

# include <stdarg.h>

# ifdef DEBUGMODE
#  define debug_printf(...)
#  define debug_print(...)
# else
#  define debug_printf(...)	__debug_printf(__VA_ARGS__)
#  define debug_print(...)	__debug_print(__VA_ARGS__)
# endif

extern void __debug_printf (const char *fmt, ...);
extern void __debug_print (const char *str);

#endif /* !THEIA_DEBUG_H */

/*
 * vim: ts=8 sw=8 noet fdm=marker tw=80
 */
