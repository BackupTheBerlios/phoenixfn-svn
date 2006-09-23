/************************************************************************
 *   IRC - Internet Relay Chat, src/m_set.c
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
 *   
 */
#include "m_commands.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "listener.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "common.h"   /* for NO */
#include "channel.h"  /* for server_was_split */
#include "s_log.h"
#include "umodes.h"

#include <stdlib.h>  /* atoi */

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

#define WHERE_SINGLE 0
#define WHERE_GLOBAL 1
#define WHERE_VISIBLE 2
#define WHERE_HIDDEN 3

/*
 * set_parser - find the correct int. to return 
 * so we can switch() it.
 * KEY:  0 - MAX
 *       1 - AUTOCONN
 *       2 - IDLETIME
 *       3 - FLUDNUM
 *       4 - FLUDTIME
 *       5 - FLUDBLOCK
 *       6 - DRONETIME
 *       7 - DRONECOUNT
 *       8 - SPLITDELAY
 *       9 - SPLITNUM
 *      10 - SPLITUSERS
 *      11 - SPAMNUM
 *      12 - SPAMTIME
 * - rjp
 */

#define TOKEN_MAX 0
#define TOKEN_AUTOCONN 1
#define TOKEN_IDLETIME 2
#define TOKEN_FLUDNUM 3
#define TOKEN_FLUDTIME 4
#define TOKEN_FLUDBLOCK 5
#define TOKEN_DRONETIME 6
#define TOKEN_DRONECOUNT 7
#define TOKEN_SPLITDELAY 8
#define TOKEN_SPLITNUM 9
#define TOKEN_SPLITUSERS 10
#define TOKEN_SPAMNUM 11
#define TOKEN_SPAMTIME 12
#define TOKEN_LOG 13
#define TOKEN_KILLNUM 14
#define TOKEN_LISTEN 15
#define TOKEN_BAD 16

static const char *set_token_table[] = {
  "MAX",
  "AUTOCONN",
  "IDLETIME",
  "FLUDNUM",
  "FLUDTIME",
  "FLUDBLOCK",
  "DRONETIME",
  "DRONECOUNT",
  "SPLITDELAY",
  "SPLITNUM",
  "SPLITUSERS",
  "SPAMNUM",
  "SPAMTIME",
  "LOG",
  "KILLNUM",
  "LISTEN",
  NULL
};

static int set_parser(const char* parsethis)
{
  int i;

  for( i = 0; set_token_table[i]; i++ )
    {
      if(!irccmp(set_token_table[i], parsethis))
        return i;
    }
  return TOKEN_BAD;
}

/* string: "%s has changed ITEM on %s to %i (%i)" */
static int set_notice(int global, struct Client *sptr, const char *string,
		int param1, int param2)
{
	const char *where;

	if (global)
	{
		switch (global)
		{
			case WHERE_GLOBAL: where = "*"; break;
			case WHERE_VISIBLE: where = "<visible>"; break;
			case WHERE_HIDDEN: where = "<hidden>"; break;
			default: where = "<unknown>";
		}
		sendto_local_ops_flag(UMODE_SERVNOTICE, string, sptr->name, where, param1, param2);
	}
	else
		sendto_ops_flag(UMODE_SERVNOTICE, string, sptr->name, me.name, param1, param2);

	return 1; /* to simplify m_set() -- jilles */
}

/* returns 1 if successful (propagate if global) */
static int do_set(struct Client *cptr, struct Client *sptr, char *command, char *param, char *param2, int global)
{
  int cnum;

  if (command != NULL)
    {
      cnum = set_parser(command);
/* This strcasecmp crap is annoying.. a switch() would be better.. 
 * - rjp
 */
      switch(cnum)
        {
        case TOKEN_MAX:
          if (param != NULL)
            {
              int new_value = atoi(param);
              if (global)
                {
                  sendto_one(sptr,
                             ":%s NOTICE %s :You cannot set MAXCLIENTS globally",
                             me.name, sptr->name);
                  return 0;
                }
              if (new_value > MASTER_MAX)
                {
                  sendto_one(sptr,
                             ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
                             me.name, sptr->name, MASTER_MAX);
                  return 0;
                }
              if (new_value < 32)
                {
                  sendto_one(sptr, ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
                             me.name, sptr->name, MAXCLIENTS, highest_fd);
                  return 0;
                }
              MAXCLIENTS = new_value;
              set_notice(global, sptr, "%s has changed MAXCLIENTS on %s to %d (%d current)",
				     MAXCLIENTS, Count.local);
              return 1;
            }
          sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
                     me.name, sptr->name, MAXCLIENTS, Count.local);
          return 1;
          break;

        case TOKEN_AUTOCONN:
          if(param2 != NULL)
            {
              int newval = atoi(param2);

              if(!irccmp(param,"ALL"))
                {
                  set_notice(global, sptr,
					 "%s has changed AUTOCONN ALL on %s to %i",
					 newval, 0);
                  GlobalSetOptions.autoconn = newval;
                }
              else
                set_autoconn(sptr,sptr->name,param,newval);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
                         me.name, sptr->name, GlobalSetOptions.autoconn);
            }
          return 1;
          break;

#ifdef FLUD
          case TOKEN_FLUDNUM:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDNUM must be > 0",
                               me.name, sptr->name);
                    return 0;
                  }       
                FLUDNUM = newval;
