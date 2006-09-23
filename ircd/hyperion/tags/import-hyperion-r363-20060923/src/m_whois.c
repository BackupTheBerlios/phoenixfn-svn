/************************************************************************
 *   IRC - Internet Relay Chat, src/m_who.c
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
#include "s_conf.h"

#include <string.h>

static char buf[BUFSIZE];

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
** m_whois
**      parv[0] = sender prefix
**      parv[1] = nickname masklist
*/
int     m_whois(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  static anUser UnknownUser =
  {
    NULL,       /* channel */
    /*NULL,*/       /* logging */
    NULL,       /* invited */
    NULL,       /* silence */
    NULL,       /* away */
    0,          /* last */
    0,          /* last_sent */
    1,          /* refcount */
    0,          /* joined */
    /*0,*/          /* logcount */
    "<Unknown>",         /* server */
    ""		/* servlogin */
  };
  Link  *lp;
  anUser        *user;
  struct Client *acptr, *a2cptr;
  aChannel *chptr;
  char  *nick, *name;
  /* char  *tmp; */
  char  *p = NULL;
  int   len, mlen;
  static time_t last_used=0L;
  static int use_count = 0;
  int found_mode;
  int doremote = 0;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }

  if(parc > 2)
    {
      if (!MyConnect(sptr))
        {
	  if (hunt_server(cptr,sptr,":%s WHOIS %s :%s", 1,parc,parv) !=
	      HUNTED_ISME)
	    return 0;
	}
      else
	/* grr, hunt_server does not allow normal local users to specify
	 * the destination server with a nick, so we will have to do
	 * it ourselves -- jilles
	 */
        doremote = 1;
      parv[1] = parv[2];
    }
  else
    {
      /* HACK: if we get a single-argument whois from a remote server
       * make it remote and rate-limited too; this way whois from 1.0.35
       * servers shows exact idle time and is rate-limited (those servers
       * do not limit the amount of remote whois sent out).
       * Rate limits are more severe than in 1.0.35 but only apply on the
       * first hyperion server they pass through.
       * The code returns something to a non-local request here, which
       * might be dangerous.
       * -- jilles */
      if(!MyConnect(sptr))
	doremote = 1;
    }

  if(!NoFloodProtection(sptr) && doremote) /* pace (possibly) remote requests */
    {
      if((last_used + WHOIS_WAIT) > CurrentTime)
        {
	  /* Let 2 go through, every WHOIS_WAIT (2/sec), then drop the rest */
	  if (++use_count > 2)
	    {
              sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0],"WHOIS");
              sendto_one(sptr,form_str(RPL_ENDOFWHOIS),me.name,parv[0],parv[1]);
	      return 0;
	    }
        }
      else
        {
          last_used = CurrentTime;
	  use_count = 0;
        }
    }

  /* Multiple whois from remote hosts, can be used
   * to flood a server off. One could argue that multiple whois on
   * local server could remain. Lets think about that, for now
   * removing it totally. 
   * -Dianora 
   */

  /*  for (tmp = parv[1]; (nick = strtoken(&p, tmp, ",")); tmp = NULL) */
  nick = parv[1];
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';

  /* completely remove wildcard WHOIS -- jilles */
  if (strchr(nick, '?') != NULL || strchr(nick, '*') != NULL)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
		 me.name, parv[0], nick);
      return 0;
    }

  /* If the nick doesn't have any wild cards in it,
   * then just pick it up from the hash table
   * - Dianora 
   */

  acptr = hash_find_client(nick,(struct Client *)NULL);
  if(!acptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
		 me.name, parv[0], nick);
      return 0;
    }
  if(!IsPerson(acptr))
    {
      sendto_one(sptr, form_str(RPL_ENDOFWHOIS),
		 me.name, parv[0], parv[1]);
      return 0;
    }

  user = acptr->user ? acptr->user : &UnknownUser;
  if (!*acptr->name)
    {
      static char q[] = "?";
      /* This should never happen */
      sendto_ops_flag(UMODE_DEBUG, "hash_find_client(%s, NULL) returned me client %p, with 0-length name",
		      nick, (void *)acptr);
      name = q;
    }
  else
    name = acptr->name;

  a2cptr = find_server(user->server);

  /* Allow remote whois for exact idle time. See comment above for
   * why this is so far down. -- jilles */
  if (doremote && !MyClient(acptr) && acptr->name)
    {
      /* send a proper two-argument whois message */
      sendto_one(a2cptr, ":%s WHOIS %s :%s", sptr->name, acptr->name,
	acptr->name);
      return 0;
    }
  
  sendto_one(sptr, form_str(RPL_WHOISUSER), me.name,
	     parv[0], name,
	     acptr->username, acptr->host, acptr->info);

  /* source server name may be changed later by serverhiding code, be sure
   * to leave enough space -- jilles */
  mlen = HOSTLEN + strlen(parv[0]) + 9 +
    strlen(name); /* ":<server> 319 <source> <name> :" */
  for (len = 0, *buf = '\0', lp = user->channel; lp;
       lp = lp->next)
    {
      chptr = lp->value.chptr;
      if (HasUmode(sptr,UMODE_USER_AUSPEX) || IsMember(sptr,chptr) || IsLogger(sptr, chptr) 
	  || (!IsInvisible(acptr) && PubChannel(chptr)))
	{
	  if (len + strlen(chptr->chname) + mlen + 4
	      > (size_t) BUFSIZE) /* space, CRLF and possible @/+ */
	    {
	      sendto_one(sptr,
			 ":%s %d %s %s :%s",
			 me.name,
			 RPL_WHOISCHANNELS,
			 parv[0], name, buf);
	      *buf = '\0';
	      len = 0;
	    }

	  found_mode = user_channel_mode(acptr, chptr);
#ifdef HIDE_OPS
	  if(is_chan_op(sptr,chptr))
#endif
	    {
	      if(found_mode & CHFL_CHANOP)
		*(buf + len++) = '@';
	      else if (found_mode & CHFL_VOICE)
		*(buf + len++) = '+';
	    }
	  if (len)
	    *(buf + len) = '\0';
	  (void)strcpy(buf + len, chptr->chname);
	  len += strlen(chptr->chname);
	  (void)strcat(buf + len, " ");
	  len++;
	}
    }
  if (buf[0] != '\0')
    sendto_one(sptr, form_str(RPL_WHOISCHANNELS),
	       me.name, parv[0], name, buf);
 
