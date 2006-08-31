/************************************************************************
 *   IRC - Internet Relay Chat, src/m_stopic.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   Written by Johnie Ingram (netgod) for Open Projects.
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
#include "channel.h"
#include "hash.h"
#include "struct.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"

#include <stdlib.h>
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
 * m_stopic
 *      parv[0] = sender prefix
 *      parv[1] = channel name
 *      parv[2] = topic nick
 *      parv[3] = topic ts
 *      parv[4] = channel ts
 *      parv[5] = topic text
 */

int     m_stopic(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr = NullChn;
  
  if (parc < 5)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "STOPIC");
      return 0;
    }

  /* STOPIC is only accepted from remote servers, although it may originate with a user */
  if (!IsServer(cptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

#ifdef TOPIC_INFO
  if ((chptr = hash_find_channel(parv[1], NullChn)))
    {
      time_t remote_topic_time = strtoul(parv[3], NULL, 0);
      time_t remote_channel_ts;
      char *remote_topic;
      int prefer_remote_topic = 0;
      if (parc > 5)
	{
	  remote_channel_ts = strtoul(parv[4], NULL, 0);
	  remote_topic = parv[5];
	  if (remote_channel_ts < chptr->channelts)
	    {
	      /* Remote channel is older, local channel must be a new
	       * creation. Take the remote topic
	       */
	      prefer_remote_topic = 1;
	    }
	  else if (remote_channel_ts == chptr->channelts)
	    {
	      /* Same channel after a split. Take the newer topic */
	      if (remote_topic_time > chptr->topic_time)
		prefer_remote_topic = 1;
	      else
		prefer_remote_topic = 0;
	    }
	  else
	    {
	      /* Remote channel is newer, it's getting destroyed */
	      prefer_remote_topic = 0;
	    }
	}
      else
	{
	  /* If the remote server is << 1.0.26, it won't send a channel ts.
	   * In this case, assume they are the same channel, and take the older
	   *  topic.
	   */
	  remote_channel_ts = chptr->channelts;
	  remote_topic = parv[4];
	  prefer_remote_topic = remote_topic_time < chptr->topic_time;
	}
      if (!strcmp(remote_topic, chptr->topic))
	prefer_remote_topic = 0;
      if (prefer_remote_topic)
        {
          chptr->topic_time = remote_topic_time ? remote_topic_time : CurrentTime;
          strncpy_irc(chptr->topic_nick, parv[2], NICKLEN + 1);
          strncpy_irc(chptr->topic, remote_topic, TOPICLEN + 1);
          sendto_channel_butserv(chptr, sptr, ":%s TOPIC %s :%s",
                                 IsPerson(sptr) ? sptr->name : NETWORK_NAME, chptr->chname, chptr->topic);
        }
    }
#endif
  sendto_serv_butone(cptr,":%s STOPIC %s %s %s %s :%s",
                     parv[0], parv[1], parv[2], parv[3], (parc > 5) ? parv[4] : "",
		     (parc > 5) ? parv[5] : parv[4]);
  return 0;
}
