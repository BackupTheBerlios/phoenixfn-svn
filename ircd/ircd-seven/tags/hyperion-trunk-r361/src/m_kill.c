/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kill.c
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
#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "whowas.h"
#include "irc_string.h"
#include "umodes.h"

#include <string.h>

static char buf[BUFSIZE];

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
 * m_kill
 *      parv[0] = sender prefix
 *      parv[1] = kill victim
 *      parv[2] = kill path
 */
int m_kill(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client*    acptr;
  char*       user;
  char*       reason;
  int         chasing = 0;
  static char blank[] = "";

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KILL");
      return 0;
    }

  user = parv[1];
  reason = parc > 2 ? parv[2] : blank;

  if (!(IsServer(cptr) || HasUmode(sptr,UMODE_KILL)))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no K umode",me.name,parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (strlen(reason) > (size_t) KILLLEN)
    reason[KILLLEN] = '\0';

  if (!(acptr = find_client(user, NULL)))
    {
      /*
       * If the user has recently changed nick, we automaticly rewrite
       * the KILL for this new nickname. This keeps servers in sync
       * when nick change and kill collide
       */
      if (!(acptr = get_history(user, (long)KILLCHASETIMELIMIT)))
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], user);
          return 0;
        }
      sendto_one(sptr,":%s NOTICE %s :KILL changed from %s to %s",
                 me.name, parv[0], user, acptr->name);
      chasing = 1;
    }

  if (IsServer(acptr) || IsMe(acptr))
    {
      sendto_one(sptr, form_str(ERR_CANTKILLSERVER),
                 me.name, parv[0]);
      return 0;
    }

  if (MyConnect(sptr) && !MyConnect(acptr) && !HasUmode(sptr,UMODE_GLOBAL_KILL) && !IsServer(sptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :Nick %s isnt on your server, and you have no +G umode",
                 me.name, parv[0], acptr->name);
      return 0;
    }

  sendto_local_ops_flag(UMODE_SKILL, "Killing %s for %s (%s)",
                        acptr->name, sptr->name, reason);

#if defined(USE_SYSLOG) && defined(SYSLOG_KILL)
  if (!IsServer(sptr))
    logprintf(L_INFO,"KILL from %s for %s via %s because %s",
        sptr->name, acptr->name, cptr->name, reason);
#endif

  if (!MyConnect(acptr) || !MyConnect(sptr) || IsServer(sptr))
    {
      sendto_serv_butone(cptr, ":%s KILL %s :%s",
                         parv[0], acptr->name, reason);
      if (chasing && IsServer(cptr))
        sendto_one(cptr, ":%s KILL %s :%s",
                   me.name, acptr->name, reason);
      acptr->flags |= FLAGS_KILLED;
    }

  if (MyConnect(acptr))
    sendto_prefix_one(acptr, sptr,":%s KILL %s :%s",
                      parv[0], acptr->name, reason);

  if (reason)
    ircsnprintf(buf, BUFSIZE, "Killed by %s (%s)", sptr->name, reason);
  else
    ircsnprintf(buf, BUFSIZE, "Killed by %s", sptr->name);

  return exit_client(cptr, acptr, sptr, buf);
}
