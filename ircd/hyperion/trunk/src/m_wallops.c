/************************************************************************
 *   IRC - Internet Relay Chat, src/m_wallops.c
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
#include <stdlib.h>
#include <string.h>
#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "send.h"
#include "s_user.h"
#include "umodes.h"

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
 * m_wallops (write to *all* opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = op mask [optional]
 *      parv[2] = message text
 */
int m_wallops(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{ 
  char* message;
  int flag=0, butflag=0;
  int spoof = 0;

  if (parc == 3)
    {
      if (*parv[1] == '*')
	{
	  parv[1]++;
	  spoof = 1;
	}
      flag = atoi(parv[1]);
      {
	char* but = strchr(parv[1], '-');
	if (but)
	  butflag = atoi(but + 1);
      }
      message = parv[2];
    }
  else
    message = parc > 1 ? parv[1] : NULL;
  
  if (EmptyString(message))
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "WALLOPS");
      return 0;
    }

  if (!IsServer(sptr) && MyConnect(sptr) && !SendWallops(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  /* If its coming from a server, do the normal thing
     if its coming from an oper, send the wallops along
     and only send the wallops to our local opers (those who are +oz)
     -Dianora
  */

  if (parc == 3)
    {
      if (spoof)
	sendto_ops_flag_butflag_butone_hidefrom(cptr, flag, butflag, "%s", message);
      else
	sendto_ops_flag_butflag_butone_from(cptr, flag, butflag, sptr, "%s", message);
      return 0;
    }

  if(!IsServer(sptr))   /* If source of message is not a server, i.e. oper */
    {

#ifdef PACE_WALLOPS
      if( MyClient(sptr) )
        {
          if( (LastUsedWallops + WALLOPS_WAIT) > CurrentTime )
            { 
          	sendto_one(sptr, ":%s NOTICE %s :Oh, one of those annoying opers who doesn't know how to use a channel",
                     me.name,parv[0]);
          	return 0;
            }
          LastUsedWallops = CurrentTime;
        }
#endif

      sendto_wallops_butone(IsServer(cptr) ? cptr : &me, sptr,
                            ":%s WALLOPS :%s", parv[0], message);

    }
  else                  /* its a server wallops */
    sendto_wallops_butone(IsServer(cptr) ? cptr : NULL, sptr,
                            ":%s WALLOPS :%s", parv[0], message);
  return 0;
}

