/************************************************************************
 *   IRC - Internet Relay Chat, src/s_user.c
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
 */

#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "flud.h"
#include "flud.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "md5crypt.h"
#include "motd.h"
#include "msg.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "s_user.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "supported.h"
#include "umodes.h"
#include "whowas.h"

#ifdef ANTI_DRONE_FLOOD
#include "dbuf.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

#ifdef PACE_WALLOPS
time_t LastUsedWallops = 0;
#endif

#ifndef STRIP_MISC
# define strip_colour(X) (X)
#endif

static int do_user (char *, aClient *, aClient*, char *, char *, char *,
                     char *, char *);
static time_t record_time = 0;
static int nickkilldone(aClient *, aClient *, int, char **, time_t, char *);
static void report_and_set_user_flags( aClient *, aConfItem * );
static int tell_user_off(aClient *,char **);
static int add_silence(struct Client *, char *);

/* internally defined functions */
#ifdef BOTCHECK
static int bot_check(char*, char*, char*);

const char *type_of_bot[]={
  "NONE",
  "eggdrop",
  "vald/com/joh bot",
  "spambot",
  "annoy/ojnkbot",
  "sub7server",
  "sub7offer"
};

#endif

unsigned long my_rand(void);    /* provided by orabidoo */


/*
 * show_staff - send the client a list of staff
 * inputs       - pointer to client to show staff to
 * output       - none
 * side effects - show list of staff
 *
 * Probably not that expensive in CPU time with the umode check.
 * -- jilles
 */
void show_staff(struct Client *cptr)
{
  register struct Client        *cptr2;
  register int j=0;

  for(cptr2 = GlobalClientList; cptr2; cptr2 = cptr2->next)
    {
      if (cptr2->user == NULL || !ShowAsStaff(cptr2))
        continue;
#ifdef NO_AWAYS_IN_STATS_P
      if (cptr2->user->away != NULL)
        continue;
#endif
      ++j;
#if 0 /* no remote idle times available right now -- jilles */
      sendto_one(cptr, ":%s %d %s p :%s (%s@%s) Idle: %.1ld%s",
                 me.name, RPL_STATSDEBUG, cptr->name,
                 cptr2->name,
                 cptr2->username, cptr2->host,
                 CurrentTime - cptr2->user->last,
		 cptr2->away != NULL ? " (Away)" : "");
#endif
      sendto_one(cptr, ":%s %d %s p :%s (%s@%s)%s",
                 me.name, RPL_STATSDEBUG, cptr->name,
                 cptr2->name,
                 cptr2->username, cptr2->host,
		 cptr2->user->away != NULL ? " (Away)" : "");
    }
  sendto_one(cptr, ":%s %d %s p :%d staff member%s", me.name, RPL_STATSDEBUG,
             cptr->name, j, (j==1) ? "" : "s");
}

/*
 * show_lusers - total up counts and display to client
 */
int show_lusers(struct Client *cptr, struct Client *sptr, 
                int parc, char *parv[])
{
#define LUSERS_CACHE_TIME 180
  static long last_time=0;
  static int    s_count = 0, c_count = 0, u_count = 0, i_count = 0;
  static int    o_count = 0, m_client = 0, m_servers = 0;
  int forced;
  struct Client *acptr;
  struct AuthRequest *auth;
#ifdef SAVE_MAXCLIENT
  static time_t last_stat_save = 0;
#endif

/*  forced = (parc >= 2); */
  forced = HasUmode(sptr,UMODE_DEBUG) && parc > 3;

/* (void)collapse(parv[1]); */

  m_servers = Count.myserver;
  m_client  = Count.local;
  i_count   = Count.invisi;
  u_count   = Count.unknown;
  c_count   = Count.total-Count.invisi;
  s_count   = Count.server;
  o_count   = Count.oper;
  if (forced || (CurrentTime > last_time+LUSERS_CACHE_TIME))
    {
      last_time = CurrentTime;
      /* only recount if more than a second has passed since last request */
      /* use LUSERS_CACHE_TIME instead... */
      s_count = 0; c_count = 0; u_count = 0; i_count = 0;
      o_count = 0; m_client = 0; m_servers = 0;

      for (acptr = GlobalClientList; acptr; acptr = acptr->next)
        {
          switch (acptr->status)
            {
            case STAT_SERVER:
              if (MyConnect(acptr))
                m_servers++;
            case STAT_ME:
              s_count++;
              break;
            case STAT_CLIENT:
              if (HasUmode(acptr,UMODE_OPER))
                o_count++;
#ifdef  SHOW_INVISIBLE_LUSERS
              if (MyConnect(acptr) && !IsHoneypot(acptr))
                m_client++;
              if (!IsHoneypot(acptr))
                {
                  if (!IsInvisible(acptr))
                    c_count++;
                  else
                    i_count++;
                }
#else
              if (MyConnect(acptr) && !IsHoneypot(acptr))
                {
                  if (IsInvisible(acptr))
                    {
                      if (HasUmode(sptr,UMODE_OPER))
                        m_client++;
                    }
                  else
                    m_client++;
                }
              if (!IsHoneypot(acptr))
                {
                  if (!IsInvisible(acptr))
                    c_count++;
                  else
                    i_count++;
                }
#endif
              break;
	    case STAT_UNKNOWN:
              u_count++;
              break;
            }
        }
      /* stuff for which auth is not yet done does not appear in local[],
       * fix up. XXX this is ugly -- jilles */
      for (auth = AuthPollList; auth; auth = auth->next)
        u_count++;
      for (auth = AuthIncompleteList; auth; auth = auth->next)
        u_count++;

      /*
       * We only want to reassign the global counts if the recount
       * time has expired, and NOT when it was forced, since someone
       * may supply a mask which will only count part of the userbase
       *        -Taner
       */
      if (m_servers != Count.myserver)
        {
          sendto_ops_flag(UMODE_DEBUG,
                              "Local server count off by %d ",
            		  Count.myserver - m_servers);
          Count.myserver = m_servers;
        }
      if (s_count != Count.server)
        {
          sendto_ops_flag(UMODE_DEBUG,
            		  "Server count off by %d",
            		  Count.server - s_count);
          Count.server = s_count;
        }
      if (i_count != Count.invisi)
        {
          sendto_ops_flag(UMODE_DEBUG,
            		  "Invisible client count off by %d",
            		  Count.invisi - i_count);
          Count.invisi = i_count;
        }
      if ((c_count+i_count) != Count.total)
        {
          sendto_ops_flag(UMODE_DEBUG, "Total client count off by %d",
            		  Count.total - (c_count+i_count));
          Count.total = c_count+i_count;
        }
      if (m_client != Count.local)
        {
          sendto_ops_flag(UMODE_DEBUG,
            		  "Local client count off by %d",
            		  Count.local - m_client);
          Count.local = m_client;
        }
      if (o_count != Count.oper)
        {
          sendto_ops_flag(UMODE_DEBUG,
            		  "Oper count off by %d", Count.oper - o_count);
          Count.oper = o_count;
        }
      if (u_count != Count.unknown)
        {
          sendto_ops_flag(UMODE_DEBUG,
            		  "Unknown count off by %d", Count.unknown - u_count);
          Count.unknown = u_count;
        }
    } /* Recount loop */

#ifdef SAVE_MAXCLIENT
  if ((CurrentTime - last_stat_save) > SAVE_TIME)
    {
      write_stats();
      last_stat_save = CurrentTime;
    }
#endif
  
#ifndef SHOW_INVISIBLE_LUSERS
  if (SeesAllConnections(sptr) && i_count)
#endif
    sendto_one(sptr, form_str(RPL_LUSERCLIENT), me.name, parv[0],
               c_count, i_count, s_count);
#ifndef SHOW_INVISIBLE_LUSERS
  else
    sendto_one(sptr,
               ":%s %d %s :There are %d users on %d servers", me.name,
               RPL_LUSERCLIENT, parv[0], c_count,
               s_count);
#endif
  if (o_count)
    sendto_one(sptr, form_str(RPL_LUSEROP),
               me.name, parv[0], o_count);
  if (u_count > 0 && HasUmode(sptr,UMODE_AUSPEX))
    sendto_one(sptr, form_str(RPL_LUSERUNKNOWN),
               me.name, parv[0], u_count);
  /* This should be ok */
  if (Count.chan > 0)
    sendto_one(sptr, form_str(RPL_LUSERCHANNELS),
               me.name, parv[0], Count.chan);
  if (HasUmode(sptr,UMODE_AUSPEX) || HasUmode(sptr,UMODE_SEEROUTING))
    sendto_one(sptr, form_str(RPL_LUSERME),
	       me.name, parv[0], m_client, m_servers);
  else
    sendto_one(sptr, form_str(RPL_LUSERME),
	       me.name, parv[0], m_client, 0);
  sendto_one(sptr, form_str(RPL_LOCALUSERS), me.name, parv[0],
             Count.local, Count.max_loc);
  sendto_one(sptr, form_str(RPL_GLOBALUSERS), me.name, parv[0],
             Count.total, Count.max_tot);

  sendto_one(sptr, form_str(RPL_STATSCONN), me.name, parv[0],
             MaxConnectionCount, MaxClientCount,
             Count.totalrestartcount);
  if (m_client > MaxClientCount)
    MaxClientCount = m_client;
  if ((m_client + m_servers) > MaxConnectionCount)
    {
      MaxConnectionCount = m_client + m_servers;
    }

  return 0;
}

  

/*
** m_functions execute protocol messages on this server:
**
**      cptr    is always NON-NULL, pointing to a *LOCAL* client
**              structure (with an open socket connected!). This
**              identifies the physical socket where the message
**              originated (or which caused the m_function to be
**              executed--some m_functions may call others...).
**
**      sptr    is the source of the message, defined by the
**              prefix part of the message if present. If not
**              or prefix not found, then sptr==cptr.
**
**              (!IsServer(cptr)) => (cptr == sptr), because
**              prefixes are taken *only* from servers...
**
**              (IsServer(cptr))
**                      (sptr == cptr) => the message didn't
**                      have the prefix.
**
**                      (sptr != cptr && IsServer(sptr) means
**                      the prefix specified servername. (?)
**
**                      (sptr != cptr && !IsServer(sptr) means
**                      that message originated from a remote
**                      user (not local).
**
**              combining
**
**              (!IsServer(sptr)) means that, sptr can safely
**              taken as defining the target structure of the
**              message in this server.
**
**      *Always* true (if 'parse' and others are working correct):
**
**      1)      sptr->from == cptr  (note: cptr->from == cptr)
**
**      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
**              *cannot* be a local connection, unless it's
**              actually cptr!). [MyConnect(x) should probably
**              be defined as (x == x->from) --msa ]
**
**      parc    number of variable parameter strings (if zero,
**              parv is allowed to be NULL)
**
**      parv    a NULL terminated list of parameter pointers,
**
**                      parv[0], sender (prefix string), if not present
**                              this points to an empty string.
**                      parv[1]...parv[parc-1]
**                              pointers to additional parameters
**                      parv[parc] == NULL, *always*
**
**              note:   it is guaranteed that parv[0]..parv[parc-1] are all
**                      non-NULL pointers.
*/

/*
 * clean_nick_name - ensures that the given parameter (nick) is
 * really a proper string for a nickname (note, the 'nick'
 * may be modified in the process...)
 *
 *      RETURNS the length of the final NICKNAME (0, if
 *      nickname is illegal)
 *
 *  Nickname characters are in range
 *      'A'..'}', '_', '-', '0'..'9'
 *  anything outside the above set will terminate nickname.
 *  In addition, the first character cannot be '-'
 *  or a Digit.
 *
 *  Note:
 *      '~'-character should be allowed, but
 *      a change should be global, some confusion would
 *      result if only few servers allowed it...
 */
static int clean_nick_name(char* nick)
{
  char* ch   = nick;
  char* endp = ch + NICKLEN;
  int got_alnum = 0;
  assert(0 != nick);

  if (*nick == '-' || IsDigit(*nick)) /* first character in [0..9-] */
    return 0;
  
  for ( ; ch < endp && *ch; ++ch) {
    if (!IsNickChar(*ch))
      break;
    if (isalnum(*ch)) got_alnum++;
  }
  *ch = '\0';
  if (got_alnum < 2 && (ch - nick) > 3) return 0;
  return (ch - nick);
}