#ifdef DEATHFLUD
                if (FLUDNUM >= KILLNUM) KILLNUM = FLUDNUM + 2;
#endif
                set_notice(global, sptr, "%s has changed FLUDNUM on %s to %i",
				       FLUDNUM, 0);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDNUM is currently %i",
                           me.name, sptr->name, FLUDNUM);
              }
            return 1;
            break;

          case TOKEN_FLUDTIME:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDTIME must be > 0",
                               me.name, sptr->name);
                    return 0;
                  }       
                FLUDTIME = newval;
                set_notice(global, sptr, "%s has changed FLUDTIME on %s to %i",
				       FLUDTIME, 0);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDTIME is currently %i",
                           me.name, sptr->name, FLUDTIME);
              }
            return 1;
            break;

          case TOKEN_FLUDBLOCK:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK must be >= 0",
                               me.name, sptr->name);
                    return 0;
                  }       
                FLUDBLOCK = newval;
                if(FLUDBLOCK == 0)
                  {
                    set_notice(global, sptr, "%s has disabled flud detection/protection",
					   0, 0);
                  }
                else
                  {
                    set_notice(global, sptr, "%s has changed FLUDBLOCK on %s to %i",
					   FLUDBLOCK, 0);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK is currently %i",
                           me.name, sptr->name, FLUDBLOCK);
              }
            return 1;       
            break;

#ifdef DEATHFLUD
          case TOKEN_KILLNUM:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval <= FLUDNUM)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :KILLNUM must be > FLUDNUM",
                               me.name, sptr->name);
                    return 0;
                  }       
                KILLNUM = newval;
                set_notice(global, sptr, "%s has changed KILLNUM on %s to %i, fear",
				       KILLNUM, 0);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :KILLNUM is currently %i",
                           me.name, sptr->name, KILLNUM, 0);
              }
            return 1;
            break;
#endif /* DEATHFLUD */

#endif
#ifdef ANTI_DRONE_FLOOD
          case TOKEN_DRONETIME:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :DRONETIME must be > 0",
                               me.name, sptr->name);
                    return 0;
                  }       
                DRONETIME = newval;
                if(DRONETIME == 0)
                  set_notice(global, sptr, "%s has disabled the ANTI_DRONE_FLOOD code",
                                 0, 0);
                else
                  set_notice(global, sptr, "%s has changed DRONETIME on %s to %i",
					 DRONETIME, 0);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :DRONETIME is currently %i",
                           me.name, sptr->name, DRONETIME);
              }
            return 1;
            break;

        case TOKEN_DRONECOUNT:
          if(param != NULL)
            {
              int newval = atoi(param);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT must be > 0",
                             me.name, sptr->name);
                  return 0;
                }       
              DRONECOUNT = newval;
              set_notice(global, sptr, "%s has changed DRONECOUNT on %s to %i",
				     DRONECOUNT, 0);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT is currently %i",
                         me.name, sptr->name, DRONECOUNT);
            }
          return 1;
          break;
