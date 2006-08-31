/************************************************************************
 *   IRC - Internet Relay Chat, src/m_snick.c
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

#include "client.h"
#include "ircd.h"
#include "common.h"
#include "numeric.h"
#include "irc_string.h"
#include "send.h"
#include "s_stats.h"
#include "m_commands.h"

#include <stdlib.h>

/* SNICK is a generic server-server propagation message for shifting data
 *  about remote users.
 * It exists because NICK is pedantic about the arguments it gets, and a
 *  way of extending the protocol without continually changing NICK is
 *  needed.
 * A server is required to ignore any extra parameters it does not recognise.
 *
 * parv[0] = sender
 * parv[1] = target nick
 * parv[2] = original (sign-on) nick
 * parv[3] = spoofhost
 * parv[4] = sign-on time
 * parv[5] = dnshost (original host)
 */

int m_snick(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client *acptr;

  if (IsPerson(sptr))
    {
      if (IsServer(cptr))
        {
	  sendto_ops_flag(UMODE_SERVNOTICE, "SNICK command from remote user %s -- %s is a hacked server",
			  sptr->name, cptr->name);
        }
      else
        {
          sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                     me.name, parv[0], "SNICK");
        }
      return 0;
    }

  if (!IsServer(sptr))
    return 0;

  if (parc < 3)
    {
      sendto_ops_flag(UMODE_DEBUG, "%s sent SNICK with no parameters", sptr->name);
      return 0;
    }

  if (!(acptr = find_client(parv[1], NULL)))
    {
#if 0 /* we don't need this, SNICK is only sent after NICK -- jilles */
      /* Ghost. Kill it. */
      sendto_ops_flag(UMODE_SERVNOTICE, "Ghosted: %s from %s",
		      parv[1], cptr->name);
      sendto_one(cptr, ":%s KILL %s :%s (%s ghosted %s)",
		 me.name, parv[1], me.name, me.name, parv[1]);
#endif
      return 0;
    }

  if (cptr != acptr->from)
    {
      /* Wrong direction. Ignore it to prevent it from being applied
       * to the wrong user. This code would never have been necessary
       * if the command had been :<user> SNICK in the first place.
       * -- jilles */
      ServerStats->is_wrdi++;
      sendto_ops_flag(UMODE_DEBUG, "%s sent SNICK for %s, but %s is at %s",
		      cptr->name, acptr->name, acptr->name, acptr->from->name);
      return 0;
    }

  if (!IsPerson(acptr))
    {
      sendto_ops_flag(UMODE_DEBUG, "%s sent SNICK for non-person %s", sptr->name, acptr->name);
      return 0;
    }

  if (parc > 2)
    strncpy_irc(acptr->origname, parv[2], HOSTLEN + 1);
  if (parc > 3)
    strncpy_irc(acptr->spoofhost, parv[3], HOSTLEN + 1);
  if (parc > 4)
    acptr->firsttime = atol(parv[4]);
  if (parc > 5)
    strncpy_irc(acptr->dnshost, parv[5], HOSTLEN + 1);
  if (parc > 6)
    {
      strncpy_irc(acptr->user->servlogin,
		      irccmp(parv[6], SERVLOGIN_NONE) ? parv[6] : "",
		      SERVLOGINLEN + 1);
    }

  sendto_serv_butone(cptr, "SNICK %s %s %s %.1ld %s %s", acptr->name,
		     acptr->origname, acptr->spoofhost, acptr->firsttime,
		     acptr->dnshost, acptr->user->servlogin[0] ?
		     acptr->user->servlogin : SERVLOGIN_NONE);

  return 0;
}
