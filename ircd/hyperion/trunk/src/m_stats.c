/************************************************************************
 *   IRC - Internet Relay Chat, src/m_stats.c
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
#include "m_commands.h"  /* m_pass prototype */
#include "class.h"       /* report_classes */
#include "client.h"      /* Client */
#include "common.h"      /* TRUE/FALSE */
#include "dline_conf.h"  /* report_dlines */
#include "irc_string.h"  /* strncpy_irc */
#include "ircd.h"        /* me */
#include "listener.h"    /* show_ports */
#include "msg.h"         /* Message */
#include "mtrie_conf.h"  /* report_mtrie_conf_links */
#include "numeric.h"     /* ERR_xxx */
#include "scache.h"      /* list_scache */
#include "send.h"        /* sendto_one */
#include "s_bsd.h"       /* highest_fd */
#include "s_conf.h"      /* ConfItem, report_configured_links */
#include "s_debug.h"     /* send_usage */
#include "s_misc.h"      /* serv_info */
#include "s_serv.h"      /* hunt_server, show_servers */
#include "s_stats.h"     /* tstats */
#include "s_user.h"      /* show_staff */
#include "umodes.h"

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
 * m_stats - STATS message handler
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 *
 *      Currently supported are:
 *              M = Message frequency (the old stat behaviour)
 *              L = Local Link statistics
 *              C = Report C and N configuration lines
 *
 *
 * m_stats/stats_conf
 *    Report N/C-configuration lines from this server. This could
 *    report other configuration lines too, but converting the
 *    status back to "char" is a bit akward--not worth the code
 *    it needs...
 *
 *    Note:   The info is reported in the order the server uses
 *            it--not reversed as in ircd.conf!
 */
static const char* Lformat = ":%s %d %s %s %u %u %u %u %u :%u %u %s";