#ifdef SERVERHIDE
  if (!(HasUmode(sptr,UMODE_AUSPEX) || acptr == sptr))
    sendto_one(sptr, form_str(RPL_WHOISSERVER),
	       me.name, parv[0], name, NETWORK_NAME,
	       NETWORK_DESC);
  else
#endif
  sendto_one(sptr, form_str(RPL_WHOISSERVER),
	     me.name, parv[0], name, user->server,
	     a2cptr ? a2cptr->info : "*Not On This Net*");
  if (HasUmode(sptr, UMODE_USER_AUSPEX))
    sendto_one(sptr, form_str(RPL_WHOISREALHOST),
	       me.name, sptr->name, name,
	       acptr->dnshost, acptr->sockhost,
	       acptr->origname, acptr->spoofhost);

  if (user->away)
    sendto_one(sptr, form_str(RPL_AWAY), me.name,
	       parv[0], name, user->away);

  if (HasUmode(acptr,UMODE_OPER)
      && (SeesOpers(sptr) || acptr == sptr)
      )
    sendto_one(sptr, form_str(RPL_WHOISOPERATOR),
	       me.name, parv[0], name);
  {
    user_modes umodes;
    AndNotUmodes(umodes, acptr->allowed_umodes, user_umodes);
    ClearBit(umodes, UMODE_OPER);
    if (AnyBits(umodes) && (SeesOperPrivs(sptr) || acptr == sptr))
      sendto_one(sptr, form_str(RPL_WHOISOPER_PRIVS),
		 me.name, parv[0], name, umodes_as_string(&umodes));
  }

  if (HasUmode(acptr,UMODE_IDENTIFIED))
    sendto_one(sptr, form_str(RPL_WHOISIDENTIFIED),
	       me.name, parv[0], name, "is identified to services", "");
	       /* was: "is an identified user" */

  if (acptr->user->servlogin[0] != '\0')
    sendto_one(sptr, form_str(RPL_WHOISIDENTIFIED),
	       me.name, parv[0], name, "is signed on as account",
	       acptr->user->servlogin);
#if 0 /* This one is nicer in theory, but often displayed poorly :( -- jilles */
    sendto_one(sptr, form_str(RPL_WHOISLOGGEDIN),
	       me.name, parv[0], name, acptr->user->servlogin);
#endif

  if (acptr->user && MyConnect(acptr) && (acptr == sptr || HasUmode(sptr,UMODE_AUSPEX) || IsServer(cptr) || doremote))
    sendto_one(sptr, form_str(RPL_WHOISIDLE),
	       me.name, parv[0], name,
	       CurrentTime - user->last,
	       acptr->firsttime);
  sendto_one(sptr, form_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);

  return 0;
}