#endif
#ifdef NEED_SPLITCODE

            case TOKEN_SPLITDELAY:
              if(param != NULL)
                {
                  int newval = atoi(param);
                  
                  if(newval < 0)
                    {
                      sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY must be > 0",
                                 me.name, sptr->name);
                      return 0;
                    }
                  /* sygma found it, the hard way */
                  if(newval > MAX_SERVER_SPLIT_RECOVERY_TIME)
                    {
                      sendto_one(sptr,
                                 ":%s NOTICE %s :Cannot set SPLITDELAY over %d",
                                 me.name, sptr->name, MAX_SERVER_SPLIT_RECOVERY_TIME);
                      newval = MAX_SERVER_SPLIT_RECOVERY_TIME;
                    }
                  set_notice(global, sptr, "%s has changed SPLITDELAY on %s to %i",
                                 newval, 0);
                  SPLITDELAY = (newval*60);
                  if(SPLITDELAY == 0)
                    {
                      cold_start = NO;
                      if (server_was_split)
                        {
                          server_was_split = NO;
                          sendto_local_ops_flag(UMODE_SERVNOTICE, "split-mode deactived by manual override");
                        }
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
                      remove_empty_channels();
#endif
#if defined(SPLIT_PONG)
                      got_server_pong = YES;
#endif
                    }
                }
              else
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY is currently %.1li",
                             me.name,
                             sptr->name,
                             SPLITDELAY/60);
                }
          return 1;
          break;

        case TOKEN_SPLITNUM:
          if(param != NULL)
            {
              int newval = atoi(param);

/*              if(newval < SPLIT_SMALLNET_SIZE)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITNUM must be >= %d",
                             me.name, sptr->name,SPLIT_SMALLNET_SIZE);
                  return 0;
                } */
              set_notice(global, sptr, "%s has changed SPLITNUM on %s to %i",
                             newval, 0);
              SPLITNUM = newval;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPLITNUM is currently %i",
                         me.name,
                         sptr->name,
                         SPLITNUM);
            }
          return 1;
          break;

          case TOKEN_SPLITUSERS:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS must be >= 0",
                               me.name, sptr->name);
                    return 0;
                  }
                set_notice(global, sptr, "%s has changed SPLITUSERS on %s to %i",
                               newval, 0);
                SPLITUSERS = newval;
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS is currently %i",
                           me.name,
                           sptr->name,
                           SPLITUSERS);
              }
            return 1;
            break;
#endif
#ifdef ANTI_SPAMBOT
          case TOKEN_SPAMNUM:
            if(param != NULL)
              {
                int newval = atoi(param);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
                               me.name, sptr->name);
                    return 0;
                  }
                if(newval == 0)
                  {
                    set_notice(global, sptr, "%s has disabled ANTI_SPAMBOT",
                                   0, 0);
                    return 0;
                  }

                if(newval < MIN_SPAM_NUM)
                  SPAMNUM = MIN_SPAM_NUM;
                else
                  SPAMNUM = newval;
                set_notice(global, sptr, "%s has changed SPAMNUM on %s to %i",
                               SPAMNUM, 0);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
                           me.name, sptr->name, SPAMNUM);
              }

            return 1;
            break;

        case TOKEN_SPAMTIME:
          if(param != NULL)
            {
              int newval = atoi(param);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
                             me.name, sptr->name);
                  return 0;
                }
              if(newval < MIN_SPAM_TIME)
                SPAMTIME = MIN_SPAM_TIME;
              else
                SPAMTIME = newval;
              set_notice(global, sptr, "%s has changed SPAMTIME on %s to %i",
                             SPAMTIME, 0);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, sptr->name, SPAMTIME);
            }
          return 1;
          break;
#endif
        case TOKEN_LOG:
          if(param != NULL)
            {
              int newval = atoi(param);

              if(newval < L_WARN)
                {
                  sendto_one(sptr, ":%s NOTICE %s :LOG must be >= %d (L_WARN)",
                             me.name, sptr->name, L_WARN);
                  return 0;
                }
              if(newval > L_DEBUG)
                newval = L_DEBUG;
              set_log_level(newval); 
              set_notice(global, sptr, "%s has changed LOG level on %s to %i",
                             newval, 0);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :LOG level is currently %i (%s)",
                         me.name, sptr->name, get_log_level(),
                         get_log_level_as_string(get_log_level()));
            }
          return 1;
          break;

          case TOKEN_LISTEN:
            if(param != NULL)
              {
                int newval = atoi(param) != 0;

		if (GlobalSetOptions.listen == newval)
		  {
                    sendto_one(sptr, ":%s NOTICE %s :LISTEN is already %d",
                           me.name, sptr->name, GlobalSetOptions.listen);
		    return 1;
		  }
                GlobalSetOptions.listen = newval;
                if (GlobalSetOptions.listen == 0)
		  {
		    mark_listeners_closing();
		    close_listeners();
                    set_notice(global, sptr, "%s has disabled LISTEN on %s",
				    0, 0);
		  }
                else
		  {
		    rehash(cptr, sptr, 0);
                    set_notice(global, sptr, "%s has reenabled LISTEN on %s",
				    0, 0);
		  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :LISTEN is currently %d",
                           me.name, sptr->name, GlobalSetOptions.listen);
              }
            return 1;
            break;

        default:
        case TOKEN_BAD:
          break;
        }
    }
  sendto_one(sptr, ":%s NOTICE %s :Options: MAX, AUTOCONN",
             me.name, sptr->name);
