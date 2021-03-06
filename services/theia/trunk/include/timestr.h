/*
 * timestr.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: timestr.h,v 1.1.1.1 2001/03/03 01:48:53 wcampbel Exp $
 */

#ifndef INCLUDED_timestr_h
#define INCLUDED_timestr_h

#ifndef INCLUDED_sys_time_h
#include <sys/time.h>        /* struct timeval */
#define INCLUDED_sys_time_h
#endif

/*
 * Prototypes
 */

char *timeago(time_t timestamp, int flag);
long timestr(char *format);
struct timeval *GetTime(struct timeval *timer);
long GetGMTOffset(time_t unixtime);

#endif /* INCLUDED_timestr_h */
