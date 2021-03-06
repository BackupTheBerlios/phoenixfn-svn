/************************************************************************
 *   IRC - Internet Relay Chat, src/m_quit.c
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
#include "s_serv.h"
#include "send.h"
#include "ctype.h"
#include "irc_string.h"
#include "umodes.h"
#include "channel.h"
#include "struct.h"

#include <string.h>

#ifndef STRIP_MISC
# define strip_colour(X) (X)
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

static char buf[BUFSIZE];

/*
 * m_quit
 *      parv[0] = sender prefix
 *      parv[1] = comment
 */
int     m_quit(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
#ifdef TIDY_QUIT
{
  buf[0] = '\0';
  if ((parc > 1) && parv[1])
    {
      if (strlen(parv[1]) > (MAX_QUIT_LENGTH - 2))
	parv[1][MAX_QUIT_LENGTH - 2] = '\0';
      /* Don't add quotes to a null message */
      if (MyConnect(sptr) && *parv[1])
	{
	  strncpy_irc(buf, "\"", 2);
	  strncat(buf, strip_colour(parv[1]), MAX_QUIT_LENGTH - 2);
	  strncat(buf, "\"", 1);
	}
      else
	strncpy_irc(buf, parv[1], BUFSIZE);
    }
  sptr->flags |= FLAGS_NORMALEX;


#ifdef ANTI_SPAM_EXIT_MESSAGE
  /* Your quit message is suppressed if:
   *
   * You haven't been connected to the server for long enough
   */
  if( !IsServer(sptr) && MyConnect(sptr) &&
     (sptr->firsttime + ANTI_SPAM_EXIT_MESSAGE_TIME) > CurrentTime)
    strcpy(buf, "Client Quit");
  else if (MyConnect(sptr) && IsPerson(sptr))
    {
      /* Or you are in a channel to which you cannot send */
      struct SLink *chptr;
      for (chptr = sptr->user->channel; chptr; chptr = chptr->next)
        {
          if (can_send(sptr, chptr->value.chptr) != 0)
            {
              strcpy(buf, "Client Quit");
              break;
            }
        }
    }
#endif
  if (IsPerson(sptr))
    {
      sendto_local_ops_flag(UMODE_CCONN,
			    "Client exiting: %s (%s@%s) [%s] [%s] [%s]",
			    sptr->name, sptr->username, sptr->host,
#ifdef WINTRHAWK
			    buf,
#else
			    (sptr->flags & FLAGS_NORMALEX) ?  "Client Quit" : comment,
#endif /* WINTRHAWK */
			    sptr->sockhost, sptr->servptr ? sptr->servptr->name : "<null>");
    }
  return IsServer(sptr) ? 0 : exit_client(cptr, sptr, sptr, buf);
}
#else /* TIDY_QUIT */
{
  char *comment = (parc > 1 && parv[1]) ? parv[1] : cptr->name;

  sptr->flags |= FLAGS_NORMALEX;
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

#ifdef ANTI_SPAM_EXIT_MESSAGE
  if( !IsServer(sptr) && MyConnect(sptr) &&
     (sptr->firsttime + ANTI_SPAM_EXIT_MESSAGE_TIME) > CurrentTime)
    comment = "Client Quit";
#endif
  if (IsPerson(sptr))
    {
      sendto_local_ops_flag(UMODE_CCONN,
			    "Client exiting: %s (%s@%s) [%s] [%s] [%s]",
			    sptr->name, sptr->username, sptr->host,
#ifdef WINTRHAWK
			    comment,
#else
			    (sptr->flags & FLAGS_NORMALEX) ?  "Client Quit" : comment,
#endif /* WINTRHAWK */
			    sptr->sockhost, sptr->servptr ? sptr->servptr->name : "<null>");
    }
  return IsServer(sptr) ? 0 : exit_client(cptr, sptr, sptr, comment);
}
#endif /* TIDY_QUIT */