/* Checks if nick matches one of client_p's allowed nicks -- jilles */
static int
isallowednick(struct Client *client_p, char *nick)
{
	char *p;
	char basenick[NICKLEN + 1];
	int i;

	p = nick;
	while (IsNickChar(*p) && !isalnum(*p))
		p++;
	i = 0;
	while (isalnum(*p) && i < NICKLEN)
		basenick[i++] = *p++;
	basenick[i] = '\0';
	if (isalnum(*p))
		return 0;
	p = client_p->allownicks;
	if (p == NULL)
		return 0;
	while (*p != '\0')
	{
		if (!irccmp(basenick, p))
			return 1;
		p += strlen(p) + 1;
	}
	return 0;
}

/*
 * show_isupport
 *
 * inputs	- pointer to client
 * output	- 
 * side effects	- display to client what we support (for them)
 */
/* Taken from ircd-ratbox, Copyright (C) 2002-2005 ircd-ratbox development team
 * -- jilles */
static void
show_isupport(struct Client *source_p)
{
	char isupportbuffer[512];

	ircsnprintf(isupportbuffer, sizeof(isupportbuffer), FEATURES, FEATURESVALUES);
	sendto_one(source_p, form_str(RPL_ISUPPORT), me.name, source_p->name, isupportbuffer);

	ircsnprintf(isupportbuffer, sizeof(isupportbuffer), FEATURES2, FEATURES2VALUES);
	sendto_one(source_p, form_str(RPL_ISUPPORT), me.name, source_p->name, isupportbuffer);

	return;
}

/*
** register_user
**      This function is called when both NICK and USER messages
**      have been accepted for the client, in whatever order. Only
**      after this, is the USER message propagated.
**
**      NICK's must be propagated at once when received, although
**      it would be better to delay them too until full info is
**      available. Doing it is not so simple though, would have
**      to implement the following:
**
**      (actually it has been implemented already for a while) -orabidoo
**
**      1) user telnets in and gives only "NICK foobar" and waits
**      2) another user far away logs in normally with the nick
**         "foobar" (quite legal, as this server didn't propagate
**         it).
**      3) now this server gets nick "foobar" from outside, but
**         has already the same defined locally. Current server
**         would just issue "KILL foobar" to clean out dups. But,
**         this is not fair. It should actually request another
**         nick from local user or kill him/her...
*/

static int register_user(aClient *cptr, aClient *sptr, 
                         char *nick, char *username)
{
  aConfItem*  aconf;
  char*       parv[3];
  anUser*     user = sptr->user;
  char*       reason;
  char        tmpstr2[512];
  aClient*    identifyservice_p;
  int         flag;
  char*       m;

  assert(0 != sptr);
  assert(sptr->username != username);

  sptr->user->last = CurrentTime;
  parv[0] = sptr->name;
  parv[1] = parv[2] = NULL;

  /* pointed out by Mortiis, never be too careful */
  /* Why waste time? -- asuffield */
/*   if(strlen(username) > USERLEN) */
  username[USERLEN] = '\0';

  reason = NULL;

#define NOT_AUTHORIZED  (-1)
#define SOCKET_ERROR    (-2)
#define I_LINE_FULL     (-3)
#define I_LINE_FULL2    (-4)
#define BANNED_CLIENT   (-5)
#define I_LINE_FULL3    (-6)

  if (MyConnect(sptr))
    {
      if (!strcmp(sptr->sockhost, "127.0.0.1") || !strcmp(sptr->sockhost, "0:0:0:0:0:0:0:1"))
        {
	  strncpy_irc(sptr->host, me.name, HOSTLEN + 1);
	  strncpy_irc(sptr->dnshost, me.name, HOSTLEN + 1);
	}
      switch(check_client(sptr,username,&reason))
        {
        case SOCKET_ERROR:
          return exit_client(cptr, sptr, &me, "Socket Error");
          break;

        case I_LINE_FULL:
          sendto_ops_flag(UMODE_FULL, "Connection class is full for %s!%s%s@%s (%s)", nick, IsGotId(sptr) ? GlobalSetOptions.identd_prefix : GlobalSetOptions.noidentd_prefix, username, sptr->host, sptr->sockhost);
          logprintf(L_INFO,"Too many connections (connection class full) from %s.", get_client_host(sptr));
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me, "No more connections allowed in your connection class");
          break;

        case I_LINE_FULL2:
          sendto_ops_flag(UMODE_FULL, "Too many local connections per IP for %s!%s%s@%s (%s)", nick, IsGotId(sptr) ? GlobalSetOptions.identd_prefix : GlobalSetOptions.noidentd_prefix, username, sptr->host, sptr->sockhost);
          logprintf(L_INFO,"Too many local connections per IP from %s.", get_client_host(sptr));
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me, "Too many host connections (local)");
          break;

        case I_LINE_FULL3:
          sendto_ops_flag(UMODE_FULL, "Too many global connections per IP for %s!%s%s@%s (%s)", nick, IsGotId(sptr) ? GlobalSetOptions.identd_prefix : GlobalSetOptions.noidentd_prefix, username, sptr->host, sptr->sockhost);
          logprintf(L_INFO,"Too many global connections per IP from %s.", get_client_host(sptr));
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me, "Too many host connections (global)");
          break;

        case NOT_AUTHORIZED:

#ifdef REJECT_HOLD

          /* Slow down the reconnectors who are rejected */
          if( (reject_held_fds != REJECT_HELD_MAX ) )
            {
              SetRejectHold(cptr);
              reject_held_fds++;
              release_client_dns_reply(cptr);
              return 0;
            }
          else
#endif
            {
              ServerStats->is_ref++;
	/* jdc - lists server name & port connections are on */
	/*       a purely cosmetical change */
              sendto_ops_flag(UMODE_CCONN,
			      "%s from %s [%s] on [%s/%u].",
			      "Unauthorized client connection",
			      get_client_host(sptr),
			      inetntoa((char *)&sptr->ip),
			      sptr->listener->name,
			      sptr->listener->port);
              logprintf(L_INFO,
		  "Unauthorized client connection from %s on [%s/%u].",
                  get_client_host(sptr),
		  sptr->listener->name,
		  sptr->listener->port);

              ServerStats->is_ref++;
              return exit_client(cptr, sptr, &me,
                                 "You are not authorized to use this server");
            }
          break;

        case BANNED_CLIENT:
          {
            if (!IsGotId(sptr))
              {
                if (IsNeedId(sptr))
                  {
                    strncpy_irc(sptr->username, GlobalSetOptions.noidentd_prefix, USERLEN + 1);
                    strncpy_irc(sptr->username + strlen(GlobalSetOptions.noidentd_prefix), username, USERLEN + 1 - strlen(GlobalSetOptions.noidentd_prefix));
                  }
                else
                  {
                    strncpy_irc(sptr->username, GlobalSetOptions.identd_prefix, USERLEN + 1);
                    strncpy_irc(sptr->username + strlen(GlobalSetOptions.identd_prefix), username, USERLEN + 1 - strlen(GlobalSetOptions.identd_prefix));
                  }
                sptr->username[USERLEN] = '\0';
              }

            if ( tell_user_off( sptr, &reason ))
              {
                ServerStats->is_ref++;
                return exit_client(cptr, sptr, &me, "Banned" );
              }
            else
              return 0;
            break;
          }
        default:
          release_client_dns_reply(cptr);
          break;
        }
      if(!valid_hostname_local(sptr->dnshost))
        {
          sendto_one(sptr,":%s NOTICE %s :*** Notice -- hostname %s isn't valid, using IP address",
                     me.name, sptr->name, sptr->dnshost);

	  if (!strcmp(sptr->host, sptr->dnshost))
            strncpy_irc(sptr->host, sptr->sockhost, HOSTIPLEN + 1);
          strncpy_irc(sptr->dnshost, sptr->sockhost, HOSTIPLEN + 1);
        }
      if (IsIPHidden(sptr) && !valid_hostname_remote(sptr->host))
	{
          sendto_one(sptr,":%s NOTICE %s :*** Notice -- spoof %s isn't valid, using %s", me.name, sptr->name, sptr->host, SPOOF_LIMIT_HOST);
	  strncpy_irc(sptr->host, SPOOF_LIMIT_HOST, HOSTLEN + 1);
	  strncpy_irc(sptr->spoofhost, SPOOF_LIMIT_HOST, HOSTLEN + 1);
	}

      aconf = sptr->confs->value.aconf;
      if (!aconf)
        return exit_client(cptr, sptr, &me, "*** Not Authorized");
      if (!IsGotId(sptr))
        {
          if (IsNeedIdentd(aconf))
            {
              ServerStats->is_ref++;
              sendto_one(sptr, ":%s NOTICE %s :*** Notice -- You need to install identd to use this server",
                         me.name, cptr->name);
               return exit_client(cptr, sptr, &me, "Install identd");
             }
           if (IsNoTilde(aconf))
             {
		/* No tilde -> do as if they have identd, even though they
		 * don't ;p -- jilles */
		strncpy_irc(sptr->username, GlobalSetOptions.identd_prefix, USERLEN + 1);
		strncpy_irc(sptr->username + strlen(GlobalSetOptions.identd_prefix), username, USERLEN + 1 - strlen(GlobalSetOptions.identd_prefix));
             }
           else
             {
		strncpy_irc(sptr->username, GlobalSetOptions.noidentd_prefix, USERLEN + 1);
		strncpy_irc(sptr->username + strlen(GlobalSetOptions.noidentd_prefix), username, USERLEN + 1 - strlen(GlobalSetOptions.noidentd_prefix));
             }
           sptr->username[USERLEN] = '\0';
        }
      else
	{
	  strncpy_irc(tmpstr2, GlobalSetOptions.identd_prefix, USERLEN + 1);
	  strncpy_irc(tmpstr2 + strlen(GlobalSetOptions.identd_prefix), sptr->username, USERLEN + 1 - strlen(GlobalSetOptions.identd_prefix));
	  strncpy_irc(sptr->username, tmpstr2, USERLEN + 1);
	}

      /* password check */

#ifdef CRYPT_I_PASSWORD
      if (!BadPtr(aconf->passwd) && 0 != strcmp(libshadow_md5_crypt(sptr->passwd, aconf->passwd), aconf->passwd))
#else
      if (!BadPtr(aconf->passwd) && 0 != strcmp(sptr->passwd, aconf->passwd))
#endif
        {
          ServerStats->is_ref++;
          sendto_one(sptr, form_str(ERR_PASSWDMISMATCH),
                     me.name, parv[0]);
          return exit_client(cptr, sptr, &me, "Bad Password");
        }
      /* If password was used, clear it now, otherwise keep it for
       * services -- jilles */
      if (!BadPtr(aconf->passwd))
        memset(sptr->passwd,0, sizeof(sptr->passwd));

      /* report if user has &^>= etc. and set flags as needed in sptr */
      report_and_set_user_flags(sptr, aconf);

      /* Limit clients */
      /*
       * We want to be able to have servers and F-line clients
       * connect, so save room for "buffer" connections.
       * Smaller servers may want to decrease this, and it should
       * probably be just a percentage of the MAXCLIENTS...
       *   -Taner
       */
      /* Except "F:" clients */
      if ( (
#ifdef BOTCHECK
          !sptr->isbot &&
#endif /* BOTCHECK */
          ((Count.local + 1) >= (MAXCLIENTS+MAX_BUFFER))) ||
            (((Count.local +1) >= (MAXCLIENTS - 5)) && !(IsFlined(sptr))))
        {
          sendto_ops_flag(UMODE_FULL, "Too many clients, rejecting %s[%s].",
                          nick, sptr->host);
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me, "Sorry, server is full - try later");
        }
      /* botcheck */
#ifdef BOTCHECK
      if(sptr->isbot)
        {
          if(IsBlined(sptr))
            {
              sendto_ops_flag(UMODE_BOTS,
				     "Possible %s: %s (%s@%s) [B-lined]",
				     type_of_bot[sptr->isbot],
				     sptr->name, sptr->username, sptr->host);
            }
          else
            {
              sendto_ops_flag(UMODE_BOTS, "Rejecting %s: %s",
				  type_of_bot[sptr->isbot],
				  get_client_name(sptr,FALSE));
              ServerStats->is_ref++;
              return exit_client(cptr, sptr, sptr, type_of_bot[sptr->isbot] );
            }
        }
