/************************************************************************
 *   IRC - Internet Relay Chat, src/m_capab.c
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
#include "irc_string.h"
#include "s_serv.h"
#include "send.h"
#include "numeric.h"
#include "ircd.h"

#include <assert.h>
#include <string.h>

struct Capability client_captab[] = {
/*   {"MARKUP", CAP_MARKUP}, */
  {"IDENTIFY-MSG", CAP_IDENTIFY_MSG},
  {"IDENTIFY-CTCP", CAP_IDENTIFY_CTCP},
  {0, 0}
};

const char *client_caps_as_string(int caps)
{
  static char buf[512 + 1];
  int comma = 0;
  struct Capability *cap;
  char *p = buf;

  for (cap = client_captab; cap->name; cap++)
    if (caps & cap->cap)
      {
        if (comma)
          *p++ = ',';
        strcpy(p, cap->name);
        p += strlen(cap->name);
        comma = 1;
      }
  *p++ = '\0';
  return buf;
}

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
 * m_capab - CAPAB message handler
 *      parv[0] = sender prefix
 *      parv[1] = space-separated list of capabilities (or a single capability, for users)
 */
int m_capab(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Capability *cap;
  char *p;
  char *s;

  if (IsClient(sptr))
    {
      if (parc < 2 || *parv[1] == '\0')
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                     me.name, parv[0], "CAPAB");
          return 0;
        }
      
      for (cap = client_captab; cap->name; cap++)
        if (!irccmp(cap->name, parv[1]))
          {
            sptr->caps |= cap->cap;
            sendto_one(sptr, form_str(RPL_CLIENTCAPAB), me.name, sptr->name, cap->name);
            return 0;
          }

      sendto_one(sptr, form_str(RPL_CLIENTCAPAB), me.name, sptr->name, "");
    }

  if ((!IsUnknown(cptr) && !IsHandshake(cptr)) || parc < 2)
    return 0;

  if (cptr->caps)
    return exit_client(cptr, cptr, cptr, "CAPAB received twice");
  else
    cptr->caps |= CAP_CAP;

  for (s = strtoken(&p, parv[1], " "); s; s = strtoken(&p, NULL, " "))
    {
      for (cap = captab; cap->name; cap++)
        {
          if (0 == strcmp(cap->name, s))
            {
              cptr->caps |= cap->cap;
              break;
            }
         }
      for (cap = other_captab; cap->name; cap++)
        {
          if (0 == strcmp(cap->name, s))
            {
              cptr->caps |= cap->cap;
              break;
            }
         }
    }
  
  return 0;
}

