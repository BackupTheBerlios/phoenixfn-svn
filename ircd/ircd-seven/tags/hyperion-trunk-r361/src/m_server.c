/************************************************************************
 *   IRC - Internet Relay Chat, src/m_server.c
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
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "s_stats.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "umodes.h"
#include "s_log.h"

#include <string.h>
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
 * m_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
int m_server(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int              i;
  char             info[REALLEN + 1];
  char*            host;
  struct Client*   acptr;
  struct Client*   bcptr;
  struct ConfItem* aconf;
  int              hop;

  info[0] = '\0';
  /*  inpath = get_client_name(cptr,FALSE); */
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(cptr,"ERROR :No servername");
      return 0;
    }
  hop = 0;
  host = parv[1];
  if (parc > 3 && atoi(parv[2]))
    {
      hop = atoi(parv[2]);
      strncpy_irc(info, parv[3], REALLEN + 1);
      info[REALLEN] = '\0';
    }
  else if (parc > 2)
    {
      /*
       * XXX - hmmmm
       */
      strncpy_irc(info, parv[2], REALLEN + 1);
      info[REALLEN] = '\0';
      if ((parc > 3) && ((i = strlen(info)) < (REALLEN - 2)))
        {
          strcat(info, " ");
          strncat(info, parv[3], REALLEN - i - 2);
          info[REALLEN] = '\0';
        }
    }
  /*
   * July 5, 1997
   * Rewritten to throw away server cruft from users,
   * combined the hostname validity test with
   * cleanup of host name, so a cleaned up hostname
   * can be returned as an error if necessary. - Dianora
   *
   * yes, the if(strlen) below is really needed!! 
   */
  if (strlen(host) > HOSTLEN)
    host[HOSTLEN] = '\0';

  if (IsPerson(sptr))
    {
      /*
       * a USER tries something fishy... ;-)
       */
      if (IsServer(cptr))
        {
	  sendto_ops_flag(UMODE_SERVNOTICE, "SERVER command from remote user %s -- %s is a hacked server",
			  sptr->name, cptr->name);
        }
      else
        {
          sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                     me.name, parv[0], "SERVER");
        }
      return 0;
    }
  else
    {
      /* Lets check for bogus names and clean them up
       * we don't bother cleaning up ones from users, becasuse
       * we will never see them any more - Dianora
       */

      int bogus_server = 0;
      int dots = 0;
      char *s;

      s = host;
      while (*s)
        {
          if (!IsServChar(*s)) {
            bogus_server = 1;
            break;
          }
          if ('.' == *s)
            ++dots;
          ++s;
        }

      if (!dots || bogus_server )
        {
          char clean_host[2 * HOSTLEN + 4];
          sendto_one(sptr,"ERROR :Bogus server name (%s)", 
                     clean_string(clean_host, (const unsigned char *) host, 2 * HOSTLEN));
          return exit_client(cptr, cptr, cptr, "Bogus server name");
        }
    }

  /* 
   * *WHEN* can it be that "cptr != sptr" ????? --msa
   * When SERVER command (like now) has prefix. -avalon
   * 
   * check to see this host even has an N line before bothering
   * anyone about it. Its only a quick sanity test to stop
   * the conference room and win95 ircd dorks. 
   * Sure, it will be redundantly checked again in m_server_estab()
   * *sigh* yes there will be wasted CPU as the conf list will
   * be scanned twice. But how often will this happen?
   * - Dianora
   *
   * This should (will be) be recoded to check the IP is valid as well, 
   * with a pointer to the valid N line conf kept for later, saving
   * an extra lookup.. *sigh* - Dianora
   */
  if (!IsServer(cptr))
    {
      if (find_conf_by_name(host, CONF_NOCONNECT_SERVER) == NULL)
        {
#ifdef WARN_NO_NLINE
          sendto_ops_flag(UMODE_SERVCONNECT, "Link %s Server %s dropped, no N: line",
			  get_client_name(cptr, MASK_IP), host);
#endif
	  logprintf(L_NOTICE, "Access denied. No N line for server %s",
	      get_client_name(cptr, TRUE));
          return exit_client(cptr, cptr, cptr, "NO N line");
        }
    }

  if (MyConnect(cptr) && (GlobalSetOptions.autoconn == 0))
    {
      sendto_ops_flag(UMODE_SERVCONNECT, "WARNING AUTOCONN is 0, Closing %s",
			     get_client_name(cptr, MASK_IP));
      return exit_client(cptr, cptr, cptr, "AUTOCONNS off");
    }

  if ((acptr = find_server(host)))
    {
      /*
       * This link is trying feed me a server that I already have
       * access through another path -- multiple paths not accepted
       * currently, kill this link immediately!!
       *
       * Rather than KILL the link which introduced it, KILL the
       * youngest of the two links. -avalon
       */
      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
      {
        sendto_ops_flag(UMODE_SERVCONNECT, "Link %s cancelled, server %s already exists",
			get_client_name(bcptr, MASK_IP), host);
        return exit_client(bcptr, bcptr, &me, "Server Exists");
      }
      /*
       * in this case, we are not dropping the link from
       * which we got the SERVER message.  Thus we canNOT
       * `return' yet! -krys
       */
      sendto_ops_flag(UMODE_SERVCONNECT, "Link %s cancelled, server %s reintroduced by %s",
		      bcptr->name, host, cptr->name);
      exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  /* The following if statement would be nice to remove
   * since user nicks never have '.' in them and servers
   * must always have '.' in them. There should never be a 
   * server/nick name collision, but it is possible a capricious
   * server admin could deliberately do something strange.
   *
   * -Dianora
   *
   * This must stay. It also catches things which are not yet
   * registered servers.
   *  -- asuffield
   */
  if ((acptr = find_client(host, NULL)) && acptr != cptr)
    {
      /*
       * Server trying to use the same name as a person. Would
       * cause a fair bit of confusion. Enough to make it hellish
       * for a while and servers to send stuff to the wrong place.
       */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
      sendto_ops_flag(UMODE_SERVCONNECT, "Link %s cancelled: Server/nick collision on %s",
                 /* inpath */ get_client_name(cptr, MASK_IP), host);
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  if (IsServer(cptr))
    {
      /*
       * Server is informing about a new server behind
       * this link. Create REMOTE server structure,
       * add it to list and propagate word to my other
       * server links...
       */
      if (parc == 1 || info[0] == '\0')
        {
          sendto_one(cptr, "ERROR :No server info specified for %s", host);
          return 0;
        }

      /*
       * See if the newly found server is behind a guaranteed
       * leaf (L-line). If so, close the link.
       */
      if ((aconf = find_conf_host(cptr->confs, host, CONF_LEAF)) &&
          (!aconf->port || (hop > aconf->port)))
        {
          sendto_ops_flag(UMODE_SERVCONNECT, "Leaf-only link %s->%s - Closing",
			  get_client_name(cptr, MASK_IP),
			  aconf->host ? aconf->host : "*");
          sendto_one(cptr, "ERROR :Leaf-only link, sorry.");
          return exit_client(cptr, cptr, cptr, "Leaf Only");
        }

      if (!(aconf = find_conf_host(cptr->confs, host, CONF_HUB)) ||
          (aconf->port && (hop > aconf->port)) )
        {
          sendto_ops_flag(UMODE_SERVCONNECT, "Non-Hub link %s introduced %s(%s).",
			  get_client_name(cptr, MASK_IP), host,
			  aconf ? (aconf->host ? aconf->host : "*") : "!");
          sendto_one(cptr, "ERROR :%s has no H: line for %s.",
                     get_client_name(cptr,  FALSE), host);
          return exit_client(cptr, cptr, cptr, "Too many servers");
        }

      acptr = make_client(cptr);
      make_server(acptr);
      acptr->hopcount = hop;
      strncpy_irc(acptr->name, host, HOSTLEN + 1);
      strncpy_irc(acptr->info, info, REALLEN + 1);
      acptr->serv->up = find_or_add(parv[0]);
      acptr->servptr = sptr;

      SetServer(acptr);

      Count.server++;

      add_client_to_list(acptr);
      add_to_client_hash_table(acptr->name, acptr);
      add_client_to_llist(&(acptr->servptr->serv->servers), acptr);

      /*
       * Old sendto_serv_but_one() call removed because we now
       * need to send different names to different servers
       * (domain name matching)
       */
      for (bcptr = serv_cptr_list; bcptr; bcptr = bcptr->next_server_client)
        {
          if (bcptr == cptr)
            continue;
          if (!(aconf = bcptr->serv->nline))
            {
              sendto_ops_flag(UMODE_SERVCONNECT, "Lost N-line for %s on %s. Closing",
				     get_client_name(cptr, MASK_IP), host);
              return exit_client(cptr, cptr, cptr, "Lost N line");
            }
          if (match(my_name_for_link(me.name, aconf), acptr->name))
            continue;

          sendto_one(bcptr, ":%s SERVER %s %d :%s",
                     parv[0], acptr->name, hop + 1, acptr->info);
                         
        }
      
      sendto_local_ops_flag(UMODE_EXTERNAL, "Server %s being introduced by %s",
			    acptr->name, sptr->name);
      return 0;
    }

  if (!IsUnknown(cptr) && !IsHandshake(cptr))
    return 0;
  /*
   * A local link that is still in undefined state wants
   * to be a SERVER, or we have gotten here as a result of a connect
   * Check if this is allowed and change status accordingly...
   */

  /* 
   * Reject a direct nonTS server connection if we're TS_ONLY -orabidoo
   */
  if (!DoesTS(cptr))
    {
      sendto_ops_flag(UMODE_SERVCONNECT, "Link %s dropped, non-TS server",
		      get_client_name(cptr, MASK_IP));
      return exit_client(cptr, cptr, cptr, "Non-TS server");
    }

  /*
   * if we are connecting (Handshake), we already have the name from the
   * C:line in cptr->name
   */
  strncpy_irc(cptr->name, host, HOSTLEN + 1);
  strncpy_irc(cptr->info, info[0] ? info : me.name, REALLEN + 1);
  cptr->hopcount = hop;

  if (check_server(cptr))
    return server_estab(cptr);

  ++ServerStats->is_ref;
  sendto_ops_flag(UMODE_SERVCONNECT, "Received unauthorized connection from %s.",
              get_client_host(cptr));
  return exit_client(cptr, cptr, cptr, "No C/N conf lines");
}