#endif
      /* End of botcheck */

      /* valid user name check */

      if (!valid_username(sptr->username))
        {
          sendto_ops_flag(UMODE_REJ,"Invalid username: %s (%s@%s)",
                          nick, sptr->username, sptr->host);
          ServerStats->is_ref++;
          ircsnprintf(tmpstr2, 512, "Invalid username [%s]", sptr->username);
          return exit_client(cptr, sptr, &me, tmpstr2);
        }
      /* end of valid user name check */

      /* ^ in I:line extends to xlines too -- jilles */
      if(!IsElined(sptr))
        {
          char *xreason;

          if ( (aconf = find_special_conf(sptr->info,CONF_XLINE)))
            {
              if(aconf->passwd)
                xreason = aconf->passwd;
              else
		{
		  static char none[] = "NONE";
		  xreason = none;
		}
              
              if(aconf->port)
                {
/*                   if (aconf->port == 1) */
/*                     { */
/*                       sendto_ops_flag(UMODE_REJ, */
/* 				      "X-line Rejecting [%s] [%s], user %s", */
/* 				      sptr->info, */
/* 				      xreason, */
/* 				      get_client_name(cptr, FALSE)); */
/*                     } */
                  ServerStats->is_ref++;      
                  return exit_client(cptr, sptr, &me, "Bad user info");
                }
              else
                sendto_ops_flag(UMODE_REJ,
				"X-line Warning [%s] [%s], user %s",
				sptr->info,
				xreason,
				cptr->name);
            }
         }

      if (!IsHoneypot(sptr))
        if ((++Count.local) > Count.max_loc)
          {
            Count.max_loc = Count.local;
            if (!(Count.max_loc % 10))
              sendto_ops_flag(UMODE_SERVNOTICE, "New local record: %d users",
                              Count.max_loc);
          }

      /* Umode initialization moved here -- jilles */
      CopyUmodes(sptr->allowed_umodes, user_umodes);
      m = GlobalSetOptions.default_umode;
      while (*m)
        {
          flag = user_modes_from_c_to_bitmask[(unsigned char)*m];
          SetBit(sptr->umodes, flag);
          m++;
        }
      if (GlobalSetOptions.noidprivmsg)
        SetBit(sptr->umodes, UMODE_BLOCK_NOTID);
      AndUmodes(sptr->umodes, sptr->umodes, user_umodes);

      if (IsInvisible(sptr))
        Count.invisi++;

      --Count.unknown;
      /* Doing SetClient(sptr) right after this, so the counts stay
       * correct -- jilles */
    }
  else
    {
      struct ConfItem* aconf;
      struct Client* acptr = find_server(user->server);

      strncpy_irc(sptr->username, username, USERLEN + 1);
      if (acptr) /* duplicates ghost check, oh well */
	{
	  /* OK, it's from a remote server. I'm going to check against *MY* K:lines. */
	  aconf = find_matching_mtrie_conf(sptr->host, sptr->username, sptr->name, get_ipv4_ip(&sptr->ip));
	  
	  if (aconf && (aconf->status & CONF_KILL))
	    {
	      /* Propagate the K:line */
	      sendto_ops_flag(UMODE_DEBUG, "Remote user %s K:lined here (%s), propagating K:line for %s@%s (%s) to %s", 
			      sptr->name, me.name,
                              aconf->user, aconf->host, aconf->passwd,
                              sptr->user->server);
	      if (aconf->hold)
		{
		  /* This one has a timeout */
		  long int time_remaining = (aconf->hold - CurrentTime) / 60;
		  /* Having minutes instead of seconds in S-S protocol
		   * is a mistake, but can't change it anymore now -- jilles */
		  if (time_remaining > 0)
		    sendto_one(acptr, ":%s KLINE %s %.1ld %s@%s :%s", me.name, me.name, 
			       time_remaining, aconf->user, aconf->host, aconf->passwd);
		}
	      else
		sendto_one(acptr, ":%s KLINE %s %s@%s :%s", me.name, me.name, aconf->user, aconf->host, aconf->passwd);
	      /* used to exit the client here, don't do that as the
	       * other server may not see the kline as matching the
	       * user and the user should get a proper k-line message
	       * in any case, not a "ghosted" message -- jilles */
	    }
          /* We have the I:line aconf here if it's necessary to kill
           * remote clients for limits */
          add_remote_client_ip(sptr);
	}
    }

  SetClient(sptr);
  /* Increment our total user count here */
  if (!IsHoneypot(sptr))
    if (++Count.total > Count.max_tot)
      {
        Count.max_tot = Count.total;
        if ( (!(Count.max_tot % 2)) && (record_time < CurrentTime - 30) )
          {
            sendto_local_ops_flag(UMODE_SERVNOTICE, "New network record: %d users (%d local)", Count.max_tot, Count.max_loc);
            record_time = CurrentTime;
          }
      }

  sptr->servptr = find_server(user->server);
  if (!sptr->servptr)
    {
      sendto_ops_flag(UMODE_SERVNOTICE,"Ghost killed: %s on invalid server %s",
                 sptr->name, sptr->user->server);
      sendto_one(cptr,":%s KILL %s :%s (Ghosted, %s doesn't exist)",
                 me.name, sptr->name, me.name, user->server);
      sptr->flags |= FLAGS_KILLED;
      return exit_client(NULL, sptr, &me, "Ghost");
    }
  add_client_to_llist(&(sptr->servptr->serv->users), sptr);

  /* Move this down here and only send it locally, to cut down on network noise
   *  -- asuffield 
   */
  sendto_local_ops_flag(UMODE_CCONN, "Client connecting: %s (%s@%s) [%s] [%s] [%s]",
			nick, sptr->username, sptr->host,
			sptr->sockhost, sptr->servptr->name, sptr->info);

  if (MyConnect(sptr))
    {
      sendto_one(sptr, form_str(RPL_WELCOME), me.name, nick, NETWORK_REALNAME, nick);
      /* This is a duplicate of the NOTICE but see below...*/
      sendto_one(sptr, form_str(RPL_YOURHOST), me.name, nick,
                 get_listener_name(sptr->listener), version);
      
      /*
      ** Don't mess with this one - IRCII needs it! -Avalon
      */
      sendto_one(sptr, "NOTICE %s :*** Your host is %s, running version %s",
                 nick, get_listener_name(sptr->listener), version);
      
      sendto_one(sptr, form_str(RPL_CREATED),me.name,nick,creation);
      sendto_one(sptr, form_str(RPL_MYINFO), me.name, parv[0],
                 me.name, version, umode_list);
      show_isupport(sptr);
      show_ip_info(sptr);
      /* Increment the total number of clients since (re)start */
      Count.totalrestartcount++;
      show_lusers(sptr, sptr, 1, parv);

#ifdef SHORT_MOTD
      sendto_one(sptr, "NOTICE %s :*** Notice -- MOTD was last changed at %s",
                 sptr->name,
                 ConfigFileEntry.motd.lastChangedDate);

      sendto_one(sptr, "NOTICE %s :*** Notice -- If you haven't read the MOTD, please do (/motd)",
                 sptr->name);
      
      sendto_one(sptr, form_str(RPL_MOTDSTART),
                 me.name, sptr->name, me.name);
      
      sendto_one(sptr,
                 form_str(RPL_MOTD),
                 me.name, sptr->name,
                 "*** This is the short motd ***"
                 );

      sendto_one(sptr, form_str(RPL_ENDOFMOTD),
                 me.name, sptr->name);
#else
      SendMessageFile(sptr, &ConfigFileEntry.motd);
#endif
      
#ifdef LITTLE_I_LINES
      if(sptr->confs && sptr->confs->value.aconf &&
         (sptr->confs->value.aconf->flags
          & CONF_FLAGS_LITTLE_I_LINE))
        {
          SetRestricted(sptr);
          sendto_one(sptr,"NOTICE %s :*** Notice -- You are in a restricted access mode",nick);
          sendto_one(sptr,"NOTICE %s :*** Notice -- You can not chanop others",nick);
        }
#endif

#ifdef NEED_SPLITCODE
      if (server_was_split)
        sendto_one(sptr, "NOTICE %s :*** Notice -- Server is currently split, channel modes are limited",nick);

      nextping = CurrentTime;
#endif
    }
  else if (IsServer(cptr))
    {
      aClient *acptr;
      if ((acptr = find_server(user->server)) && acptr->from != sptr->from)
        {
          sendto_ops_flag(UMODE_DEBUG, 
                          "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
                          cptr->name, nick, sptr->username,
                          sptr->host, user->server,
                          acptr->name, acptr->from->name);
          sendto_one(cptr,
                     ":%s KILL %s :%s (%s != %s[%s] USER from wrong direction)",
                     me.name, sptr->name, me.name, user->server,
                     acptr->from->name, acptr->from->host);
          sptr->flags |= FLAGS_KILLED;
          return exit_client(sptr, sptr, &me,
                             "USER server wrong direction");
          
        }
      /*
       * Super GhostDetect:
       *        If we can't find the server the user is supposed to be on,
       * then simply blow the user away.        -Taner
       */
      if (!acptr)
        {
          sendto_one(cptr,
                     ":%s KILL %s :%s GHOST (no server %s on the net)",
                     me.name,
                     sptr->name, me.name, user->server);
          sendto_ops_flag(UMODE_SERVNOTICE,"No server %s for user %s[%s@%s] from %s",
                          user->server, sptr->name, sptr->username,
                          sptr->host, sptr->from->name);
          sptr->flags |= FLAGS_KILLED;
          return exit_client(sptr, sptr, &me, "Ghosted Client");
        }
    }

  /* LINKLIST 
   * add to local client link list -Dianora
   * I really want to move this add to link list
   * inside the if (MyConnect(sptr)) up above
   * but I also want to make sure its really good and registered
   * local client
   *
   * double link list only for clients, traversing
   * a small link list for opers/servers isn't a big deal
   * but it is for clients -Dianora
   */

  if (MyConnect(sptr))
    {
      if(local_cptr_list)
        local_cptr_list->previous_local_client = sptr;
      sptr->previous_local_client = (aClient *)NULL;
      sptr->next_local_client = local_cptr_list;
      local_cptr_list = sptr;
    }
  
  /* Store origname */
  strncpy_irc(sptr->origname, sptr->name, HOSTLEN + 1);

  /* Make sure spoofhost is not the empty string (which could
   * cause an invalid SNICK to be sent later) -- jilles */
  if (sptr->spoofhost[0] == '\0')
    strcpy(sptr->spoofhost, "x.");

  if (!IsHoneypot(sptr))
    {
      sendto_serv_butone(cptr, "NICK %s %d %lu +%s %s %s %s %s :%s",
                         nick, sptr->hopcount+1, (long unsigned)sptr->tsinfo,
                         umodes_as_string(&sptr->umodes),
                         sptr->username, sptr->host, user->server,
                         sptr->sockhost, sptr->info);
      /* Propagate spoofhost if necessary */
      /* Note that we can't propagate here if the user is remote -
       * we'll do it in m_snick instead, when we actually know the
       * correct value
       */
      if (MyConnect(sptr) /*&& (strcmp(sptr->spoofhost, SPOOF_LIMIT_HOST) != 0
			      || strcmp(sptr->dnshost, sptr->host) != 0)*/)
	{
          sendto_serv_butone(NULL, "SNICK %s %s %s %.1ld %s %s", sptr->name,
                           sptr->origname, sptr->spoofhost, sptr->firsttime,
			   sptr->dnshost, sptr->user->servlogin[0] ?
			   sptr->user->servlogin : SERVLOGIN_NONE);
	  /* notify client of initial umode -- jilles */
	  if (AnyBits(sptr->umodes))
	    sendto_one(sptr, ":%s MODE %s :+%s", sptr->name, sptr->name,
                       umodes_as_string(&sptr->umodes));
	  if (sptr->passwd[0] != '\0' && GlobalSetOptions.identifyservice[0] != '\0' && GlobalSetOptions.identifycommand[0] != '\0')
	    {
	      identifyservice_p = find_client(GlobalSetOptions.identifyservice, NULL);
	      if (identifyservice_p != NULL)
	        {
		  sendto_one(identifyservice_p, ":%s PRIVMSG %s :%s %s",
		    sptr->name, GlobalSetOptions.identifyservice,
		    GlobalSetOptions.identifycommand, sptr->passwd);
	        }
	    }
          memset(sptr->passwd,0, sizeof(sptr->passwd));
	}
    }

  if (sptr->user)
    sptr->user->last_sent = CurrentTime;

  return 0;
}

/* 
 * valid_hostname_local - check hostname for validity
 *                        (local client's DNS name)
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot or colon and
 * will not allow more dots than chars.
 */
int valid_hostname_local(const char* hostname)
{
  int         dots  = 0;
  int         chars = 0;
  const char* p     = hostname;

  assert(0 != p);

  if (strlen(hostname) > HOSTLEN)
    return NO;

  if ('.' == *p || ':' == *p)
    return NO;

  while (*p) {
    if (!IsHostChar(*p))
      return NO;
    if ('.' == *p || ':' == *p)
      ++dots;
    else
      ++chars;
    ++p;
  }
  return (0 == dots || chars < dots) ? NO : YES;
}

