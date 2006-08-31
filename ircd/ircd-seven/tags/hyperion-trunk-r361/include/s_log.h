/* - Internet Relay Chat, include/s_log.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef INCLUDED_s_log_h
#define INCLUDED_s_log_h

#include "config.h"

#ifdef MALLOC_LOG
#define malloc_log _malloc_log
#define expect_malloc expecting_malloc++
extern void _malloc_log(const char *, ...)
     printf_attribute(1, 2);
extern int expecting_malloc;
#else
#define malloc_log(...)
#define expect_malloc
#endif

#define L_CRIT    0
#define L_ERROR   1
#define L_WARN    2
#define L_NOTICE  3
#define L_TRACE   4
#define L_INFO    5
#define L_DEBUG   6

#ifdef MALLOC_LOG
extern void init_log(const char *);
#else
extern void init_log(void);
#endif
extern void reopen_log(void);
extern void close_log(void);
extern void set_log_level(int level);
extern int  get_log_level(void);
extern void logprintf(int priority, const char* fmt, ...)
     printf_attribute(2,3);
extern const char *get_log_level_as_string(int level);

#endif /* INCLUDED_s_log_h */
