/************************************************************************
 *   IRC - Internet Relay Chat, src/m_services.c
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
#include "s_misc.h"
#include "s_serv.h"
#include "send.h"

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

#ifdef USE_SERVICES

/*
 * m_chanserv
 *
 */
int m_chanserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }

  if (IsHoneypot(sptr) || !(acptr = find_person(CHANSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }
  /* OK, enough with the safety checks. The nick should be Q:lined anyway. */
                                                                
  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], CHANSERV, parv[1]);        
  return(0);
}


/*
 * m_nickserv
 *
 */
int m_nickserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }
	
  if (IsHoneypot(sptr) || !(acptr = find_person(NICKSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }

  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], NICKSERV, parv[1]);        
  return(0);
}

/*
 * m_memoserv
 *
 */
int m_memoserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }

  if (IsHoneypot(sptr) || !(acptr = find_person(MEMOSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }

  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], MEMOSERV, parv[1]);        
  return(0);
}

/*
 * m_operserv
 *
 */
int m_operserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }

  if (IsHoneypot(sptr) || !(acptr = find_person(OPERSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }
	
  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], OPERSERV, parv[1]);
  return(0);
}
/*
 * m_seenserv
 *
 */
int m_seenserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }
	
  if (IsHoneypot(sptr) || !(acptr = find_person(SEENSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }

  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], SEENSERV, parv[1]);
  return(0);
}
/*
 * m_statserv
 *
 */
int m_statserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }
	
  if (IsHoneypot(sptr) || !(acptr = find_person(STATSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }

  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], STATSERV, parv[1]);
  return(0);
}
/*
 * m_helpserv
 *
 */
int m_helpserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }
	
  if (IsHoneypot(sptr) || !(acptr = find_person(HELPSERV, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }

  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], HELPSERV, parv[1]);
  return(0);
}

/*
 * m_global
 *
 */
int m_global(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client       *acptr;
  if (parc < 2 || *parv[1] == '\0') {
    sendto_one (sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
    return(-1);
  }

  if (IsHoneypot(sptr) || !(acptr = find_person(GLOBALNOTICE, NULL)))
    {
      sendto_one(sptr, form_str(ERR_SERVICES_OFFLINE), me.name, parv[0]);
      return 0;
    }
	
  sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0], GLOBALNOTICE, parv[1]);
  return(0);
}

#endif /* USE_SERVICES */