/* 
 * valid_hostname_remote - check hostname for validity
 *                         (remote client or SETHOST)
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot, slash or colon
 */
int valid_hostname_remote(const char* hostname)
{
  int         dots  = 0;
  const char* p     = hostname;
  const char* lastslash = NULL;

  assert(0 != p);

  if (strlen(hostname) > HOSTLEN)
    return NO;

  if ('.' == *p || '/' == *p || ':' == *p)
    return NO;

  while (*p) {
    if ('.' == *p || ':' == *p)
      ++dots;
    else if ('/' == *p) {
      ++dots;
      lastslash = p;
    }
    else if (!IsHostChar(*p))
      return NO;
    ++p;
  }
  if (lastslash != NULL && IsDigit(lastslash[1]))
    return NO;
  return (0 == dots) ? NO : YES;
}


/* 
 * valid_username - check username for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 * 
 * Absolutely always reject any '*' '!' '?' '@' '.' in an user name
 * reject any odd control characters names.
 */
int valid_username(const char* username)
{
  const char *p = username;
  assert(0 != p);

  if ('~' == *p)
    ++p;
  else if (!strncmp(p, GlobalSetOptions.identd_prefix, strlen(GlobalSetOptions.identd_prefix)))
    p += strlen(GlobalSetOptions.identd_prefix);
  else if (!strncmp(p, GlobalSetOptions.noidentd_prefix, strlen(GlobalSetOptions.noidentd_prefix)))
    p += strlen(GlobalSetOptions.noidentd_prefix);
  else if (p[0] != '\0' && p[1] == '=')
    p += 2;
  /* 
   * reject usernames that don't start with an alphanum
   * i.e. reject jokers who have '-@somehost' or '.@somehost'
   * or "-hi-@somehost", "h-----@somehost" would still be accepted.
   *
   * -Dianora
   */
  /* Allow starting with an underscore as well
   * For example, many tor nodes ident as _tor
   * -- jilles */
  if (!IsAlNum(*p) && *p != '_')
    return NO;

  while (*++p) {
    if (!IsUserChar(*p))
      return NO;
  }
  return YES;
}

/* 
 * tell_user_off
 *
 * inputs       - client pointer of user to tell off
 *              - pointer to reason user is getting told off
 * output       - drop connection now YES or NO (for reject hold)
 * side effects -
 */

static int
tell_user_off(aClient *cptr, char **preason )
{
#ifdef KLINE_WITH_REASON
  char *p = 0, *q;
#endif /* KLINE_WITH_REASON */
  struct ConfItem *aconf;
  const char *adminemail;

  /* Ok... if using REJECT_HOLD, I'm not going to dump
   * the client immediately, but just mark the client for exit
   * at some future time, .. this marking also disables reads/
   * writes from the client. i.e. the client is "hanging" onto
   * an fd without actually being able to do anything with it
   * I still send the usual messages about the k line, but its
   * not exited immediately.
   * - Dianora
   */
            
#ifdef REJECT_HOLD
  if( (reject_held_fds != REJECT_HELD_MAX ) )
    {
      SetRejectHold(cptr);
      reject_held_fds++;
#endif

#ifdef KLINE_WITH_REASON
      if(*preason && **preason != '\0')
        {
	  /* Hide oper reason */
          if(( p = strchr(*preason, '|')) )
            *p = '\0';

	  /* Strip nick of who set the ban */
	  q = strchr(*preason, ';');
	  if (q != NULL)
	    q++;
	  else
	    q = *preason;
          sendto_one(cptr, ":%s NOTICE %s :*** Banned: %s",
                     me.name,cptr->name,q);
            
          if(p)
            *p = '|';
        }
      else
#endif
        {
	  /* Note: if KLINE_WITH_REASON is enabled, we only get here
	   * if someone has edited a conf file and removed a reason
	   */
	  /* Get email address from A:line -- jilles */
	  aconf = find_admin();
	  adminemail = aconf != NULL ? aconf->user : "<not configured>";
          sendto_one(cptr, get_str(STR_YOUREBANNED), /* ":%s NOTICE %s :*** Banned: No Reason", */
                   me.name,cptr->name,adminemail);
        }
#ifdef REJECT_HOLD
      return NO;
    }
#endif

  return YES;
}

/* report_and_set_user_flags
 *
 * Inputs       - pointer to sptr
 *              - pointer to aconf for this user
 * Output       - NONE
 * Side effects -
 * Report to user any special flags they are getting, and set them.
 */

static void 
report_and_set_user_flags(aClient *sptr,aConfItem *aconf)
{
  /* If this user is being spoofed, tell them so */
  if(IsConfDoAutoSpoof(aconf))
    {
      sendto_one(sptr,
                 ":%s NOTICE %s :*** Spoofing your IP. congrats.",
                 me.name,sptr->name);
    }

  /* If this user is in the exception class, Set it "E lined" */
  if(IsConfElined(aconf))
    {
      SetElined(sptr);
      sendto_one(sptr,
         ":%s NOTICE %s :*** You are exempt from K/D/X lines. congrats.",
                 me.name,sptr->name);
    }

  /* Honeypot is silent; the client is (obviously) not warned */
  if (IsConfHoneypot(aconf))
    SetHoneypot(sptr);

  /* If this user can run bots set it "B lined" */
  if(IsConfBlined(aconf))
    {
      SetBlined(sptr);
      sendto_one(sptr,
                 ":%s NOTICE %s :*** You can run bots here. congrats.",
                 me.name,sptr->name);
    }

  /* If this user is exempt from user limits set it F lined" */
  if(IsConfFlined(aconf))
    {
      SetFlined(sptr);
      sendto_one(sptr,
                 ":%s NOTICE %s :*** You are exempt from user limits. congrats.",
                 me.name,sptr->name);
    }
}

/*
 * nickkilldone
 *
 * input        - pointer to physical aClient
 *              - pointer to source aClient
 *              - argument count
 *              - arguments
 *              - newts time
 *              - nick
 * output       -
 * side effects -
 */

static int
nickkilldone(struct Client *cptr, struct Client *sptr, int parc,
	     char *parv[], time_t newts, char *nick)
{
  struct SLink *channels;
  user_modes old_umodes;

  if (IsServer(sptr))
    ClearBitfield(old_umodes);
  else
    CopyUmodes(old_umodes, sptr->umodes);

  if (IsServer(sptr))
    {
      /* A server introducing a new client, change source */
      
      sptr = make_client(cptr);
      add_client_to_list(sptr);         /* double linked list */
      if (parc > 2)
        sptr->hopcount = atoi(parv[2]);
      if (newts)
        sptr->tsinfo = newts;
      else
        {
          newts = sptr->tsinfo = CurrentTime;
          ts_warn("Remote nick %s (%s) introduced without a TS", nick, parv[0]);
        }
      /* copy the nick in place */
      strncpy_irc(sptr->name, nick, NICKLEN + 1);
      add_to_client_hash_table(nick, sptr);
      if (parc > 9)
        {
          int   flag;
          char* m;

          m = &parv[4][1];
          while (*m)
            {
              flag = user_modes_from_c_to_bitmask[(unsigned char)*m];
	      SetBit(sptr->umodes, flag);
	      if (!MyClient(sptr))
		sptr->from->serv->umode_count[flag]++;
              m++;
            }

	  if (HasUmode(sptr, UMODE_INVISIBLE))
	    Count.invisi++;
	  if (HasUmode(sptr, UMODE_OPER))
	    Count.oper++;
          
          return do_user(nick, cptr, sptr, parv[5], parv[6],
                         parv[7], parv[8], parv[9]);
        }
    }
  else if (sptr->name[0])
    {
      /*
      ** Client just changing his/her nick. If he/she is
      ** on a channel, send note of change to all clients
      ** on that channel. Propagate notice to other servers.
      */
      if (irccmp(parv[0], nick))
        sptr->tsinfo = newts ? newts : CurrentTime;

      if (IsHoneypot(sptr))
        {
          sendto_one(sptr, ":%s NICK :%s", parv[0], nick);
        }
      else if (MyConnect(sptr) && IsRegisteredUser(sptr) && cptr)
        { 
	  for (channels = sptr->user->channel; channels; channels = channels->next)
	    {
	      if (can_send(sptr, channels->value.chptr) != 0)
		{
		  /* Cannot send to that channel, so cannot change nicks */
		  sendto_one(sptr, form_str(ERR_BANNICKCHANGE),
			     me.name,
			     sptr->name,
			     nick,
			     channels->value.chptr->chname);
		  return 0;
		}
	    }

#ifdef ANTI_NICK_FLOOD

          if( (sptr->last_nick_change + MAX_NICK_TIME) < CurrentTime)
            sptr->number_of_nick_changes = 0;
          sptr->number_of_nick_changes++;

          if((sptr->number_of_nick_changes <= MAX_NICK_CHANGES) || NoFloodProtection(sptr))
            {
              sptr->last_nick_change = CurrentTime;
#endif
              sendto_local_ops_flag(UMODE_NCHANGE,
				    "Nick change: From %s to %s [%s@%s]",
				    parv[0], nick, sptr->username,
				    sptr->host);

	      if (irccmp(nick, sptr->name))
		{
		  ClearUmode(sptr,UMODE_IDENTIFIED);
		  send_umode_out(cptr, sptr, sptr, &old_umodes);
		}

              sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
              if (sptr->user)
                {
                  add_history(sptr,1);
              
                  sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                                     parv[0], nick, (long unsigned)sptr->tsinfo);
		  /* This global message will reset the idle time everywhere, so
		   *  reset the last_sent counter as well
		   */
		  sptr->user->last_sent = CurrentTime;
                }
#ifdef ANTI_NICK_FLOOD
            }
          else
            {
              sendto_one(sptr,
                         ":%s NOTICE %s :*** Notice -- Too many nick changes; wait %d seconds before trying again",
                         me.name,
                         sptr->name,
                         (int)(sptr->last_nick_change + MAX_NICK_TIME - CurrentTime));
              sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s (nick)",
				     sptr->name, sptr->username, sptr->host, me.name);

              return 0;
            }
#endif
        }
      else
        {
          sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
          if (sptr->user)
            {
              add_history(sptr,1);
	      if(cptr)
	              sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                                 parv[0], nick, (long unsigned)sptr->tsinfo);
	      sptr->user->last_sent = CurrentTime;

              sendto_local_ops_flag(UMODE_NCHANGE,
				    "Nick change: From %s to %s [%s@%s]",
				    parv[0], nick, sptr->username,
				    sptr->host);
            }
        }
    }
  else
    {
      /* Client setting NICK the first time */
      /* This had to be copied here to avoid problems.. */
      strncpy_irc(sptr->name, nick, NICKLEN + 1);
      sptr->tsinfo = CurrentTime;
      if (sptr->user)
        {
          char buf[USERLEN + 1];
          strncpy_irc(buf, sptr->username, USERLEN + 1);
          buf[USERLEN] = '\0';
          /*
          ** USER already received, now we have NICK.
          ** *NOTE* For servers "NICK" *must* precede the
          ** user message (giving USER before NICK is possible
          ** only for local client connection!). register_user
          ** may reject the client and call exit_client for it
          ** --must test this and exit m_nick too!!!
          */
            if (register_user(cptr, sptr, nick, buf) == CLIENT_EXITED)
              return CLIENT_EXITED;
        }
    }

  /*
  **  Finally set new nick name.
  */
  if (!IsHoneypot(sptr) && sptr->name[0])
    del_from_client_hash_table(sptr->name, sptr);

  strncpy_irc(sptr->name, nick, NICKLEN + 1);

  if (!IsHoneypot(sptr))
    add_to_client_hash_table(nick, sptr);

  return 0;
}

