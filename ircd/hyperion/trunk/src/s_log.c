/************************************************************************
 *   IRC - Internet Relay Chat, src/s_log.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers. 
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
#include "s_log.h"
#include "irc_string.h"
#include "ircd.h"
#include "s_misc.h"
#include "paths.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


#define LOG_BUFSIZE 2000 

#ifdef USE_LOGFILE
static int logFile = -1;
#endif /* USE_LOGFILE */

#ifdef MALLOC_LOG
static int malloc_log_fd = -1;
int expecting_malloc = 0;
#endif

static int logLevel = INIT_LOG_LEVEL;

#ifdef USE_SYSLOG
static int sysLogLevel[] = {
  LOG_CRIT,
  LOG_ERR,
  LOG_WARNING,
  LOG_NOTICE,
  LOG_INFO,
  LOG_INFO,
  LOG_INFO
};
#endif /* USE_SYSLOG */

static const char *logLevelToString[] =
{ "L_CRIT",
  "L_ERROR",
  "L_WARN",
  "L_NOTICE",
  "L_TRACE",
  "L_INFO",
  "L_DEBUG"
};

/*
 * open_log - open ircd logging file
 * returns true (1) if successful, false (0) otherwise
 */
#if defined(USE_LOGFILE) 
static int
open_log(const char* filename)
{
  logFile = open(filename, 
                 O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, 0640);
  if (-1 == logFile)
    {
#ifdef USE_SYSLOG
      syslog(LOG_ERR, "Unable to open log file: %s: %s",
	     filename, strerror(errno));
#endif
      return 0;
    }
  return 1;
}
#endif

#ifdef MALLOC_LOG
static int
open_malloc_log(const char *malloc_log_filename)
{
  malloc_log_fd = open(malloc_log_filename, O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, 0640);
  if (-1 == malloc_log_fd)
    {
      logprintf(LOG_ERR, "Unable to open malloc log file: %s: %s",
	  malloc_log_filename, strerror(errno));
      return 0;
    }
  write(malloc_log_fd, "*** opening malloc log\n", strlen("*** opening malloc log\n"));
  return 1;
}
#endif

void
close_log(void)
{
#if defined(USE_LOGFILE) 
  if (-1 < logFile)
    {
      close(logFile);
      logFile = -1;
    }
#endif
#ifdef MALLOC_LOG
  if (-1 < malloc_log_fd)
    {
      close(malloc_log_fd);
      malloc_log_fd = -1;
    }
#endif
#ifdef USE_SYSLOG  
  closelog();
#endif
}

#if defined(USE_LOGFILE) 
static void
write_log(const char* message)
{
  char buf[LOG_BUFSIZE];
  sprintf(buf, "[%s] %s\n", smalldate(CurrentTime), message);
  write(logFile, buf, strlen(buf));
}
#endif
   
void
reopen_log(void)
{
#if defined(USE_LOGFILE) 
  if (-1 < logFile) {
    close(logFile);
    logFile = -1;
  }
#endif
#ifdef USE_SYSLOG  
  closelog();
#endif
#if defined(USE_LOGFILE) 
  open_log(main_log_file);
#endif
#ifdef USE_SYSLOG
  openlog("ircd", LOG_PID | LOG_NDELAY, LOG_FACILITY);
#endif
}

#ifdef MALLOC_LOG
void
_malloc_log(const char *fmt, ...)
{
  char    buf[LOG_BUFSIZE];
  va_list args;
  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);
  write(malloc_log_fd, buf, strlen(buf));
  write(malloc_log_fd, "\n", 1);
}
#endif

void
logprintf(int priority, const char* fmt, ...)
{
  char    buf[LOG_BUFSIZE];
  va_list args;
  assert(-1 < priority);
  assert(0 != fmt);

  if (priority > logLevel)
    return;

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

#ifdef USE_SYSLOG  
  if (priority <= L_DEBUG)
    syslog(sysLogLevel[priority], "%s", buf);
#endif
#if defined(USE_LOGFILE) 
  write_log(buf);
#endif
}

#ifdef MALLOC_LOG
void init_log(const char *malloc_log_filename)
#else
void
init_log(void)
#endif
{
#if defined(USE_LOGFILE) 
  open_log(main_log_file);
#endif
#ifdef USE_SYSLOG
  openlog("ircd", LOG_PID | LOG_NDELAY, LOG_FACILITY);
#endif
#ifdef MALLOC_LOG
  open_malloc_log(malloc_log_filename);
#endif
}

void
set_log_level(int level)
{
  if (L_ERROR < level && level <= L_DEBUG)
    logLevel = level;
}

int
get_log_level(void)
{
  return( logLevel );
}

const char *
get_log_level_as_string(int level)
{
  if(level > L_DEBUG)
    level = L_DEBUG;
  else if(level < L_ERROR)
    level = L_ERROR;

  return(logLevelToString[level]);
}
