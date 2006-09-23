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
#include "send.h"
#include "whowas.h"
#include "umodes.h"
#include "irc_string.h"

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
 * m_collide
 *      parv[0] = sender prefix
 *      parv[1] = collide victim
 */
/* COLLIDE emits different messages to KILL. It should make services
 * kills easier to distinguish from oper kills (and aid in logging).
 * This is for purely server-generated collisions, it can never come
 * from a user.
 *
 * It's also much simpler and transfers less data across the network
 */
int m_collide(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client*    acptr;
  char*       user;
  int         chasing = 0;
  static char collide_reason[BUFSIZE];

  if (!IsServer(sptr))
    {
      sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                 me.name, parv[0], "COLLIDE");
      return 0;
    }

  if (parc < 2)
    return 0;

  user = parv[1];

  if (!(acptr = find_client(user, NULL)))
    {
      /*
      ** If the user has recently changed nick, we automaticly
      ** rewrite the KILL for this new nickname--this keeps
      ** servers in synch when nick change and kill collide
      */
      if (!(acptr = get_history(user, (long)KILLCHASETIMELIMIT)))
        return 0;
      chasing = 1;
    }

  if (IsServer(acptr) || IsMe(acptr))
    {
      sendto_one(sptr, form_str(ERR_CANTKILLSERVER),
                 me.name, parv[0]);
      sendto_ops_flag(UMODE_DEBUG, "COLLIDE for %s from %s (rejected, is server)",
                      acptr->name, sptr->name);
      return 0;
    }

  sendto_local_ops_flag(UMODE_DEBUG,
                        "Received COLLIDE message for %s from %s",
                        acptr->name, sptr->name);

  sendto_serv_butone(cptr, ":%s COLLIDE %s",
                     parv[0], acptr->name);
  if (chasing && IsServer(cptr))
    sendto_one(cptr, ":%s COLLIDE %s",
               me.name, acptr->name);
  acptr->flags |= FLAGS_KILLED;

  /*
  ** Tell the victim she/he has been zapped, but *only* if
  ** the victim is on current server--no sense in sending the
  ** notification chasing the above kill, it won't get far
  ** anyway (as this user don't exist there any more either)
  */
  if (MyConnect(acptr))
    sendto_one(acptr, ":%s KILL %s :Nick collision",
               parv[0], acptr->name);

  ircsnprintf(collide_reason, 512, "Nick collision from %s", sptr->name);
  return exit_client(cptr, acptr, sptr, collide_reason);
}