/*
** m_nick
**      parv[0] = sender prefix
**      parv[1] = nickname
**      parv[2] = optional hopcount when new user; TS when nick change
**      parv[3] = optional TS
**      parv[4] = optional umode
**      parv[5] = optional username
**      parv[6] = optional hostname
**      parv[7] = optional server
**      parv[8] = client IP address
**      parv[9] = optional ircname
*/
int m_nick(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  aClient* acptr;
  char     nick[NICKLEN + 2];
  char*    s;
  time_t   newts = 0;
  int      sameuser = 0;
  int      fromTS = 0;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }

  if (!IsServer(sptr) && IsServer(cptr) && parc > 2)
    newts = atol(parv[2]);
  else if (IsServer(sptr) && parc > 3)
    newts = atol(parv[3]);
  else parc = 2;

  /*
   * parc == 2 on a normal client sign on (local) and a normal
   *      client nick change
   * parc == 4 on a normal server-to-server client nick change
   *      notice
   * parc == 10 on a normal TS style server-to-server NICK
   *      introduction
   */
  if ((IsServer(sptr)) && (parc < 10))
    {
      /*
       * We got the wrong number of params. Someone is trying
       * to trick us. Kill it. -ThemBones
       * As discussed with ThemBones, not much point to this code now
       * sending a whack of global kills would also be more annoying
       * then its worth, just note the problem, and continue
       * -Dianora
       */
      ts_warn("BAD NICK: %s[%s@%s] on %s (from %s)", parv[1],
                     (parc >= 6) ? parv[5] : "-",
                     (parc >= 7) ? parv[6] : "-",
                     (parc >= 8) ? parv[7] : "-", parv[0]);
      return 0;
    }

  if ((parc >= 7) && !valid_hostname_remote(parv[6]))
    {
      /*
       * Ok, we got the right number of params, but there
       * isn't a single dot in the hostname, which is suspicious.
       * Don't fret about it just kill it. - ThemBones
       */
      ts_warn("BAD HOSTNAME: %s@%s on %s (from %s, via %s)",
                     parv[5], parv[6], parv[7], parv[0], cptr->name);
      /* Kill it now instead of when we see the prefix -- jilles */
      sendto_one(sptr, ":%s KILL %s :%s (Bad hostname)", me.name, parv[1],
                     me.name);
      return 0;
    }

  fromTS = (parc > 6);

  /* A tilde is only allowed if nickprefix is a tilde and it's at the
   * first position */
  if (MyConnect(sptr) && !IsServer(sptr) && (s = strchr(GlobalSetOptions.nickprefix == '~' ? parv[1] + 1 : parv[1], '~')))
    *s = '\0';
  /*
   * nick is an auto, need to terminate the string
   */
  if (MyConnect(sptr) && !IsServer(sptr) && GlobalSetOptions.nickprefix != 0 &&
		  parv[1][0] != GlobalSetOptions.nickprefix &&
		  !isallowednick(sptr, parv[1]))
    {
      nick[0] = GlobalSetOptions.nickprefix;
      strncpy_irc(nick + 1, parv[1], NICKLEN);
    }
  else
    strncpy_irc(nick, parv[1], NICKLEN + 1);
  nick[NICKLEN] = '\0';

  /*
   * if clean_nick_name() returns a null name OR if the server sent a nick
   * name and clean_nick_name() changed it in some way (due to rules of nick
   * creation) then reject it. If from a server and we reject it,
   * and KILL it. -avalon 4/4/92
   */
  if (clean_nick_name(nick) == 0 ||
      (IsServer(cptr) && strcmp(nick, parv[1])))
    {
      sendto_one(sptr, form_str(ERR_ERRONEUSNICKNAME),
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], parv[1]);
      
      if (IsServer(cptr))
        {
          ServerStats->is_kill++;
          sendto_ops_flag(UMODE_DEBUG, "Bad Nick: %s From: %s %s",
			  parv[1], parv[0],
			  get_client_name(cptr, MASK_IP));
          sendto_one(cptr, ":%s KILL %s :%s (%s <- %s)",
                     me.name, parv[1], me.name, parv[1],
                     nick);
          if (sptr != cptr) /* bad nick change */
            {
              sendto_serv_butone(cptr,
                                 ":%s KILL %s :%s (%s <- %s)",
                                 me.name, parv[0], me.name,
				 cptr->name, parv[0]);
              sptr->flags |= FLAGS_KILLED;
              return exit_client(cptr,sptr,&me,"BadNick");
            }
        }
      else
        sendto_ops_flag(UMODE_REJ, "Illegal nick \"%s\" from: %s [%s]",
			parv[1], cptr->name,
			cptr->host);
      return 0;
    }

  /* Cannot change from a non-prefixed nick to a prefixed nick */
  if (MyConnect(sptr) && !IsServer(sptr) && GlobalSetOptions.nickprefix != 0 &&
		  sptr->name[0] != 0 &&
		  sptr->name[0] != GlobalSetOptions.nickprefix &&
		  nick[0] == GlobalSetOptions.nickprefix)
    {
      sendto_one(sptr, form_str(ERR_ERRONEUSNICKNAME),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  if(MyConnect(sptr) && !IsServer(sptr) && !HasUmode(sptr, UMODE_ANYNICK) &&
     find_q_line(nick, sptr->username, sptr->host)) 
    {
      sendto_ops_flag(UMODE_REJ,
		      "Quarantined nick [%s] from user %s",
		      nick, get_client_name(cptr, FALSE));
      sendto_one(sptr, form_str(ERR_ERRONEUSNICKNAME),
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], parv[1]);
      return 0;
    }

  /*
  ** Check against nick name collisions.
  **
  ** Put this 'if' here so that the nesting goes nicely on the screen :)
  ** We check against server name list before determining if the nickname
  ** is present in the nicklist (due to the way the below for loop is
  ** constructed). -avalon
  */
  if ((acptr = find_server(nick)))
    if (MyConnect(sptr))
      {
        sendto_one(sptr, form_str(ERR_NICKNAMEINUSE), me.name,
                   BadPtr(parv[0]) ? "*" : parv[0], nick);
        return 0; /* NICK message ignored */
      }
  /*
  ** acptr already has result from previous find_server()
  */
  /*
   * Well. unless we have a capricious server on the net,
   * a nick can never be the same as a server name - Dianora
   */

  if (acptr)
    {
      /*
      ** We have a nickname trying to use the same name as
      ** a server. Send out a nick collision KILL to remove
      ** the nickname. As long as only a KILL is sent out,
      ** there is no danger of the server being disconnected.
      ** Ultimate way to jupiter a nick ? >;-). -avalon
      */
      sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)",
			    sptr->name, acptr->from->name,
			    cptr->name);
      ServerStats->is_kill++;
      sendto_one(cptr, ":%s KILL %s :Nick collision",
                 me.name, sptr->name);
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick/Server collision");
    }
  

  if (IsHoneypot(sptr) || !(acptr = find_client(nick, NULL)))
    return(nickkilldone(cptr,sptr,parc,parv,newts,nick));  /* No collisions,
                                                            * all clear...
                                                            */

  /*
   * If acptr == sptr, then we have a client doing a nick
   * change between *equivalent* nicknames as far as server
   * is concerned (user is changing the case of his/her
   * nickname or somesuch)
   */
  if (acptr == sptr)
   {
    if (strcmp(acptr->name, nick) != 0)
      /*
      ** Allows change of case in his/her nick
      */
      return(nickkilldone(cptr,sptr,parc,parv,newts,nick)); /* -- go and process change */
    else
      {
        /*
         * This is just ':old NICK old' type thing.
         * Just forget the whole thing here. There is
         * no point forwarding it to anywhere,
         * especially since servers prior to this
         * version would treat it as nick collision.
         */
        return 0; /* NICK Message ignored */
      }
   }
  /*
   * Note: From this point forward it can be assumed that
   * acptr != sptr (point to different client structures).
   */


  /*
   * If the older one is "non-person", the new entry is just
   * allowed to overwrite it. Just silently drop non-person,
   * and proceed with the nick. This should take care of the
   * "dormant nick" way of generating collisions...
   */
  if (IsUnknown(acptr)) 
    {
      if (MyConnect(acptr))
	{
	  exit_client(NULL, acptr, &me, "Overridden");
	  return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
	}
      else
	{
	  if (fromTS && !(acptr->user))
	    {
	      sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick Collision on %s(%s(NOUSER) <- %s!%s@%s)(TS:%s)",
				    acptr->name, acptr->from->name, parv[1], parv[5], parv[6],
				    cptr->name);

#ifndef LOCAL_NICK_COLLIDE
	      sendto_serv_butone(NULL, /* all servers */
				 ":%s KILL %s :%s (%s(NOUSER) <- %s!%s@%s)(TS:%s)",
				 me.name,
				 acptr->name,
				 me.name,
				 acptr->from->name,
				 parv[1],
				 parv[5],
				 parv[6],
				 cptr->name);
#endif

	      acptr->flags |= FLAGS_KILLED;
	      /* Having no USER struct should be ok... */
	      return exit_client(cptr, acptr, &me,
				 "Got TS NICK before Non-TS USER");
	    }
	}
    }
  /*
   * Decide, we really have a nick collision and deal with it
   */
  if (MyConnect(sptr) && !IsServer(sptr))
    {
      /* Just send error reply and ignore the command. */
      sendto_one(sptr, form_str(ERR_NICKNAMEINUSE),
                 /* parv[0] is empty when connecting */
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], nick);
      return 0; /* NICK message ignored */
    }

  /*
   * NICK was coming from a server connection. Means that the same
   * nick is registered for different users by different server.
   * This is either a race condition (two users coming online about
   * same time, or net reconnecting) or just two net fragments becoming
   * joined and having same nicks in use. We cannot have TWO users with
   * same nick--purge this NICK from the system with a KILL... >;)
   *
   * This seemingly obscure test (sptr == cptr) differentiates
   * between "NICK new" (TRUE) and ":old NICK new" (FALSE) forms.
   *
   * Changed to something reasonable like IsServer(sptr)
   * (true if "NICK new", false if ":old NICK new") -orabidoo
   */

  if (IsServer(sptr))
    {
      /* As discussed with chris (comstud) nick kills can
       * be handled locally, provided all NICK's are propogated
       * globally. Just like channel joins are handled.
       *
       * I think I got this right. 
       * -Dianora
       * There are problems with this, due to lag it looks like.
       * backed out for now...
       */
#ifdef LOCAL_NICK_COLLIDE
      /* just propogate it through */
      sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                         parv[0], nick, (long unsigned)sptr->tsinfo);
#endif
      /*
      ** A new NICK being introduced by a neighbouring
      ** server (e.g. message type "NICK new" received)
      */
      if (!newts || !acptr->tsinfo
          || (newts == acptr->tsinfo))
        {
          sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)(both killed)",
				acptr->name, acptr->from->name,
				cptr->name);
          ServerStats->is_kill++;
          sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                     me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(NULL, /* all servers */
			     ":%s KILL %s :Nick collision",
			     me.name, acptr->name);
#endif
          acptr->flags |= FLAGS_KILLED;
          return exit_client(cptr, acptr, &me, "Nick collision");
        }
      else
        {
          sameuser = fromTS && (acptr->user) &&
            irccmp(acptr->username, parv[5]) == 0 &&
            irccmp(acptr->host, parv[6]) == 0;
          if ((sameuser && newts < acptr->tsinfo) ||
              (!sameuser && newts > acptr->tsinfo))
            return 0;
          else
            {
              if (sameuser)
                sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)(older killed)",
				      acptr->name, acptr->from->name,
				      cptr->name);
              else
                sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)(newer killed)",
				      acptr->name, acptr->from->name,
				      cptr->name);
              
              ServerStats->is_kill++;
              sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                         me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	      sendto_serv_butone(sptr, /* all servers but sptr */
				 ":%s KILL %s :Nick collision",
				 me.name, acptr->name);
