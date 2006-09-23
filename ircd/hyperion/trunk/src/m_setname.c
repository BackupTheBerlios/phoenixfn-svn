/************************************************************************
 *   IRC - Internet Relay Chat, src/m_setname.c
 *
 *   Written for UltimateIRCd by ShadowRealm Creations.
 *   Copyright (C) 1997-2000 Infomedia Inc.
 *
 *   Adapted to ircd-hybrid by Johnie Ingram (netgod) for Open Projects.
 *
 *   Output messages fixed by Daniel Dent (ddent).
 *
 *   Some functions in this file may be Copyright
 *   (C) 1999 Carsten Munk (Techie/Stskeeps) <stskeeps@tspre.org>
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
#include "client.h"
#include "s_user.h"

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
 * m_sethost
 *      parv[0] = sender prefix
 *      parv[1] = target (optional)
 *      parv[2] = hostname
 */
int     m_sethost(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  struct Client *acptr;
  const char *hostname, *target;

  target = parv[1];
  hostname = parv[2];

  if (MyClient(sptr) && !HasUmode(sptr,UMODE_SETNAME))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'P');
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ((parc > 1) && MyClient(sptr) && !HasUmode(sptr,UMODE_FREESPOOF))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], '@');
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc <= 2)
    {
      hostname = parv[1];
      target = sptr->name;
    }

  if (parc < 2)
    hostname = strcmp(sptr->host, sptr->dnshost) ? sptr->dnshost : sptr->spoofhost;

  if (!(acptr = find_person(target, NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, sptr->name, target);
      return 0;
    }

  if (strcmp(hostname, ".dns") == 0)
    hostname = acptr->dnshost;

  if (!valid_hostname_remote(hostname))
    {
      sendto_one(sptr, ":%s NOTICE %s :SETHOST Error [%s]: Invalid hostname",
		 me.name, parv[0], hostname);
      if (IsServer(cptr))
        sendto_ops_flag(UMODE_SERVNOTICE,
	  "Invalid SETHOST ignored: [%s] for %s from %s (via %s)",
	  hostname, acptr->name, sptr->name, cptr->name);
      return 0;
    }

  if (sptr != acptr)
    if (MyClient(sptr) && !HasUmode(sptr,UMODE_CHANGEOTHER))
      {
        if (SeesOperMessages(sptr))
          sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'B');
        else
          sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
        return 0;
      }

  if (!IsServer(sptr))
    sendto_local_ops_flag(UMODE_SPY,
                          "Hmmm, %s is rehosting %s (%s@%s) to \"%s\"",
                          sptr->name, acptr->name,
                          acptr->username, acptr->host, hostname);

  if (sptr == acptr)
    {
      if (MyClient(acptr))
        sendto_one(acptr, ":%s NOTICE %s :your hostname is now set to \"%s\"",
                   me.name, sptr->name, hostname);
    }
  else
    {
      if (MyClient(acptr))
        sendto_one(acptr, ":%s NOTICE %s :%s set your hostname to \"%s\"",
                   me.name, acptr->name, sptr->name, hostname);
      if (MyClient(sptr))
        sendto_one(sptr, ":%s NOTICE %s :%s\'s hostname has been set to \"%s\"",
                   me.name, sptr->name, acptr->name, hostname);
    }

  sendto_serv_butone(cptr, ":%s SETHOST %s :%s",
                     sptr->name,
                     target,
                     hostname);
  ircsnprintf(acptr->host, HOSTLEN + 1, "%s", hostname);

#ifdef  DEBUGMODE
  Debug((DEBUG_NOTICE, "SETHOST: %s %s", target, hostname));
#endif
  return 0;
}


/*
 * m_setident
 *      parv[0] = sender prefix
 *      parv[1] = target (optional)
 *      parv[2] = username
 */
