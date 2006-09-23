/************************************************************************
 *   IRC - Internet Relay Chat, src/m_burst.c
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
#include "s_conf.h"
#include "m_commands.h"
#include "s_serv.h"
#include "s_log.h"

/* Send all of my K:lines over to the remote host */
static int
burst_klines(struct Client *sptr)
{
  struct ConfItem *aconf;
  int i = 0;

  for (aconf = kline_list; aconf; aconf = aconf->kline_next)
    {
      if (aconf->status == CONF_KILL)
	{
	  if (aconf->hold)
	    {
	      /* This one has a timeout */
	      long int time_remaining = (aconf->hold - CurrentTime) / 60;
	      if (time_remaining > 0)
		sendto_one(sptr, ":%s KLINE %s %.1ld %s@%s :%s", me.name, me.name, 
			   time_remaining, aconf->user, aconf->host, aconf->passwd);
	    }
	  else
	    sendto_one(sptr, ":%s KLINE %s %s@%s :%s", me.name, me.name, aconf->user, aconf->host, aconf->passwd);
	}
      if (i++ > kline_count)
	{
	  sendto_ops_flag(UMODE_DEBUG, "WTF: kline_list larger than kline_count (%d) in m_burst(), aborting", kline_count);
	  logprintf(L_WARN, "WTF: kline_list larger than kline_count (%d)in m_burst(), aborting", kline_count);
	  return 0;
	}
    }
  return 0;
}

int m_burst(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  /* Check up on remote servers, send appropriate error */
  if (IsPerson(sptr))
    {
      if (IsServer(cptr))
        {
	  sendto_ops_flag(UMODE_SERVNOTICE, "BURST command from remote user %s -- %s is a hacked server",
			  sptr->name, cptr->name);
        }
      else
        {
          sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                     me.name, parv[0], "BURST");
        }
      return 0;
    }

  /* Ignore anything else */
  if (!IsServer(sptr))
    return 0;

  if (parc < 2)
    {
      sendto_ops_flag(UMODE_DEBUG, "%s sent BURST with no parameters", sptr->name);
      return 0;
    }

  if (!irccmp(parv[1], "KLINES"))
    return burst_klines(sptr);
  else
    sendto_ops_flag(UMODE_DEBUG, "%s sent BURST for unknown type %s", sptr->name, parv[1]);

  return 0;
}