#endif

              acptr->flags |= FLAGS_KILLED;
              (void)exit_client(cptr, acptr, &me, "Nick collision");
              return nickkilldone(cptr,sptr,parc,parv,newts,nick);
            }
        }
    }

  /*
   * A NICK change has collided (e.g. message type
   * ":old NICK new". This requires more complex cleanout.
   * Both clients must be purged from this server, the "new"
   * must be killed from the incoming connection, and "old" must
   * be purged from all outgoing connections.
   */
  if (!newts || !acptr->tsinfo || (newts == acptr->tsinfo) || !sptr->user)
    {
      sendto_ops_flag(UMODE_SERVNOTICE,"Nick change collision from %s to %s(%s <- %s)(both killed)",
		      sptr->name, acptr->name, acptr->from->name,
		      cptr->name);
      ServerStats->is_kill++;
      sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                 me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
      sendto_serv_butone(NULL, /* KILL old from outgoing servers */
			 ":%s KILL %s :Nick collision",
			 me.name, sptr->name);
#endif

      ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
      sendto_serv_butone(NULL, /* Kill new from incoming link */
			 ":%s KILL %s :Nick collision",
			 me.name, acptr->name);
#endif

      acptr->flags |= FLAGS_KILLED;
      exit_client(NULL, acptr, &me, "Nick collision(new)");
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick collision(old)");
    }
  else
    {
      sameuser = 
	(!irccmp(acptr->username, sptr->username) &&
	 !irccmp(acptr->host, sptr->host));
      if ((sameuser && newts < acptr->tsinfo) ||
          (!sameuser && newts > acptr->tsinfo))
        {
          if (sameuser)
            sendto_ops_flag(UMODE_SERVNOTICE,"Nick change collision from %s to %s(%s <- %s)(older killed)",
			    sptr->name, acptr->name, acptr->from->name,
			    cptr->name);
          else
            sendto_ops_flag(UMODE_SERVNOTICE,"Nick change collision from %s to %s(%s <- %s)(newer killed)",
			    sptr->name, acptr->name, acptr->from->name,
			    cptr->name);
          ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(cptr, /* KILL old from outgoing servers */
			     ":%s KILL %s :Nick collision",
			     me.name, sptr->name);
#endif

          sptr->flags |= FLAGS_KILLED;
          if (sameuser)
            return exit_client(cptr, sptr, &me, "Nick collision(old)");
          else
            return exit_client(cptr, sptr, &me, "Nick collision(new)");
        }
      else
        {
          if (sameuser)
            sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)(older killed)",
				  acptr->name, acptr->from->name,
				  cptr->name);
          else
            sendto_local_ops_flag(UMODE_SERVNOTICE,"Nick collision on %s(%s <- %s)(newer killed)",
				  acptr->name, acptr->from->name,
				  cptr->name);
          
          ServerStats->is_kill++;
          sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                     me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(sptr, /* all servers but sptr */
			     ":%s KILL %s :Nick collision",
			     me.name, acptr->name);
#endif

          acptr->flags |= FLAGS_KILLED;
          exit_client(cptr, acptr, &me, "Nick collision");
          /* goto nickkilldone; */
        }
    }
  return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
}

/* Code provided by orabidoo */
/* a random number generator loosely based on RC5;
   assumes ints are at least 32 bit */
 
unsigned long my_rand()
{
  static unsigned long s = 0, t = 0, k = 12345678;
  int i;
 
  if (s == 0 && t == 0)
    {
      s = (unsigned long)getpid();
      t = (unsigned long)time(NULL);
    }
  for (i=0; i<12; i++)
    {
      s = (((s^t) << (t&31)) | ((s^t) >> (31 - (t&31)))) + k;
      k += s + t;
      t = (((t^s) << (s&31)) | ((t^s) >> (31 - (s&31)))) + k;
      k += s + t;
    }
  return s;
}


/*
 * m_user
 *      parv[0] = sender prefix
 *      parv[1] = username (login name, account)
 *      parv[2] = client host name (used only from other servers)
 *      parv[3] = server host name (used only from other servers)
 *      parv[4] = users real name info
 */
int m_user(aClient* cptr, aClient* sptr, int parc, char *parv[])
{
  char* username;
  char* host;
  char* server;
  char* realname;
 
  if (parc > 2 && (username = strchr(parv[1],'@')))
    *username = '\0'; 
  if (parc < 5 || *parv[1] == '\0' || *parv[2] == '\0' ||
      *parv[3] == '\0' || *parv[4] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], "USER");
      if (IsServer(cptr))
        sendto_ops_flag(UMODE_SERVNOTICE,"bad USER param count for %s from %s",
                       parv[0], cptr->name);
      else
        return 0;
    }

  /* Copy parameters into better documenting variables */

  {
    static char 
      bad_user[] = "bad-user",
      bad_host[] = "bad-host",
      bad_server[] = "bad-server",
      bad_realname[] = "bad-realname";
    username = (parc < 2 || BadPtr(parv[1])) ? bad_user : parv[1];
    host     = (parc < 3 || BadPtr(parv[2])) ? bad_host : parv[2];
    server   = (parc < 4 || BadPtr(parv[3])) ? bad_server : parv[3];
    realname = (parc < 5 || BadPtr(parv[4])) ? bad_realname : parv[4];
  }
  
#ifdef BOTCHECK
  /* Only do bot checks on local connecting clients */
      if(MyClient(cptr))
        cptr->isbot = bot_check(username,host,realname);
#endif

  return do_user(parv[0], cptr, sptr, username, host, server, NULL, realname);
}


/*
 * is_silence : Does the actual check whether sptr is allowed
 *              to send a message to acptr.
 *              Both must be registered persons.
 *              acptr must be a local client.
 * If sptr is silenced by acptr, his message should not be propagated.
 */
int is_silenced(aClient *sptr, aClient *acptr)
{
    Link *lp;
    anUser *user;
    static char sender[HOSTLEN + NICKLEN + USERLEN + 5];

    if (!(acptr->user) || !(lp = acptr->user->silence) ||
        !(user = sptr->user))
        return 0;
    ircsnprintf(sender, HOSTLEN + NICKLEN + USERLEN + 5, "%s!%s@%s", 
		sptr->name, sptr->username, sptr->host);
    for (; lp; lp = lp->next) {
        if (match(lp->value.cp, sender)) {
            return 1;
        }
    }
    return 0;
}

/*
 * m_silence - borrowed from Cyclone
 *      parv[0] = sender prefix
 *      parv[1] = mask (NULL sends the list)
 *
 * Changed by jilles Mar 2005 to disable propagation across servers.
 * This saves a lot of CPU in PRIVMSG/NOTICE handling and closes a
 * non-rate-limited method for users to flood the network.
 */
int m_silence(aClient* cptr, aClient* sptr, int parc, char *parv[])
{
    Link *lp;
    char c = 0, *cp;

    if (!MyClient(sptr))
      return 0;
    if (parc < 2 || *parv[1] == '\0')
      {
	for (lp = sptr->user->silence; lp; lp = lp->next)
	  sendto_one(sptr, form_str(RPL_SILELIST), me.name,
		     sptr->name, sptr->name, lp->value.cp);
	sendto_one(sptr, form_str(RPL_ENDOFSILELIST), me.name, sptr->name);
	return 0;
      }
    cp = parv[1];
    c = *cp;
    if (c == '-' || c == '+')
      cp++;
    else if (!(strchr(cp, '@') || strchr(cp, '.') ||
	       strchr(cp, '!') || strchr(cp, '*')))
      {
	sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, sptr->name, parv[1]);
	return -1;
      }
    else
      c = '+';
    cp = pretty_mask(cp);
    if (c == '-')
      del_silence(sptr, cp);
    else
      add_silence(sptr, cp);
    return 0;
}

static int do_user(char* nick, aClient* cptr, aClient* sptr,
                   char* username, char *host, char *server, char *ip, char *realname)
{
  unsigned int oflags;
  struct User* user;

  assert(0 != sptr);
  assert(sptr->username != username);

  user = make_user(sptr);

  oflags = sptr->flags;

  if (!MyConnect(sptr))
    {
      /*
       * coming from another server, take the server's word for it
       */
      user->server = find_or_add(server);
      strncpy_irc(sptr->host, host, HOSTLEN + 1); 
      strncpy_irc(sptr->dnshost, host, HOSTLEN + 1); 
      strncpy_irc(sptr->origname, sptr->name, HOSTLEN + 1);
    }
  else
    {
      if (!IsUnknown(sptr))
        {
          sendto_one(sptr, form_str(ERR_ALREADYREGISTRED), me.name, nick);
          return 0;
        }
      /*
       * don't take the clients word for it, ever
       *  strncpy_irc(user->host, host, HOSTLEN); 
       */
      user->server = me.name;
    }
  strncpy_irc(sptr->info, realname, REALLEN + 1);
  strip_colour(sptr->info);

  if (ip)
    {
      strncpy_irc(sptr->sockhost, ip, HOSTIPLEN + 1);
      if (strchr(ip, ':'))
#ifdef IPV6
        inetpton(AFINET, ip, &sptr->ip);
#else
        sptr->ip.s_addr = INADDR_NONE; /* can't store IPv6 address */
#endif
      else if (strchr(ip, '.'))
#ifdef IPV6
        inetpton(AFINET, ip, &sptr->ip);
#else
        inet_aton(ip, &sptr->ip);
#endif
      else
#ifdef IPV6
        inetpton(AFINET, "255.255.255.255", &sptr->ip); /* bad address, go away */
#else
        inet_aton("255.255.255.255", &sptr->ip); /* bad address, go away */
#endif
    }

  if (sptr->name[0]) /* NICK already received, now I have USER... */
    return register_user(cptr, sptr, sptr->name, username);
  else
    {
      if (!IsGotId(sptr)) 
        {
          /*
           * save the username in the client
           */
          strncpy_irc(sptr->username, username, USERLEN + 1);
        }
    }
  return 0;
}