#ifdef FLUD
  sendto_one(sptr,
#ifdef DEATHFLUD
             ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK, KILLNUM",
#else /* DEATHFLUD */
             ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
#endif /* DEATHFLUD */
             me.name, sptr->name);
#endif
#ifdef ANTI_DRONE_FLOOD
  sendto_one(sptr, ":%s NOTICE %s :Options: DRONETIME, DRONECOUNT",
             me.name, sptr->name);
#endif
#ifdef ANTI_SPAMBOT
  sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
             me.name, sptr->name);
#endif
#ifdef NEED_SPLITCODE
  sendto_one(sptr, ":%s NOTICE %s :Options: SPLITNUM SPLITUSERS SPLITDELAY",
               me.name, sptr->name);
#endif
  sendto_one(sptr, ":%s NOTICE %s :Options: LOG LISTEN",
             me.name, sptr->name);
  return 0;
}

/*
 * m_set - SET command handler
 * set options while running
 *
 * parv[0] = sender prefix
 * parv[1] = "ON", "GLOBAL" or option name
 * parv[2] = server name or option name
 * parv[3] = option name
 */
int m_set(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int i = 1, isglobal = WHERE_SINGLE, applyhere = 1;
  char *servname;
  const char *wherestr = "<invalid>";
  struct Client *acptr;
  struct ConfItem *mynline;
  char buf1[256];

  if (MyClient(sptr))
    {
      if (!HasUmode(sptr, UMODE_REHASH))
        {
          if (SeesOperMessages(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :You have no H umode", me.name, parv[0]);
          else
	    sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
          return 0;
        }
    }

  mynline = find_conf_by_name(me.name, CONF_NOCONNECT_SERVER);

  if (parc > i && !irccmp(parv[i], "GLOBAL"))
    {
      wherestr = "GLOBAL";
      isglobal = WHERE_GLOBAL;
      i++;
    }
  else if (parc > i + 1 && !irccmp(parv[i], "ON"))
    {
      servname = parv[i + 1];
      i += 2;
      acptr = find_server(servname);
      if (acptr == NULL)
        {
	  sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name, parv[0],
	    servname);
	  return 0;
	}
      else if (!IsMe(acptr))
        {
          sendto_one(acptr, ":%s SET ON %s %s %s %s", parv[0],
		  servname,
		  parc > i ? parv[i] : "",
		  parc > i + 1 ? parv[i + 1] : "",
		  parc > i + 2 ? parv[i + 2] : "");
	  return 0;
	}
    }
  else if (parc > i && !irccmp(parv[i], "VISIBLE"))
    {
      wherestr = "VISIBLE";
      isglobal = WHERE_VISIBLE;
      if (mynline == NULL || mynline->flags & CONF_FLAGS_HIDDEN_SERVER)
        applyhere = 0;
      i++;
    }
  else if (parc > i && !irccmp(parv[i], "HIDDEN"))
    {
      wherestr = "HIDDEN";
      isglobal = WHERE_HIDDEN;
      if (mynline != NULL && !(mynline->flags & CONF_FLAGS_HIDDEN_SERVER))
        applyhere = 0;
      i++;
    }

  /* Hack -- jilles */
  if (MyClient(sptr) && isglobal > WHERE_GLOBAL && parc > i + 2 &&
      !irccmp(parv[i], "AUTOCONN") && irccmp(parv[i + 1], "ALL"))
    {
      /* We cannot generate proper notices on this and it does not
       * seem useful */
      sendto_one(sptr, ":%s NOTICE %s :Cannot set AUTOCONN for a specific server on %s", me.name, parv[0], wherestr);
      return 0;
    }

  ircsnprintf(buf1, sizeof buf1, "%%s has changed %s%s%s on %%s to %%i (not here)",
	parc > i ? parv[i] : "<nothing>",
	parc > i + 2 ? " " : "",
	parc > i + 2 ? parv[i + 1] : "");
  if ((applyhere ? do_set(cptr, sptr, parc > i ? parv[i] : NULL,
		    parc > i + 1 ? parv[i + 1] : NULL,
		    parc > i + 2 ? parv[i + 2] : NULL, isglobal) :
		  (parc <= i + 1 ||
		    set_notice(isglobal, sptr, buf1,
		      atoi(parv[parc > i + 2 ? i + 2 : i + 1]), 0))) &&
		isglobal)
    {
      sendto_serv_butone(cptr, ":%s SET %s %s %s %s", parv[0],
		  wherestr,
		  parc > i ? parv[i] : "",
		  parc > i + 1 ? parv[i + 1] : "",
		  parc > i + 2 ? parv[i + 2] : "");
    }
  return 0;
}