int     m_setident(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  struct Client *acptr;
  char  *username, *target;

  target = parv[1];
  username = parv[2];

  if (MyClient(sptr) && !HasUmode(sptr,UMODE_SETNAME))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'P');
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "SETIDENT");
      return 0;
    }

  if (parc == 2)
    {
      username = parv[1];
      target = sptr->name;
    }

  if (!valid_username(username) || strlen(username) > USERLEN) {
    sendto_one(sptr, ":%s NOTICE %s :SETIDENT Error [%s]: A username may contain "
               "a-z, A-Z, 0-9, '-', '~' & '.' - please use only these",
               me.name, parv[0], username);
    return 0;
  }

  if ((acptr = find_person(target, NULL)))
    {
      if (sptr != acptr)
	if (MyClient(sptr) && !HasUmode(sptr,UMODE_CHANGEOTHER))
	  {
	    if (SeesOperMessages(sptr))
	      sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'B');
	    else
	      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	    return 0;
	  }
      sendto_local_ops_flag(UMODE_SPY,
			    "Hmmm, %s is identing %s (%s@%s) to \"%s\"",
			    sptr->name, acptr->name,
			    acptr->username, acptr->host, username);
      if (sptr == acptr)
	{
	  if (MyClient(acptr))
	    sendto_one(acptr, ":%s NOTICE %s :your ident is now set to \"%s\"",
		       me.name, sptr->name, username);
	}
      else
	{
	  if (MyClient(acptr))
	    sendto_one(acptr, ":%s NOTICE %s :%s set your ident to \"%s\"",
		       me.name, acptr->name, sptr->name, username);
	  if (MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :%s\'s ident has been set to \"%s\"",
		       me.name, sptr->name, acptr->name, username);
	}
      sendto_serv_butone(cptr, ":%s SETIDENT %s :%s",
			 sptr->name,
			 target,
			 username);
      ircsnprintf(acptr->username, USERLEN, "%s", username);
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, sptr->name, target);
      return 0;
    }

#ifdef  DEBUGMODE
  Debug((DEBUG_NOTICE, "SETIDENT: %s %s", target, username));
#endif
  return 0;
}


/*
 * m_setname
 *      parv[0] = sender prefix
 *      parv[1] = target (optional)
 *      parv[2] = realname
 */
int     m_setname(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  struct Client *acptr;
  char  *realname, *target;

  target = parv[1];
  realname = parv[2];

  if (parc < 3)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "SETNAME");
      return 0;
    }

  if (MyClient(sptr) && !HasUmode(sptr,UMODE_SETNAME))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'P');
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (strlen(realname) > REALLEN)
    {
      sendto_one(sptr, ":%s NOTICE %s :SETNAME Error [%s]: The gecos field "
		 "may not be that long",
		 me.name, parv[0], realname);
      return 0;
    }

  if ((acptr = find_server(target)))
    {
      sendto_local_ops_flag(UMODE_SERVNOTICE,
			    "Hmmm, %s is changing the realname field for %s to \"%s\"",
			    sptr->name, acptr->name, realname);
      sendto_serv_butone(cptr, ":%s SETNAME %s :%s",
			 sptr->name,
			 target,
			 realname);
      ircsnprintf(acptr->info, REALLEN, "%s", realname);
    }
  else if ((acptr = find_person(target, NULL)))
    {
      if (sptr != acptr)
	if (MyClient(sptr) && !HasUmode(sptr,UMODE_CHANGEOTHER))
	  {
	    if (SeesOperMessages(sptr))
	      sendto_one(sptr, form_str(ERR_NEED_UMODE), me.name, parv[0], 'B');
	    else
	      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	    return 0;
	  }
      sendto_local_ops_flag(UMODE_SPY,
			    "Hmmm, %s is changing the realname field for %s (%s@%s) to \"%s\"",
			    sptr->name, acptr->name,
			    acptr->username, acptr->host, realname);
      if (sptr == acptr)
	{
	  if (MyClient(acptr))
	    sendto_one(acptr, ":%s NOTICE %s :your realname is now set to \"%s\"",
		       me.name, sptr->name, realname);
	}
      else
	{
	  if (MyClient(acptr))
	    sendto_one(acptr, ":%s NOTICE %s :%s set your realname to \"%s\"",
		       me.name, acptr->name, sptr->name, realname);
	  if (MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :%s\'s realname has been set to \"%s\"",
		       me.name, sptr->name, acptr->name, realname);
	}
      sendto_serv_butone(cptr, ":%s SETNAME %s :%s",
			 sptr->name,
			 target,
			 realname);
      ircsnprintf(acptr->info, REALLEN, "%s", realname);
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, sptr->name, target);
      return 0;
    }

#ifdef  DEBUGMODE
  Debug((DEBUG_NOTICE, "SETNAME: %s %s", target, realname));
#endif
  return 0;
}