/*
 * user_mode - set current users mode
 *
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int user_mode(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  int   flag;
  int   i;
  char  **p, *m;
  aClient *acptr;
  int   what;
  char  *m2;
  int   ircophack, restrictedmodes;
  int   badflag = NO;   /* Only send one bad flag notice -Dianora */
  int   block_notid_notice = NO;
  char  buf[BUFSIZE];
  user_modes bad_umodes, old_umodes, granted_umodes, stripped_umodes;

  what = MODE_ADD;
  ircophack = 0;
  restrictedmodes = 0;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "MODE");
      return 0;
    }

  if (!(acptr = find_person(parv[1], NULL)))
    {
      if (MyConnect(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[1]);
      return 0;
    }

  if (IsServer(sptr) || sptr != acptr || acptr->from != sptr->from)
    {
      if (sptr != acptr && HasUmode(sptr, UMODE_CHANGEOTHER))
        {
          ircophack = 1;
          
          /* Forward it along. */
          if (!MyClient(acptr))
            {
              if (MyClient(sptr))
                for (p = &parv[2]; p && *p; p++ )
                  {
                    for (m = *p, m2 = *p; *m; m++)
		      switch(*m)
			{
			case '+':
			  what = MODE_ADD;
			  *m2++ = *m;
			  break;
			case '-' :
			  what = MODE_DEL;
			  *m2++ = *m;
			  break;
			default:
			  {
			    int mode = user_modes_from_c_to_bitmask[(unsigned char)*m];
			    if (HasUmode(sptr,UMODE_GRANT))
			      {
				/* OK, we have to check these against allowed_umodes here */
				if (TestBit(sptr->allowed_umodes, mode))
				  *m2++ = *m;
			      }
			    else
			      /* We're not doing any magic. Send it on. */
			      *m2++ = *m;
			  }
			  break;
			}
		    *m2 = 0;
                  }
              sendto_serv_butone(cptr, ":%s MODE %s %s", parv[0],
				 parv[1], ((parc < 3) ? "" : parv[2]));
              return 0;
            }
        }
      else if (IsServer(sptr))
	ircophack = 1; /* Remote servers are trusted to set sane umodes. Illicit servers must not be allowed to attach */
      else if (IsServer(cptr))
        sendto_ops_flag(UMODE_SERVNOTICE,
			"%s issuing MODE for User %s From %s!%s",
			me.name, parv[1],
			cptr->name, sptr->name);
      else
        sendto_one(sptr, form_str(ERR_USERSDONTMATCH),
                   me.name, parv[0]);

      if (!ircophack)
        return 0;
    }

  if (parc < 3)
    {
      m = buf;
      *m++ = '+';

      for (i = 0; user_mode_table[i].letter && (m - buf < BUFSIZE - 4);i++)
        if (TestBit(acptr->umodes, user_mode_table[i].mode))
          *m++ = user_mode_table[i].letter;
      *m = '\0';

      if (ircophack)
	{
	  if (IsClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :*** User mode for %s is %s",
		       me.name, sptr->name, acptr->name, buf);
	}
      else
        sendto_one(sptr, form_str(RPL_UMODEIS), me.name, parv[0], buf);

      return 0;
    }

  CopyUmodes(old_umodes, acptr->umodes);
  ClearBitfield(granted_umodes);
  ClearBitfield(stripped_umodes);

  /*
   * parse mode change string(s)
   */
  for (p = &parv[2]; p && *p; p++ )
    for (m = *p; *m; m++)
      {
	switch(*m)
	  {
	  case '+' :
	    what = MODE_ADD;
	    break;
	  case '-' :
	    what = MODE_DEL;
	    break;        
	    
	  /*case 'O': */case 'o' :
	    if(what == MODE_ADD)
	      {
		/* I'm going to disable this for now, we do things differently
		 *  --asuffield
		 */
		if (MyClient(sptr))
		  break;
		if (ircophack)
		  break;

		if(IsServer(cptr) && !HasUmode(acptr,UMODE_OPER))
		  {
		    ++Count.oper;
		    SetUmode(acptr,UMODE_OPER);
		    SetBit(acptr->allowed_umodes, UMODE_OPER);
		    if (!MyClient(acptr))
		      acptr->from->serv->umode_count[UMODE_OPER]--;
		  }
	      }
	    else
	      {
		/* Only decrement the oper counts if an oper to begin with
		 * found by Pat Szuta, Perly , perly@xnet.com 
		 */

		if(!HasUmode(acptr,UMODE_OPER))
		  break;

		ClearBit(acptr->umodes, UMODE_OPER);
		/*		ClearBit(acptr->umodes, UMODE_LOCOP);*/

		Count.oper--;

		{
		  int identified = TestBit(acptr->umodes, UMODE_IDENTIFIED);
		  user_modes removed_umodes;
		  AndNotUmodes(removed_umodes, acptr->umodes, acptr->allowed_umodes);
		  CopyUmodes(acptr->allowed_umodes, user_umodes);
		  AndUmodes(acptr->umodes, acptr->umodes, acptr->allowed_umodes);
		  if (identified)
		    SetBit(acptr->umodes, UMODE_IDENTIFIED);
                  if (!MyClient(acptr))
                    for (i = 0; user_mode_table[i].letter; i++)
                      if (TestBit(removed_umodes, user_mode_table[i].mode))
                        acptr->from->serv->umode_count[user_mode_table[i].mode]--;
		}

		if (MyConnect(acptr))
		  {
		    fdlist_delete(acptr->fd, FDL_OPER | FDL_BUSY);

		    det_confs_butmask(acptr, ~CONF_OPERATOR);
#if 0
		    aClient *prev_cptr = (aClient *)NULL;
		    aClient *cur_cptr = oper_cptr_list;

		    while(cur_cptr)
		      {
			if(acptr == cur_cptr) 
			  {
			    if(prev_cptr)
			      prev_cptr->next_oper_client = cur_cptr->next_oper_client;
			    else
			      oper_cptr_list = cur_cptr->next_oper_client;
			    cur_cptr->next_oper_client = (aClient *)NULL;
			    break;
			  }
			else
			  prev_cptr = cur_cptr;
			cur_cptr = cur_cptr->next_oper_client;
		      }
#endif
		  }
	      }
	    break;
	  case 'e' :
	    if(IsServer(sptr) || (acptr == sptr && IsServer(cptr)))
	      {
		if (what==MODE_ADD)
		  {
		    SetBit(acptr->umodes,UMODE_IDENTIFIED);
		    if (!MyClient(acptr))
		      acptr->from->serv->umode_count[UMODE_IDENTIFIED]++;
		  }
		else
		  {
		    ClearBit(acptr->umodes,UMODE_IDENTIFIED);
		    if (!MyClient(acptr))
		      acptr->from->serv->umode_count[UMODE_IDENTIFIED]--;
		  }
	      }
	    break;

	    /* we may not get these,
	     * but they shouldnt be in default
	     */
	  case ' ' :
	  case '\n' :
	  case '\r' :
	  case '\t' :
	    break;
	  default :
	    if((flag = user_modes_from_c_to_bitmask[(unsigned char)*m]))
	      {
		if (flag == UMODE_OPER)
		  break;
		if (GlobalSetOptions.noidprivmsg &&
			sptr == acptr && MyClient(sptr) &&
			flag == UMODE_BLOCK_NOTID && what == MODE_DEL &&
			!HasUmode(sptr, UMODE_DONTBLOCK) &&
			!HasUmode(sptr, UMODE_IDENTIFIED) &&
			sptr->user->servlogin[0] == '\0')
		  {
		    if (!block_notid_notice)
		      {
			sendto_one(sptr, ":%s NOTICE %s :*** -E will have no effect until you identify", me.name, sptr->name);
			block_notid_notice = YES;
		      }
		  }
		/* OK, let's handle granting stuff */
		/* Grant a mode if we're supposed to, if it's not +* itself, and if we are either
		 *  handling a remote grant or it's allowed locally
		 */
		if ((sptr != acptr) && HasUmode(sptr,UMODE_GRANT) && (flag != UMODE_GRANT) &&
		    (!MyClient(sptr) || TestBit(sptr->allowed_umodes, flag)))
		  {
		    /* We're granting, rather than just normally changing */
		    if (what == MODE_ADD)
		      {
			if (!TestBit(acptr->allowed_umodes, flag) && !TestBit(user_umodes, flag))
			  {
			    SetBit(acptr->allowed_umodes, flag);
			    SetBit(granted_umodes, flag);
			  }
		      }
		    else
		      {
			if (!TestBit(user_umodes, flag) && TestBit(acptr->allowed_umodes, flag))
			  {
			    ClearBit(acptr->allowed_umodes, flag);
			    SetBit(stripped_umodes, flag);
			  }
		      }
		  }
		if (what == MODE_ADD)
		  {
		    SetBit(acptr->umodes, flag);
		    if (!MyClient(acptr))
		      acptr->from->serv->umode_count[flag]++;
		  }
		else
		  {
		    ClearBit(acptr->umodes, flag);
		    if (!MyClient(acptr))
		      acptr->from->serv->umode_count[flag]--;
		  }
	      }
	    else
	      {
		if (MyConnect(sptr))
		  badflag = YES;
	      }
	    break;
	  }
      }

  /* Find which ones shouldn't have been set, and unset them (and warn)
   * But only if this is my client. A server can set any umode, even
   * though they will be lost the next time the client changes their
   * umode. But that situation is better than an umode desync -- jilles
   */
  if (MyClient(acptr) && !IsServer(sptr))
    {
      AndNotUmodes(bad_umodes, acptr->umodes, acptr->allowed_umodes);
      /* Special case, this isn't really a umode... */
      ClearBit(bad_umodes, UMODE_IDENTIFIED);
      if (AnyBits(bad_umodes))
	{
	  int identified = TestBit(acptr->umodes, UMODE_IDENTIFIED);
	  AndUmodes(acptr->umodes, acptr->umodes, acptr->allowed_umodes);
	  if (identified)
	    SetBit(acptr->umodes, UMODE_IDENTIFIED);
	  if (SeesOperMessages(sptr) || sptr != acptr)
	    sendto_one(sptr, ":%s NOTICE %s :*** Privilege(s) +%s not granted", me.name, parv[0],
		       umodes_as_string(&bad_umodes));
	  else
	    badflag = YES;
	}
    } 

  m = buf;
  *m++ = '+';
  if (AnyBits(granted_umodes))
    {
      sendto_one(acptr, ":%s NOTICE %s :%s has bestowed the power of +%s onto you",
		      me.name, acptr->name, sptr->name, umodes_as_string(&granted_umodes));
      sendto_ops_flag(UMODE_SERVNOTICE, "%s has bestowed the power of +%s onto %s",
		      sptr->name, umodes_as_string(&granted_umodes), acptr->name);
      logprintf(L_NOTICE, "%s bestowed the power of +%s onto %s",
	  sptr->name, umodes_as_string(&granted_umodes), acptr->name);

      for (i = 0; user_mode_table[i].letter && (m - buf < BUFSIZE - 4);i++)
        if (TestBit(granted_umodes, user_mode_table[i].mode))
          *m++ = user_mode_table[i].letter;
    }

  *m++ = '-';
  if (AnyBits(stripped_umodes))
    {
      sendto_one(acptr, ":%s NOTICE %s :%s has stripped you of the power of +%s",
		      me.name, acptr->name, sptr->name, umodes_as_string(&stripped_umodes));
      sendto_ops_flag(UMODE_SERVNOTICE, "%s has stripped %s of the power of +%s",
		      sptr->name, acptr->name, umodes_as_string(&stripped_umodes));
      logprintf(L_NOTICE, "%s stripped %s of the power of +%s",
	  sptr->name, acptr->name, umodes_as_string(&stripped_umodes));

      for (i = 0; user_mode_table[i].letter && (m - buf < BUFSIZE - 4);i++)
        if (TestBit(stripped_umodes, user_mode_table[i].mode))
          *m++ = user_mode_table[i].letter;
    }

  *m = '\0';
  if (AnyBits(granted_umodes) || AnyBits(stripped_umodes))
    sendto_serv_butone(NULL, ":%s OPER %s %s",
		       me.name, acptr->name, buf);
  
  if(badflag)
    {
      sendto_one(sptr, form_str(ERR_UMODEUNKNOWNFLAG), me.name, parv[0]);
    }

  if (!TestBit(old_umodes, UMODE_INVISIBLE) && IsInvisible(acptr))
    ++Count.invisi;
  if (TestBit(old_umodes, UMODE_INVISIBLE) && !IsInvisible(acptr))
    --Count.invisi;
  /*
   * compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */
  if(ircophack)
    send_umode_out(cptr, sptr, acptr, &old_umodes);
  else
    send_umode_out(cptr, sptr, sptr, &old_umodes);

  return 0;
}

/* Rewrote to use sane umode functions
 *  -- asuffield
 *
 * cptr == local origin of whatever caused this function to be called
 * from == client that caused the change
 * sptr == client that is changing
 */
void send_umode_out(struct Client *cptr,
		    struct Client *from,
		    struct Client *sptr,
                    user_modes *old)
{
  char *mode_string = umode_difference(old, &sptr->umodes);

  if (from != sptr && MyClient(sptr) && !IsServer(from)) /* ircophack stuff */
    {
      if (*mode_string)
        sendto_one(from, ":%s NOTICE %s :*** Changed mode for %s: %s",
                   me.name, from->name, sptr->name, mode_string);
      else
        {
	  /* This case makes this function rather complicated :( -- jilles */
          sendto_one(from, ":%s NOTICE %s :*** Mode for %s unchanged",
                     me.name, from->name, sptr->name);
          return;
        }
      /* send this from the target everywhere -- it was not
       * processed earlier on -- jilles */
      sendto_serv_butone(NULL, ":%s MODE %s :%s", sptr->name, sptr->name, mode_string);
    }
  else if (*mode_string == '\0')
    return;
  else
    sendto_serv_butone(cptr, ":%s MODE %s :%s", from->name, sptr->name, mode_string);

  /* Send the target their mode change (it is now certainly nonempty) */
  if (MyClient(sptr))
	sendto_one(sptr, ":%s MODE %s :%s", from->name, sptr->name, mode_string);
}

#ifdef BOTCHECK
/**
 ** bot_check(host)
 **   Reject a bot based on a fake hostname...
 **           -Taner
 **/
static int bot_check(char *username, char* host, char* realname)
{
/*
 * Eggdrop Bots:        "USER foo 1 1 :foo"
 * Vlad, Com, joh Bots: "USER foo null null :foo"
 * Annoy/OJNKbots:      "user foo . . :foo"   (disabled)
 * Spambots that are based on OJNK: "user foo x x :foo"
 */
  if (!strcmp(host,"1")) return 1;
  if (!strcmp(host,"null")) return 2;
  if (!strcmp(host, "x")) return 3;
  if (!strcmp(realname, "sub7server")) return 5;
  if (!strcmp(username, "username")) return 6;

  return 0;
}
#endif

int del_silence(struct Client *sptr, char *mask)
{
    Link **lp, *tmp;

    for (lp = &(sptr->user->silence); *lp; lp = &((*lp)->next))
        if (strcasecmp(mask, (*lp)->value.cp) == 0) {
            tmp = *lp;
            *lp = tmp->next;
            MyFree(tmp->value.cp);
            free_link(tmp);
            return 0;
        }

    return -1;
}

static int add_silence(struct Client *sptr, char *mask)
{
    Link *lp;
    int cnt = 0, len = 0;

    for (lp = sptr->user->silence; lp; lp = lp->next) {
        len += strlen(lp->value.cp);
        if (MyClient(sptr)) {
            if ((len > MAXSILELENGTH) || (++cnt >= MAXSILES)) {
                sendto_one(sptr, form_str(ERR_SILELISTFULL), me.name, sptr->name, mask);
                return -1;
            } else {
                if (match(lp->value.cp, mask))
                    return -1;
            }
        } else if (!strcasecmp(lp->value.cp, mask)) {
            return -1;
        }
    }

    lp = make_link();
    DupString(lp->value.cp, mask);
    lp->next = sptr->user->silence;
    sptr->user->silence = lp;

    return 0;
}

