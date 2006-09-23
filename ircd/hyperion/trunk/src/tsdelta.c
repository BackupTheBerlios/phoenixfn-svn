/************************************************************************
 *   IRC - Internet Relay Chat, src/tsdelta.c
 *   This file is copyright (C) 2001 Andrew Suffield
 *                                    <asuffield@freenode.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#include "config.h"

#ifdef TSDELTA

#include "client.h"
#include "ircd.h"
#include "common.h"
#include "numeric.h"
#include "irc_string.h"
#include "send.h"
#include "s_serv.h"
#include "umodes.h"
#include "m_commands.h"

#include <stdlib.h>
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

/*
 * m_tsdelta
 *      parv[0] = sender prefix
 *      parv[1] = target server
 *      parv[2] = source server
 */

int m_tsdelta(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client* acptr;

  if (!HasUmode(sptr,UMODE_EXPERIMENTAL))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'X');
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 2)
    if(MyClient(sptr))
      {
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "TSDELTA");
	return 0;
      }

  if (hunt_server(cptr, sptr,
                  ":%s TSDELTA %s :%s", 2, parc, parv) != HUNTED_ISME)
    return 0;

  for (acptr = GlobalClientList; (acptr = next_client(acptr, parv[1]));
       acptr = acptr->next)
    if (IsServer(acptr) || IsMe(acptr))
      break;

  if (!acptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  if (IsMe(acptr))
    {
      sendto_one(sptr, get_str(STR_TSDELTA_ME), me.name, parv[0]);
      return 0;
    }

  sendto_ops_flag(UMODE_SPY, get_str(STR_TSDELTA_SPY), sptr->name, acptr->name);

  sendto_one(acptr, ":%s UTIME %s", me.name, acptr->name);

  return 0;
}

/*
 * m_tsdelta
 *      parv[0] = sender prefix
 *      parv[1] = target server
 *      parv[2] = time in secs
 *      parv[3] = time in usecs
 */

int m_utime(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct timeval tv;

  if (!IsServer(sptr))
    {
      sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
		 me.name, parv[0], "UTIME");
      return 0;
    }

  /* Gah. Do I need to do this in 2 forms? Not sure. Easier to just do it than to go figure
   * the internals of hunt_server and sendto_* 
   *  -- asuffield
   */
  if (parc == 4)
    if (hunt_server(cptr,sptr,":%s UTIME %s %s %s",1,parc,parv) != HUNTED_ISME)
      return 0;
  if (parc == 2)
    if (hunt_server(cptr,sptr,":%s UTIME %s",1,parc,parv) != HUNTED_ISME)
      return 0;

  if (parc > 3)
    {
      /* It's a time response, do delta stuff? */
      if (IsServer(sptr) && sptr->serv)
	{
	  struct timeval tv;
	  gettimeofday(&tv, NULL);

	  sendto_ops_flag(UMODE_EXPERIMENTAL, get_str(STR_TSDELTA), sptr->name,
			  (tv.tv_sec - atoi(parv[2])) * 1000 +
			  (tv.tv_usec - atoi(parv[3])) / 1000);
	}
    }
  else
    {
      gettimeofday(&tv, NULL);
#ifdef HAVE_LONG_LONG
      sendto_one(sptr, ":%s UTIME %s %.1lld %.1lld", me.name, parv[0], 
		 (long long)tv.tv_sec, (long long)tv.tv_usec);
#else
      sendto_one(sptr, ":%s UTIME %s %.1ld %.1ld", me.name, parv[0], 
		 (long)tv.tv_sec, (long)tv.tv_usec);
#endif
    }

  return 0;
}

#endif
