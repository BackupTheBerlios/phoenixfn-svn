/************************************************************************
 *   IRC - Internet Relay Chat, src/m_makepass.c
 *
 *   Written for UltimateIRCd by ShadowRealm Creations.
 *   Copyright (C) 1997-2000 Infomedia Inc.
 *
 *   Adapted to ircd-hybrid by Johnie Ingram (netgod) for Open Projects.
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
 */

#include "common.h"
#include "assert.h"
#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "channel.h"
#include "irc_string.h"
#include "s_debug.h"
#include "md5crypt.h"

#include <stdlib.h>

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
** m_makepass
**      parv[0] = sender prefix
**      parv[1] = passphrase to encrypt
*/
int m_makepass(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  static char saltChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
  static time_t   last_used = 0;
  char salt[9];

  if(!IsClient(sptr))
    {
      return 0;
    }

  if(!NoFloodProtection(sptr))
    {
      if(IsHoneypot(sptr) || (last_used + PACE_WAIT) > CurrentTime)
        {
          /* safe enough to give this on a local connect only */
          if(MyClient(sptr))
            sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0],"MAKEPASS");
          return 0;
        }
      else
        {
          last_used = CurrentTime;
        }
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "MAKEPASS");
      return 0;
    }

  srandom(CurrentTime);
  salt[0] = saltChars[random() % 64];
  salt[1] = saltChars[random() % 64];
  salt[2] = saltChars[random() % 64];
  salt[3] = saltChars[random() % 64];
  salt[4] = saltChars[random() % 64];
  salt[5] = saltChars[random() % 64];
  salt[6] = saltChars[random() % 64];
  salt[7] = saltChars[random() % 64];
  salt[8] = 0;

  sendto_one(sptr, ":%s NOTICE %s :*** Encryption for [ %s ] is [ %s ]",
             me.name, parv[0], parv[1], libshadow_md5_crypt(parv[1], salt));
  return 0;
}
