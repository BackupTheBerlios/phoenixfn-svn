/************************************************************************
 *   IRC - Internet Relay Chat, src/m_pong.c
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
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "channel.h"
#include "irc_string.h"
#include "s_debug.h"
#include "s_serv.h"
#include "class.h"

#ifdef DEBUGMODE
#include "s_debug.h"
#endif

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
** m_pong
**      parv[0] = sender prefix
**      parv[1] = origin
**      parv[2] = destination
*/
int     m_pong(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Client *acptr;
  char  *origin, *destination;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOORIGIN), me.name, parv[0]);
      return 0;
    }

  origin = parv[1];
  destination = parv[2];
  cptr->flags &= ~FLAGS_PINGSENT;
  sptr->flags &= ~FLAGS_PINGSENT;

#ifdef NEED_SPLITCODE
#ifdef SPLIT_PONG
  if (IsServer(cptr))
    {
      if (!got_server_pong)
        {
	  time_t ping;
          sendto_ops_flag_butone(cptr, UMODE_SEEROUTING,
				 "%s acknowledged end of %s burst (%.1lu sec). %u messages in %u kB",
				 sptr->name, me.name, CurrentTime - sptr->firsttime,
				 sptr->sendM, sptr->sendK);
	  sendto_ops_flag_butflag_butone_hidefrom(cptr, UMODE_SERVNOTICE, UMODE_SEEROUTING,
						  "Burst complete, %s has linked", sptr->name);
	  /* Ping it again, since that ping time will be unrealistically long */
	  if (!IsRegistered(cptr))
	    ping = CONNECTTIMEOUT;
	  else
	    ping = get_client_ping(cptr);
	  cptr->flags |= FLAGS_PINGSENT;
	  cptr->lasttime = CurrentTime - ping;
	  gettimeofday(&cptr->ping_send_time, NULL);
	  sendto_one(cptr, "PING :%s", me.name);
        }
      got_server_pong = 1;
    }
#endif
#endif

  if (sptr == burst_in_progress)
    burst_in_progress = NULL;

  if (EmptyString(destination) || irccmp(destination, me.name) == 0)
    {
      /* For now we'll only do this extra work when it's a server pong.
       * Client ping times can be recorded simply be expanding this test
       * to include clients.
       *  -- asuffield
       */
      if (IsServer(sptr))
	{
	  struct timeval tv;

	  gettimeofday(&tv, NULL);
	  sptr->ping_time.tv_sec = tv.tv_sec - sptr->ping_send_time.tv_sec;
	  sptr->ping_time.tv_usec = tv.tv_usec - sptr->ping_send_time.tv_usec;

#if 0
	  if (MyConnect(sptr)) /* This should never fail, with all dancer/hyperion servers */
	    {
	      sendto_serv_butone(cptr, ":%s SPINGTIME %s %.1ld", me.name, sptr->name, 
				 (sptr->ping_time.tv_sec * 1000000) + sptr->ping_time.tv_usec);
	    }
#endif
	}
    }
  /* Now attempt to route the PONG, comstud pointed out routable PING
   * is used for SPING.  routable PING should also probably be left in
   *        -Dianora
   * That being the case, we will route, but only for registered clients (a
   * case can be made to allow them only from servers). -Shadowfax
   */
  else if (IsRegistered(sptr))
    {
      if ((acptr = find_client(destination, NULL)) ||
          (acptr = find_server(destination)))
        sendto_one(acptr,":%s PONG %s %s",
                   parv[0], origin, destination);
      else
        {
          sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
                     me.name, parv[0], destination);
          return 0;
        }
    }

#ifdef  DEBUGMODE
  else
    Debug((DEBUG_NOTICE, "PONG: %s %s", origin,
           destination ? destination : "*"));
#endif
  return 0;
}