int m_stats(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Message* mptr;
  struct Client*  acptr;
  char            statcmd = parc > 1 ? parv[1][0] : '\0';
  int             i;
  int             doall = 0;
  int             wilds = 0;
  int             ignore_request = 0;
  int             valid_stats = 0;
  char*           name;

  if(!IsClient(sptr))
    {
      return 0;
    }

#ifdef SERVERHIDE
  /* We do this on a per-stats basis -- asuffield
    if (!IsSetOperAuspex(sptr))
    {
    ignore_request++;
    valid_stats++;
    return 0;
    }
  */
#endif

  if (!HasUmode(sptr,UMODE_REMOTEINFO) && parc > 2)
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no S umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 3)
    {
      if (hunt_server(cptr,sptr,":%s STATS %s %s",2,parc,parv)!=HUNTED_ISME)
	return 0;
    }
  else
    {
      if (hunt_server(cptr,sptr,":%s STATS %s %s %s",2,parc,parv)!=HUNTED_ISME)
	return 0;
    }

  if (parc > 2)
    {
      name = parv[2];
      if (!irccmp(name, me.name))
        doall = 2;
      else if (match(name, me.name))
        doall = 1;
      if (strchr(name, '*') || strchr(name, '?'))
        wilds = 1;
    }
  else
    name = me.name;

  switch (statcmd)
    {
    case 'L' : case 'l' :
      /*
       * send info about connections which match, or all if the
       * mask matches me.name.  Only restrictions are on those who
       * are invisible not being visible to 'foreigners' who use
       * a wild card based search to list it.
       */
      for (i = 0; i <= highest_fd; i++)
        {
          if (!(acptr = local[i]))
            continue;

          if (IsPerson(acptr) &&
              !HasUmode(sptr,UMODE_AUSPEX) &&
              (acptr != sptr))
            continue;
          if (IsInvisible(acptr) && (doall || wilds) &&
              !HasUmode(sptr,UMODE_AUSPEX) &&
              (acptr != sptr))
            continue;
          if (!doall && wilds && !match(name, acptr->name))
            continue;
          if (!(doall || wilds) && irccmp(name, acptr->name))
            continue;

          /* I've added a sanity test to the "CurrentTime - acptr->since"
           * occasionally, acptr->since is larger than CurrentTime.
           * The code in parse.c "randomly" increases the "since",
           * which means acptr->since is larger then CurrentTime at times,
           * this gives us very high odd number.. 
           * So, I am going to return 0 for ->since if this happens.
           * - Dianora
           */
          /* trust opers not on this server */
          /* if(IsAnOper(sptr)) */

          /* Don't trust opers not on this server */
          if(MyClient(sptr) && HasUmode(sptr,UMODE_AUSPEX))
            {
              sendto_one(sptr, Lformat, me.name,
                     RPL_STATSLINKINFO, parv[0],
                     (IsUpper(stat)) ?
                     get_client_name(acptr, TRUE) :
                     get_client_name(acptr, FALSE),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     CurrentTime - acptr->firsttime,
                     (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
                     IsServer(acptr) ? show_capabilities(acptr) : "-");
	    }
	  else
	    {
	      if(IsIPHidden(acptr) || IsServer(acptr) ||
		 IsConnecting(acptr) || IsHandshake(acptr))
		sendto_one(sptr, Lformat, me.name,
			   RPL_STATSLINKINFO, parv[0],
			   get_client_name(acptr, MASK_IP),
			   (int)DBufLength(&acptr->sendQ),
			   (int)acptr->sendM, (int)acptr->sendK,
			   (int)acptr->receiveM, (int)acptr->receiveK,
			   CurrentTime - acptr->firsttime,
			   (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
			   IsServer(acptr) ? show_capabilities(acptr) : "-");
	      else
		sendto_one(sptr, Lformat, me.name,
			   RPL_STATSLINKINFO, parv[0],
			   (IsUpper(statcmd)) ?
			   get_client_name(acptr, TRUE) :
			   get_client_name(acptr, FALSE),
			   (int)DBufLength(&acptr->sendQ),
			   (int)acptr->sendM, (int)acptr->sendK,
			   (int)acptr->receiveM, (int)acptr->receiveK,
			   CurrentTime - acptr->firsttime,
			   (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
			   IsServer(acptr) ? show_capabilities(acptr) : "-");
	    }
        }
      valid_stats++;
      break;
    case 'C' : case 'c' :
#ifdef SERVERHIDE
      if (!HasUmode(sptr,UMODE_AUSPEX))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
      else
#endif
      report_configured_links(sptr, CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER);
      valid_stats++;
      break;

    case 'D': case 'd':
      if (!CanSeeDlines(sptr))
        {
          sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
          break;
        }
      report_dlines(sptr);
      valid_stats++;
      break;

    case 'H' : case 'h' :
#ifdef SERVERHIDE
      if (!HasUmode(sptr,UMODE_AUSPEX))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
      else
#endif
      report_configured_links(sptr, CONF_HUB|CONF_LEAF);
      valid_stats++;
      break;

    case 'I' : case 'i' :
#ifdef I_LINES_OPER_ONLY
      if (!CanSeeIlines(sptr))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
#endif /* I_LINES_OPER_ONLY */
      report_mtrie_conf_links(sptr, CONF_CLIENT);
      valid_stats++;
      break;

    case 'k' :
    case 'K' :
#ifdef K_LINES_OPER_ONLY
      if (!CanSeeKlines(sptr))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
#endif /* K_LINES_OPER_ONLY */
      if(parc > 3)
        report_matching_host_klines(sptr,parv[3]);
      else
        if (CanSeeKlines(sptr))
          report_mtrie_conf_links(sptr, CONF_KILL);
        else
          report_matching_host_klines(sptr,sptr->host);
      valid_stats++;
      break;

    case 'M' : case 'm' :
      for (mptr = msgtab; mptr->cmd; mptr++)
          sendto_one(sptr, form_str(RPL_STATSCOMMANDS),
                     me.name, parv[0], mptr->cmd,
                     mptr->count, mptr->bytes);
      valid_stats++;
      break;

    case 'o' : case 'O' :
#ifdef O_LINES_OPER_ONLY
      if (!CanSeeOlines(sptr))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
#endif /* O_LINES_OPER_ONLY */
      report_configured_links(sptr, CONF_OPERATOR);
      valid_stats++;
      break;

    case 'P' :
#ifdef P_LINES_OPER_ONLY
      if (!HasUmode(sptr,UMODE_AUSPEX)) 
        {
          ignore_request++;
          valid_stats++;
          break;
        }
#endif /* P_LINES_OPER_ONLY */
      show_ports(sptr);
      valid_stats++;
      break;

    case 'p' :
      show_staff(sptr);
      valid_stats++;
      break;
    case 'Q' : case 'q' :
      if(!CanSeeQlines(sptr))
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      else
        {
          report_qlines(sptr);
          valid_stats++;
        }
      break;

    case 'R' : case 'r' :
      if (!HasUmode(sptr, UMODE_DEBUG))
	{
	  ignore_request++;
	  valid_stats++;
	  break;
	}
      send_usage(sptr,parv[0]);
      valid_stats++;
      break;

    case 'S' : case 's':
      if (HasUmode(sptr,UMODE_DEBUG))
        list_scache(cptr,sptr,parc,parv);
      else
        sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      valid_stats++;
      break;

    case 'T' : case 't' :
      if (!CanSeeStatsT(sptr))
        {
          sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
          break;
        }
      tstats(sptr, parv[0]);
      valid_stats++;
      break;

    case 'u' :
      {
        time_t now;
        
        now = CurrentTime - me.since;
        sendto_one(sptr, form_str(RPL_STATSUPTIME), me.name, parv[0],
                   now/86400, (now/3600)%24, (now/60)%60, now%60);
        sendto_one(sptr, form_str(RPL_STATSCONN), me.name, parv[0],
                   MaxConnectionCount, MaxClientCount,
                   Count.totalrestartcount);
        valid_stats++;
        break;
      }

    case 'v' : case 'V' :
      if (!HasUmode(sptr, UMODE_SEEROUTING))
        {
          ignore_request++;
          valid_stats++;
          break;
        }
      show_servers(sptr);
      valid_stats++;
      break;

    case 'x' : case 'X' :
      if(CanSeeXlines(sptr))
        {
          report_specials(sptr,CONF_XLINE,RPL_STATSXLINE);
          valid_stats++;
        }
      break;;

    case 'Y' : case 'y' :
      if(CanSeeYlines(sptr))
        { 
          report_classes(sptr);
          valid_stats++;
        }
      break;

    case 'Z' : case 'z' :
      if (HasUmode(sptr,UMODE_DEBUG))
        {
          count_memory(sptr, parv[0]);
        }
      else
        sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      valid_stats++;
      break;

    case '?':
      if (CanSeeStatsServinfo(sptr))
      {
        serv_info(sptr, parv[0]);
      }
      else
        sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);

      valid_stats++;
      break;

    default :
      statcmd = '*';
      break;
    }
  sendto_one(sptr, form_str(RPL_ENDOFSTATS), me.name, parv[0], statcmd);

  /* personally, I don't see why opers need to see stats requests
   * at all. They are just "noise" to an oper, and users can't do
   * any damage with stats requests now anyway. So, why show them?
   * -Dianora
   */

#ifdef STATS_NOTICE
  if (valid_stats)
    {
      if ( (statcmd == 'L') || (statcmd == 'l') )
        {
          sendto_ops_flag(UMODE_SPY,
			  "STATS %c requested by %s (%s@%s) [%s] on %s%s",
			  statcmd,
			  sptr->name,
			  sptr->username,
			  sptr->host,
			  sptr->user->server,
			  parc > 2 ? parv[2] : "<no recipient>",
			  ignore_request > 0 ? " [request ignored]" : "\0"
			  );
        }
      else
        {
          sendto_ops_flag(UMODE_SPY,
			  "STATS %c requested by %s (%s@%s) [%s]%s",
			  statcmd,
			  sptr->name,
			  sptr->username,
			  sptr->host,
			  sptr->user->server,
			  ignore_request > 0 ? " [request ignored]" : "\0"
			  );
        }
    }
#endif
  return 0;
}
