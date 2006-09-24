#ifndef THEIA_COMMON_H
# define THEIA_COMMON_H

# include "debug.h"

/*
 * Solaris does not provide this by default. Anyway this is wrong approach,
 * since -1 is 255.255.255.255 addres which is _valid_! Obviously
 * inet_aton() should be used instead. I'll fix that later. -kre
 */
# ifndef INADDR_NONE
#  define INADDR_NONE ((unsigned long)-1)
# endif

# ifdef HAVE_SOLARIS_THREADS
#  include <thread.h>
#  include <synch.h>
# else
#  ifdef HAVE_PTHREADS
#   include <pthread.h>
#  endif
# endif

#endif /* !THEIA_COMMON_H */

/*
 * vim: ts=8 sw=8 noet fdm=marker tw=80
 */