/*
 * Change a user's nick, ident, and/or hostname while updating their login info,
 *   and broadcasting a SIGNON
 * Use target server to be able to target unknowns in the future
 *
 * parv[0] = source
 * parv[1] = target server
 * parv[2] = target nick
 * parv[3] = login id
 * parv[4] = new nick
 * parv[5] = new ident
 * parv[6] = new hostname
 * parv[7] = optional new allowed nicks
 */
int m_svslogin(struct Client *client_p, struct Client *source_p,
		int parc, char *parv[])
{
	struct Client *server_p, *target_p, *override_p;
	char *nick_parv[7];
	char nick[NICKLEN + 2];
	char ts[24];
	char *p;

	if (!IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_UNKNOWNCOMMAND),
				me.name, parv[0], "SVSLOGIN");
		return 0;
	}

	if (parc < 7)
	{
		sendto_ops_flag(UMODE_DEBUG, "%s sent SVSLOGIN with too few parameters", source_p->name);
		return 0;
	}

	server_p = find_server(parv[1]);
	if (server_p == NULL)
		return 0;

	strncpy_irc(nick, parv[4], NICKLEN + 1);
	if (clean_nick_name(nick) == 0)
	{
		sendto_ops_flag(UMODE_SERVNOTICE, "Invalid SVSLOGIN nick ignored: from %s for %s to %s",
				source_p->name, parv[2], parv[4]);
		return 0;
	}

	if (IsMe(server_p))
	{
		target_p = find_client(parv[2], NULL);
		if (target_p == NULL || IsServer(target_p))
			return 0;

		if (!MyConnect(target_p))
		{
			sendto_ops_flag(UMODE_DEBUG, "Server %s sent me SVSLOGIN for %s which is not my connection", source_p->name, target_p->name);
			return 0;
		}

		if (!IsPerson(target_p))
		{
			sendto_ops_flag(UMODE_DEBUG, "Server %s sent me SVSLOGIN for unknown %s which is not supported yet", source_p->name, target_p->name);
			return 0;
		}

		if (!valid_hostname_remote(parv[6]))
		{
			if (IsServer(client_p))
				sendto_ops_flag(UMODE_SERVNOTICE,
					"Invalid SVSLOGIN host ignored: [%s] for %s from %s",
					parv[6], target_p->name, source_p->name);
			return 0;
		}

		override_p = find_client(nick, NULL);
		if (override_p && override_p != target_p)
		{
			if (IsServer(override_p))
			{
				sendto_ops_flag(UMODE_DEBUG, "%s tried to SVSLOGIN server %s", source_p->name, override_p->name);
				return 0;
			}

			if (IsPerson(override_p))
			{
				sendto_local_ops_flag(UMODE_SKILL, "SVSLOGIN overridden: %s by %s for %s", override_p->name, source_p->name, target_p->name);
				sendto_serv_butone(NULL, ":%s KILL %s :%s Nick regained by %s for %s",
					   me.name, override_p->name, me.name,
					   source_p->name, target_p->name);
			}
			if(MyClient(override_p))
				sendto_one(override_p, ":%s KILL %s :%s Nick regained by %s for %s",
					   me.name, override_p->name, me.name,
					   source_p->name, target_p->name);
			override_p->flags |= FLAGS_KILLED;
			exit_client(NULL, override_p, &me, "Nick regained by services");
		}

		if (parc > 7 && parv[7][0] != '*')
		{
			if (target_p->allownicks != NULL)
			{
				MyFree(target_p->allownicks);
				target_p->allownicks = NULL;
			}
			if (parv[7][0] != '\0' && parv[7][0] != ',')
			{
				expect_malloc;
				p = MyMalloc(strlen(parv[7]) + 2);
				malloc_log("m_svslogin() allocating %d bytes at %p", strlen(parv[7]) + 2, p);
				strcpy(p, parv[7]);
				target_p->allownicks = p;
				while (*p != '\0')
				{
					while (*p != ',' && *p != '\0')
						p++;
					if (*p == ',')
						*p = '\0', p++;
				}
				p[1] = '\0';
			}
		}
		if (GlobalSetOptions.nickprefix != 0 &&
				nick[0] != GlobalSetOptions.nickprefix &&
				!isallowednick(target_p, nick))
		{
			/* Oops! */
			/* Let's not do anything more than sending a notice,
			 * to avoid killing the user -- jilles */
			char allowed[512];
			p = target_p->allownicks;
			if (p != NULL)
			{
				allowed[0] = '\0';
				while (*p != '\0')
				{
					if (allowed[0] != '\0')
						strcat(allowed, ",");
					strcat(allowed, p);
					if (strlen(allowed) > 400)
						strcat(allowed, ",<TRUNCATED>");
					p += strlen(p) + 1;
				}
			}
			else
				strcpy(allowed, "<NONE>");
			sendto_one(target_p, ":%s NOTICE %s :Your new nick %s does not match any of your allowed nicks (%s), expect strange behaviour", me.name, target_p->name, nick, allowed);
			sendto_ops_flag(UMODE_SERVNOTICE, "Server %s changed nick of user %s to %s not matching any allowed nick (%s)", source_p->name, target_p->name, nick, allowed);
		}

		nick_parv[0] = target_p->name;
		nick_parv[1] = parv[3];
		nick_parv[2] = nick;
		nick_parv[3] = parv[5];
		nick_parv[4] = parv[6];
		ircsnprintf(ts, 24, "%lu", (unsigned long)CurrentTime);
		nick_parv[5] = ts;
		nick_parv[6] = NULL;
		m_signon(&me, target_p, 6, nick_parv);

		if (*target_p->user->servlogin)
			sendto_one(target_p, form_str(RPL_LOGGEDIN), me.name, target_p->name, parv[3], parv[5], parv[6], parv[3], parv[5], parv[6]);
		else
			sendto_one(target_p, form_str(RPL_LOGGEDOUT), me.name, target_p->name, parv[3], parv[5], parv[6], parv[3], parv[5], parv[6]);
	}else{
		sendto_prefix_one(server_p, source_p, ":%s SVSLOGIN %s %s %s %s %s %s%s%s",
				   parv[0], server_p->name, parv[2], parv[3], nick, parv[5], parv[6], parc > 7 ? " " : "", parc > 7 ? parv[7] : "");
	}

	return 0;
}

/*
 * Make broadcasted changes to a user's login id, nick, user, and hostname.
 *
 * parv[0] = source
 * parv[1] = login id
 * parv[2] = new nick
 * parv[3] = new ident
 * parv[4] = new hostname
 * parv[5] = ts
 */
int m_signon(struct Client *client_p, struct Client *source_p,
		int parc, char *parv[])
{
	struct Client *override_p;
	char *nick_parv[2];
	int newts, sameuser = 0;
	int nickchange = 0, identchange = 0, hostchange = 0;
	user_modes old_umodes;

	if (!IsMe(client_p) && !IsServer(client_p))
	{
		sendto_one(source_p, form_str(ERR_UNKNOWNCOMMAND),
				me.name, parv[0], "SIGNON");
		return 0;
	}

	if(parc < 6)
	{
		sendto_ops_flag(UMODE_DEBUG, "%s sent SIGNON with too few parameters", source_p->name);
		return 0;
	}

	if (!valid_hostname_remote(parv[4]))
		return 0;

	
	if (!valid_username(parv[3]) || strlen(parv[3]) > USERLEN)
		return 0;
	
	newts = atol(parv[5]);
	if((override_p = find_client(parv[2], NULL)) && override_p != source_p)
	{
		if (IsUnknown(override_p))
		{
			exit_client(NULL, override_p, &me, "Overridden");
		}
		else if(!newts || !override_p->tsinfo || (newts == override_p->tsinfo))
		{
			sendto_ops_flag(UMODE_SERVNOTICE,"Signon collision from %s to %s(%s <- %s)(both killed)",
					source_p->name, override_p->name, override_p->from->name,
					client_p->name);
			ServerStats->is_kill++;
			sendto_one(override_p, form_str(ERR_NICKCOLLISION),
					me.name, override_p->name, override_p->name);

#ifndef LOCAL_NICK_COLLIDE
			sendto_serv_butone(NULL, /* KILL old from outgoing servers */
					":%s KILL %s :Nick collision",
					me.name, source_p->name);
#endif

			ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
			sendto_serv_butone(NULL, /* Kill new from incoming link */
					":%s KILL %s :Nick collision",
					me.name, override_p->name);
#endif

			override_p->flags |= FLAGS_KILLED;
			exit_client(NULL, override_p, &me, "Nick collision(new)");
			source_p->flags |= FLAGS_KILLED;
			return exit_client(client_p, source_p, &me, "Nick collision(old)");
		}else{
			/* Match the user@host against the values we /will/ use, not the ones we have now */
			sameuser = (!irccmp(override_p->username, parv[3]) &&
					!irccmp(override_p->host, parv[4]));
			if ((sameuser && newts < override_p->tsinfo) ||
					(!sameuser && newts > override_p->tsinfo))
			{
				if (sameuser)
					sendto_ops_flag(UMODE_SERVNOTICE,"Signon collision from %s to %s(%s <- %s)(older killed)",
							source_p->name, override_p->name, override_p->from->name,
							client_p->name);
				else
					sendto_ops_flag(UMODE_SERVNOTICE,"Signon collision from %s to %s(%s <- %s)(newer killed)",
							source_p->name, override_p->name, override_p->from->name,
							client_p->name);
				ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
				sendto_serv_butone(client_p, /* KILL old from outgoing servers */
						":%s KILL %s :Nick collision",
						me.name, source_p->name);
#endif

				source_p->flags |= FLAGS_KILLED;
				if (sameuser)
					return exit_client(client_p, source_p, &me, "Nick collision(old)");
				else
					return exit_client(client_p, source_p, &me, "Nick collision(new)");
			}else{
				if (sameuser)
					sendto_local_ops_flag(UMODE_SERVNOTICE,"Signon collision on %s(%s <- %s)(older killed)",
							override_p->name, override_p->from->name,
							client_p->name);
				else
					sendto_local_ops_flag(UMODE_SERVNOTICE,"Signon collision on %s(%s <- %s)(newer killed)",
							override_p->name, override_p->from->name,
							client_p->name);

				ServerStats->is_kill++;
				sendto_one(override_p, form_str(ERR_NICKCOLLISION),
						me.name, override_p->name, override_p->name);

#ifndef LOCAL_NICK_COLLIDE
				sendto_serv_butone(source_p, /* all servers but sptr */
						":%s KILL %s :Nick collision",
						me.name, override_p->name);
#endif

				override_p->flags |= FLAGS_KILLED;
				exit_client(client_p, override_p, &me, "Nick collision");
			}
		}
	}

	/* flags indicating anything changed on these */
	nickchange = strcmp(source_p->name, parv[2]);
	identchange = strcmp(source_p->username, parv[3]);
	hostchange = strcmp(source_p->host, parv[4]);

	strncpy_irc(source_p->host, parv[4], HOSTLEN + 1);
	strncpy_irc(source_p->username, parv[3], USERLEN + 1);
	strncpy_irc(source_p->user->servlogin,
			irccmp(parv[1], SERVLOGIN_NONE) ? parv[1] : "",
			SERVLOGINLEN + 1);

	/* Let's do -e the traditional way -- jilles */
	if (MyClient(source_p) && irccmp(parv[2], source_p->name))
	  {
	    CopyUmodes(old_umodes, source_p->umodes);
	    ClearUmode(source_p, UMODE_IDENTIFIED);
	    send_umode_out(client_p, source_p, source_p, &old_umodes);
	  }

	sendto_match_cap_servs(NULL, client_p, CAP_SIGNON, NOCAPS,
			":%s SIGNON %s %s %s %s %s",
			source_p->name, parv[1], parv[2], parv[3], parv[4], parv[5]);
	if (hostchange)
		sendto_match_cap_servs(NULL, client_p, NOCAPS, CAP_SIGNON,
			":%s SETHOST %s %s",
			source_p->servptr->name, source_p->name, parv[4]);
	if (identchange)
		sendto_match_cap_servs(NULL, client_p, NOCAPS, CAP_SIGNON,
			":%s SETIDENT %s %s",
			source_p->servptr->name, source_p->name, parv[3]);
	if (nickchange)
	{
		sendto_match_cap_servs(NULL, client_p, NOCAPS, CAP_SIGNON,
			":%s NICK %s %s",
			source_p->name, parv[2], parv[5]);

		nick_parv[0] = source_p->name;
		nick_parv[1] = NULL;
		/* Use a NULL cptr to indicate that this NICK should not be broadcast */
		nickkilldone(NULL, source_p, 1, nick_parv,
			newts, parv[2]);
	}

	return 0;
}

