/************************************************************************
 *   IRC - Internet Relay Chat, include/serv.h
 *   Copyright (C) 1992 Darren Reed
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
 */
#ifndef INCLUDED_serv_h
#define INCLUDED_serv_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

#include "caps.h"

struct Client;
struct ConfItem;

#define NOCAPS          0

#define CAP_QS          0x00000002      /* Can handle quit storm removal */
#define CAP_ZIP         0x00000004      /* Can do server compresion */
#define CAP_EX          0x00000008      /* Can do channel +e exemptions */
#define CAP_CHW         0x00000010      /* Can do channel wall @# */
#define CAP_DE          0x00000020      /* Can do channel +d (regex deny) */
#define CAP_IE          0x00000040      /* can do +I */
#define CAP_QU          0x00000080      /* can do +q (quiet) */
#define CAP_RW          0x00000100      /* can handle WHOIS messages from remote users. 
					   This is used in m_whois to prevent services
					   from being queried remotely, which it can't
					   do anything with */
#define CAP_SERVICES    0x00000200      /* Is a services daemon. Used primarily to identify
					   what is and is not a server jupe */
#define CAP_LANG        0x00000400      /* Supports LANG, MCNOTICE, and general i18n/l10n stuff */
#define CAP_ENCAP       0x00000800      /* Supports ENCAP (not implemented) */
#define CAP_SIGNON      0x00001000      /* Supports SIGNON (services login name,
					   change username, hostname) */
#define CAP_DANCER      0x10000000      /* Is a dancer 1.0 server */

#define DoesCAP(x)      ((x)->caps)

/*
 * Globals
 *
 *
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
extern struct Capability captab[];
extern struct Capability other_captab[];

extern int MaxClientCount;     /* GLOBAL - highest number of clients */
extern int MaxConnectionCount; /* GLOBAL - highest number of connections */

/* 
 * allow DEFAULT_SERVER_SPLIT_RECOVERY_TIME minutes after server rejoins
 * the network before allowing chanops new channels,
 *  but allow it to be set to a maximum of MAX_SERVER_SPLIT_RECOVERY_TIME 
 */
#if defined(NO_CHANOPS_ON_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
        defined(NO_JOIN_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT_SIMPLE)
#define MAX_SERVER_SPLIT_RECOVERY_TIME 30
#ifndef DEFAULT_SERVER_SPLIT_RECOVERY_TIME
#define DEFAULT_SERVER_SPLIT_RECOVERY_TIME 15
#endif /* DEFAULT_SERVER_SPLIT_RECOVERY_TIME */
#endif

/*
 * return values for hunt_server() 
 */
#define HUNTED_NOSUCH   (-1)    /* if the hunted server is not found */
#define HUNTED_ISME     0       /* if this server should execute the command */
#define HUNTED_PASS     1       /* if message passed onwards successfully */


extern int         check_server(struct Client* server);
extern int         hunt_server(struct Client* cptr, struct Client* sptr,
                               const char* command, int server, 
                               int parc, char** parv);
extern const char* my_name_for_link(const char* name, struct ConfItem* conf);
extern void        send_capabilities(struct Client* client, int use_zip);
extern int         server_estab(struct Client* cptr);
extern void        set_autoconn(struct Client *,char *,char *,int);
extern const char* show_capabilities(struct Client* client);
extern void        show_servers(struct Client *);
extern time_t      try_connections(time_t currenttime);

extern void        rehash_timer(void);

extern struct Client *burst_in_progress;

#endif /* INCLUDED_s_serv_h */



