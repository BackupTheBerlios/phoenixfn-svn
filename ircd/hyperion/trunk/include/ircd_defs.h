/* - Internet Relay Chat, include/ircd_defs.h
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
 *
 * 
 *
 * ircd_defs.h - Global size definitions for record entries used
 * througout ircd. Please think 3 times before adding anything to this
 * file.
 */
#ifndef INCLUDED_ircd_defs_h
#define INCLUDED_ircd_defs_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#include "s_log.h"
#if !defined(CONFIG_H_LEVEL_6_1)
#  error Incorrect config.h for this revision of ircd.
#endif
#include <stdlib.h>

#define HOSTLEN         63      /* Length of hostname.  Updated to         */
                                /* comply with RFC1123                     */

#define NICKLEN         16      /* Necessary to put 9 here instead of 10
                                 * if s_msg.c/m_nick has been corrected.
                                 * This preserves compatibility with old
                                 * servers --msa
                                 */
#define USERLEN         10
#define REALLEN         50
#define TOPICLEN        450     /* old value 90, truncated on other servers */
#define KILLLEN         90      
#define CHANNELLEN      30
#define KEYLEN          23
#define BUFSIZE         512     /* WARNING: *DONT* CHANGE THIS!!!! */
#define MAXRECIPIENTS   20
#define MAXBANS         50      /* bans + exceptions together */
#define MAXBANS_PERM    500     /* bans + exceptions together */
#define MAXBANLENGTH    1024
#define MAXSILES        50
#define MAXSILELENGTH   128

#define OPERWALL_LEN    400     /* can be truncated on other servers */

#define USERHOST_REPLYLEN       (NICKLEN+HOSTLEN+USERLEN+5)
#define MAX_DATE_STRING 32      /* maximum string length for a date string */
#define SERVLOGINLEN    16      /* services login length */
#define SERVLOGIN_NONE  "0"     /* special login: no login (in protocol) */

/* 
 * message return values 
 */
#define CLIENT_EXITED    -2
#define HANDLED_OK        0

/* 
 * Macros everyone uses :/ moved here from sys.h
 */
#ifdef MALLOC_LOG
#define MyFree(x)       if ((x)) {malloc_log("MyFree() releasing %p", (void *)x); free((x));}
#else
/* free() is defined as accepting NULL input silently */
#define MyFree(x)       free(x)
#endif

#define free_client(x) _free_client((x))
#define free_link(x)   _free_link((x))
#define free_user(x,y) _free_user((x), (y))

#ifdef FLUD
#define free_fludbot(x) BlockHeapFree(free_fludbots, (x))
#endif

#ifdef IPV6
#define IN_ADDR     in6_addr
#define S_ADDR      s6_addr
#define SOCKADDR_IN sockaddr_in6
#define SIN_PORT    sin6_port
#define SIN_ADDR    sin6_addr
#define SIN_FAMILY  sin6_family
#define AFINET      AF_INET6
#define INADDRANY   in6addr_any
#else
#define IN_ADDR     in_addr
#define S_ADDR      s_addr
#define SOCKADDR_IN sockaddr_in
#define SIN_PORT    sin_port
#define SIN_ADDR    sin_addr
#define SIN_FAMILY  sin_family
#define AFINET      AF_INET
#define INADDRANY   INADDR_ANY
#endif

#endif /* INCLUDED_ircd_defs_h */
