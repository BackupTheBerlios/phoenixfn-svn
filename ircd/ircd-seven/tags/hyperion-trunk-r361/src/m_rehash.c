/************************************************************************
 *   IRC - Internet Relay Chat, src/m_rehash.c
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
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "umodes.h"
#include "m_kline.h"

#include <string.h>

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

static time_t last_rehash = 0;
static int rehash_pending = 0;

/*
 * m_rehash - REHASH message handler
 *
 */
int m_rehash(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int found = NO;
  int send_notice = 1, global = 0;
  void (*sendto_the_ops_flag)(int flag, const char *pattern, ...);
  char *what;

  if (!IsServer(sptr) && (!HasUmode(sptr,UMODE_REHASH)))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no H umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  /* Allow these forms
   * REHASH <what> <destination>
   * REHASH <destination> (if <destination> contains * ? or .)
   * REHASH <what>
   * REHASH
   */
  if (parc > 2 || (parc > 1 && (strchr(parv[1], '*') || strchr(parv[1], '?') ||
				  strchr(parv[1], '.'))))
    {
      if (((parc > 2) && !strcmp(parv[2], "*")) || (parc == 2 && !strcmp(parv[1], "*")))
	{
	  sendto_serv_butone(cptr, ":%s REHASH %s *", sptr->name, (parc > 2) ? parv[1] : "");
	  global = 1;
	}
      else if (hunt_server(cptr, sptr, 
			   (parc > 2) ? ":%s REHASH %s %s" : ":%s REHASH %s", 
			   (parc > 2) ? 2 : 1,
			   parc, parv) != HUNTED_ISME)
	return 0;
      what = parc > 2 ? parv[1] : NULL;
    }
  else
    what = parc > 1 ? parv[1] : NULL;

  /* send a RPL_REHASHING? */
  send_notice = (!IsServer(sptr) && !global) || MyClient(sptr);
  /* local or global snotes? */
  sendto_the_ops_flag = IsServer(sptr) || global ? sendto_local_ops_flag :
	  sendto_ops_flag;

  if (what != NULL)
    {
      if (irccmp(what,"DNS") == 0)
        {
	  if (send_notice)
            sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name, "DNS");
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "Hmmm, %s is rehashing DNS", sptr->name);
          restart_resolver();   /* re-read /etc/resolv.conf AGAIN?
                                   and close/re-open res socket */
          found = YES;
        }
      else if(irccmp(what,"GC") == 0)
        {
	  if (send_notice)
            sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name, "garbage collecting");
          block_garbage_collect();
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "Hmmm, %s is garbage collecting", sptr->name);
          found = YES;
        }
      else if(irccmp(what,"MOTD") == 0)
        {
	  if (send_notice)
	    sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name, &ConfigFileEntry.motd);
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is forcing re-reading of MOTD file",sptr->name);
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(what,"OMOTD") == 0)
        {
	  if (send_notice)
	    sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name, &ConfigFileEntry.opermotd);
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is forcing re-reading of oper MOTD file",sptr->name);
          ReadMessageFile( &ConfigFileEntry.opermotd );
          found = YES;
        }
      else if(irccmp(what,"HELP") == 0)
        {
	  if (send_notice)
	    sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name, &ConfigFileEntry.helpfile);
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is forcing re-reading of oper help file",sptr->name);
          ReadMessageFile( &ConfigFileEntry.helpfile );
          found = YES;
        }
      else if(irccmp(what,"dump") == 0)
        {
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is dumping conf file",sptr->name);
          rehash_dump(sptr);
          found = YES;
        }
      else if(irccmp(what,"dlines") == 0)
        {
	  if (send_notice)
            sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name,
                     ConfigFileEntry.configfile);
          /* this does a full rehash right now, so report it as such */
#ifdef CUSTOM_ERR
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is rehashing dlines from server config file while whistling innocently",
			      sptr->name);
#else
	  sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is rehashing dlines from server config file",
			      sptr->name);
#endif
          logprintf(L_NOTICE, "REHASH from %s\n", sptr->name);
          dline_in_progress = 1;

          return rehash(cptr, sptr, 0);
        }
      else if (irccmp(what, "all") == 0)
	{
	  /* Lots of rehashes in a hurry -> collapse into one */
          if (last_rehash + 2 >= CurrentTime || rehash_pending)
            {
              rehash_pending = 1;
              return 0;
            }
          last_rehash = CurrentTime;

	  /* Duplication. Bite me */
	  if (send_notice)
	    {
	      sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name,
			 ConfigFileEntry.configfile);
	    }
	  if (!global)
	    {
#ifdef CUSTOM_ERR
	      sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is rehashing server config file while whistling innocently",
				  sptr->name);
#else
	      sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is rehashing server config file",
				  sptr->name);
#endif
	    }
	  else
	    sendto_the_ops_flag(UMODE_SERVNOTICE, "%s is rehashing server config file while whistling innocently, GLOBALLY",
				sptr->name);
	  logprintf(L_NOTICE, "REHASH From %s\n", get_client_name(sptr, SHOW_IP));

          return rehash(cptr, sptr, 0);
	}
      if(found)
        {
          logprintf(L_NOTICE, "REHASH %s from %s\n", what, sptr->name);
          return 0;
        }
      else
        {
          sendto_one(sptr, ":%s NOTICE %s :Rehash one of: DNS, DLINES, TKLINES, GC, HELP, MOTD, OMOTD, DUMP", 
		     me.name, sptr->name);
          return(0);
        }
    }
  else
    {
      /* Lots of rehashes in a hurry -> collapse into one */
      if (last_rehash + 2 >= CurrentTime || rehash_pending)
        {
          rehash_pending = 1;
          return 0;
        }
      last_rehash = CurrentTime;
      if (send_notice)
	sendto_one(sptr, form_str(RPL_REHASHING), me.name, sptr->name,
		   ConfigFileEntry.configfile);
      if (!global)
	sendto_the_ops_flag(UMODE_SERVNOTICE, "Hmm, %s is rehashing server config file", sptr->name);
      else
	sendto_the_ops_flag(UMODE_SERVNOTICE, "Hmm, %s is rehashing server config file, GLOBALLY",
			    sptr->name);
      logprintf(L_NOTICE, "REHASH From %s\n", get_client_name(sptr, SHOW_IP));
      return rehash(cptr, sptr, 0);
    }
  return 0; /* shouldn't ever get here */
}

/* This is a bit of a kludge, but it should do the trick. Notices get
   lost, what a pity */
void
rehash_timer(void)
{
  /* make sure we *always* rehash after rehash_pending was set -- jilles */
  if (!rehash_pending || last_rehash + 2 >= CurrentTime)
    return;
  sendto_local_ops_flag(UMODE_SERVNOTICE, "Executing delayed rehash");
  rehash(&me, &me, 0);
  rehash_pending = 0;
  last_rehash = CurrentTime;
}
