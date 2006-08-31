/************************************************************************
 *   IRC - Internet Relay Chat, src/m_message.c
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
#include "flud.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "channel.h"
#include "irc_string.h"
#include "hash.h"
#include "class.h"
#include "s_user.h"
#include "umodes.h"
#include "s_log.h"

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
** m_message (used in m_private() and m_notice())
** the general function to deliver MSG's between users/channels
**
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
*/

static  int     m_message(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[],
                          int notice)
{
  struct Client       *acptr;
#ifdef NEED_TLD_FOR_MASS_NOTICE
  char  *s;
#endif
  struct Channel *chptr;
  char  *nick, *server, *host;
  char  errbuf[BUFSIZE];
  const char *cmd;
  int type=0, msgs=0;
#ifdef FLUD
  int flud;
#endif

  cmd = notice ? MSG_NOTICE : MSG_PRIVATE;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], cmd);
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  if (MyConnect(sptr))
    {
#ifdef ANTI_SPAMBOT
#ifndef ANTI_SPAMBOT_WARN_ONLY
      /* if its a spambot, just ignore it */
      if(sptr->join_leave_count >= MAX_JOIN_LEAVE_COUNT)
        return 0;
#endif
#endif
#ifdef NO_DUPE_MULTI_MESSAGES
      if (strchr(parv[1],','))
        parv[1] = canonize(parv[1]);
#endif
    }


  /*
  ** channels are privmsg'd a lot more than other clients, moved up here
  ** plain old channel msg ?
  */
  while(msgs < MAX_MULTI_MESSAGES)
  {
     if(!msgs)
        nick = strtok(parv[1], ",");
     else
        nick = strtok(NULL, ",");

     if(!nick && msgs == 0)
        nick = parv[1];
     else if(!nick)
        break;

  if( IsChanPrefix(*nick) && ((chptr = hash_find_channel(nick, NullChn))))
    {
#ifdef FLUD
      if (MyClient(sptr)) {
#ifdef DEATHFLUD
      if(!notice && check_for_ctcp(parv[2])
         && check_for_flud(sptr, NULL, chptr, 1))
        return 0;
      if((flud = check_for_spam(sptr, NULL, chptr, parv[2])))
        {
          if (check_for_flud(sptr, NULL, chptr, flud))
            return 0;
        }
#else /* DEATHFLUD */
      if(!notice)
	if(check_for_ctcp(parv[2]))
	  check_for_flud(sptr, NULL, chptr, 1);
#endif /* DEATHFLUD */
      }
#endif /* FLUD */

      /* 
       * Channel color blocking. Usually set with the +c chanmode.
       * - Andre Guibert de Bruet <webdev@cheetah.properkernel.com>
       */
      if(chptr->mode.mode & MODE_NOCOLOR)
	strip_colour(parv[2]);

      switch (can_send(sptr, chptr))
        {
        case 0:
          sendto_channel_message_butone(cptr, sptr, chptr, cmd, parv[2]);
          break;
        case MODE_QUIETUNIDENT:
          if (!notice)
            sendto_one(sptr, form_str(ERR_QUIETUNIDENT),
                       me.name, parv[0], nick);
          break;
        case MODE_MODERATED:
          if (chptr->mode.mode & MODE_OPMODERATE)
            {
              /* The flag MODE_OPMODERATE will instruct sendto_channel_type()
	       * to put bare #channel in the message (instead of @#channel);
	       * it will still be sent to ops and servers with ops only.
	       * Strange things will happen if the user is not banned
	       * remotely.
	       * -- jilles
	       */
              sendto_channel_type(cptr, sptr, chptr,
			      MODE_CHANOP | MODE_OPMODERATE, nick, cmd,
			      parv[2]);
            }
          else
            {
              if (!notice)
                sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
                           me.name, parv[0], nick);
            }
          break;
        default:
          if (!notice)
            sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
                       me.name, parv[0], nick);
          break;
        }
      msgs++;
      continue;
    }
      
  /*
  ** @# type of channel msg?
  */

  if(*nick == '@')
    type = MODE_CHANOP;
  else if(*nick == '+')
    type = MODE_CHANOP|MODE_VOICE;

  if(type)
    {
      /* Strip if using DALnet chanop/voice prefix. */
      if (*(nick+1) == '@' || *(nick+1) == '+')
        {
          nick++;
          *nick = '@';
          type = MODE_CHANOP|MODE_VOICE;
        }

      /* suggested by Mortiis */
      if(!*nick)        /* if its a '\0' dump it, there is no recipient */
        {
          sendto_one(sptr, form_str(ERR_NORECIPIENT),
                     me.name, parv[0], cmd);
          return -1;
        }

      /* At this point, nick+1 should be a channel name i.e. #foo or &foo
       * if the channel is found, fine, if not report an error
       */

      if ( (chptr = hash_find_channel(nick+1, NullChn)) )
        {
#ifdef FLUD
          if (MyClient(sptr)) {
#ifdef DEATHFLUD
          if(!notice && check_for_ctcp(parv[2])
             && check_for_flud(sptr, NULL, chptr, 1))
            return 0;
          if((flud = check_for_spam(sptr, NULL, chptr, parv[2])))
            {
              if (check_for_flud(sptr, NULL, chptr, flud))
                return 0;
            }
#else /* DEATHFLUD */
          if(!notice)
            if(check_for_ctcp(parv[2]))
              check_for_flud(sptr, NULL, chptr, 1);
#endif /* DEATHFLUD */
          }
#endif /* FLUD */

          if (!IsServer(sptr) && !is_chan_op(sptr,chptr))
            {
              if (!notice)
                {
                  sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
                             me.name, parv[0], nick);
                }
	msgs++;
	continue;
            }
          else
            {
              sendto_channel_type(cptr,
                                  sptr,
                                  chptr,
                                  type,
                                  nick+1,
                                  cmd,
                                  parv[2]);
            }
        }
      else
        {
	  if (!IsServer(sptr))
	    sendto_one(sptr, form_str(ERR_NOSUCHNICK),
		       me.name, parv[0], nick);
	  msgs++;
	  continue;
        }
      return 0;
    }

  /*
  ** nickname addressed?
  */
  if ((acptr = find_person(nick, NULL)))
    {
#ifdef FLUD
#ifdef DEATHFLUD
      if(!notice && MyConnect(sptr) && check_for_ctcp(parv[2])
         && check_for_flud(sptr, acptr, NULL, 1))
        return 0;
      if(MyConnect(sptr) && (flud = check_for_spam(sptr, acptr, NULL, parv[2])))
        {
          if (check_for_flud(sptr, acptr, NULL, flud))
            return 0;
        }
#else /* DEATHFLUD */
      if(!notice && MyConnect(sptr))
	if(check_for_ctcp(parv[2]))
	  if(check_for_flud(sptr, acptr, NULL, 1))
	    return 0;
#endif /* DEATHFLUD */
#endif /* FLUD */
#ifdef ANTI_DRONE_FLOOD
      if(MyConnect(acptr) && IsClient(sptr) && !NoFloodProtection(sptr) && DRONETIME)
        {
          if((acptr->first_received_message_time+DRONETIME) < CurrentTime)
            {
              acptr->received_number_of_privmsgs=1;
              acptr->first_received_message_time = CurrentTime;
              acptr->drone_noticed = 0;
            }
          else
            {
              if(acptr->received_number_of_privmsgs > DRONECOUNT)
                {
                  if(acptr->drone_noticed == 0) /* tiny FSM */
                    {
                      sendto_ops_flag(UMODE_BOTS,
				      "Possible Drone Flooder %s [%s@%s] on %s target: %s",
				      sptr->name, sptr->username,
				      sptr->host,
				      sptr->user->server, acptr->name);
                      acptr->drone_noticed = 1;
                    }
                  /* heuristic here, if target has been getting a lot
                   * of privmsgs from clients, and sendq is above halfway up
                   * its allowed sendq, then throw away the privmsg, otherwise
                   * let it through. This adds some protection, yet doesn't
                   * DOS the client.
                   * -Dianora
                   */
                  if(DBufLength(&acptr->sendQ) > (get_sendq(acptr)/2UL))
                    {
                      if(acptr->drone_noticed == 1) /* tiny FSM */
                        {
                          sendto_ops_flag(UMODE_BOTS,
					  "ANTI_DRONE_FLOOD SendQ protection activated for %s",
					  acptr->name);

                          sendto_one(acptr,     
				     ":%s NOTICE %s :*** Notice -- Server drone flood protection activated for %s",
                                     me.name, acptr->name, acptr->name);
                          acptr->drone_noticed = 2;
                        }
                    }

                  if(DBufLength(&acptr->sendQ) <= (get_sendq(acptr)/4UL))
                    {
                      if(acptr->drone_noticed == 2)
                        {
                          sendto_one(acptr,     
                                     ":%s NOTICE %s :*** Notice -- Server drone flood protection de-activated for %s",
                                     me.name, acptr->name, acptr->name);
                          acptr->drone_noticed = 1;
                        }
                    }
                  if(acptr->drone_noticed > 1)
                    return 0;
                }
              else
                acptr->received_number_of_privmsgs++;
            }
        }
#endif

      /*
       * Block the message if the target is +E, we're not identified,
       * and neither are +6.
       *
       * If NOIDPRIVMSG is enabled, treat all unidentified users as +E.
       */
      if (MyClient(sptr) && sptr != acptr
	   && (HasUmode(acptr,UMODE_BLOCK_NOTID) || (GlobalSetOptions.noidprivmsg && !HasUmode(acptr,UMODE_IDENTIFIED) && acptr->user && !acptr->user->servlogin[0]))
	   && !HasUmode(sptr,UMODE_IDENTIFIED)
	   && !sptr->user->servlogin[0]
	   && !HasUmode(acptr,UMODE_DONTBLOCK)
	   && !HasUmode(sptr,UMODE_DONTBLOCK))
        {
	  /* Replace errbuf with either the default or custom message,
	   * then send the numeric on...
	   *    --nenolod
	   */
	  if (GlobalSetOptions.noidprivmsg != 0 &&
		GlobalSetOptions.noidprivmsg_notice[0])
	    {
	      strncpy_irc(errbuf, GlobalSetOptions.noidprivmsg_notice, BUFSIZE);
	    }
	  else
	    {
	      ircsnprintf(errbuf, BUFSIZE, get_str(STR_NOTID_DEFAULT),
		nick);
	    }

          sendto_one(sptr, form_str(ERR_BLOCKING_NOTID),
	      me.name, parv[0], acptr->name, errbuf);
	  return 0;
        }

#if 0 /* This is too strong, disable it for now -- jilles */
      /* Don't send the message if they can't respond to us. -- gxti */
      if (MyClient(sptr) && sptr != acptr
	   && HasUmode(sptr,UMODE_BLOCK_NOTID)
	   && !HasUmode(acptr,UMODE_IDENTIFIED)
	   && !acptr->user->servlogin[0]
	   && !HasUmode(sptr,UMODE_DONTBLOCK)
	   && !HasUmode(acptr,UMODE_DONTBLOCK))
	{
	   ircsnprintf(errbuf, BUFSIZE, get_str(STR_NOTID_SENDER),
		nick);
	   sendto_one(sptr, form_str(ERR_BLOCKING_NOTID),
		me.name, parv[0], acptr->name, errbuf);
	   return 0;
	}
#endif

#ifdef  SILENCE
      /* only check silence masks at the recipient's server -- jilles */
      if (!MyConnect(acptr) || !is_silenced(sptr, acptr)) {
#endif
        if (MyConnect(sptr) && acptr->user && (sptr != acptr))
          {
#ifdef  NCTCP
            /* NCTCP (umode +C) checks  -- PMA */
            if (parv[2][0] == 1) /* is CTCP */
	      /* Huh? No way, NOCTCP means NOCTCP. */
/*               if (!HasUmode(sptr,UMODE_IMMUNE) && */
/*                   !HasUmode(acptr,UMODE_IMMUNE)) */
	      if (HasUmode(acptr,UMODE_NOCTCP) ||            /* block to +C */
		  (notice && HasUmode(sptr,UMODE_NOCTCP)))   /* block replies from +C */
		return 0;                                    /* kill it! */
#endif /* NCTCP */
            if (!notice && acptr->user->away)
              sendto_one(sptr, form_str(RPL_AWAY), me.name,
                         parv[0], acptr->name,
                         acptr->user->away);
          }
        {
          /* here's where we actually send the message */
          int is_ctcp = check_for_ctcp(parv[2]);
          int cap = is_ctcp ? CAP_IDENTIFY_CTCP : CAP_IDENTIFY_MSG;
          sendto_prefix_one(acptr, sptr, ":%s %s %s :%s%s",
                            parv[0], cmd, nick,
                            !(acptr->caps & cap) ? "" :
                            (HasUmode(sptr, UMODE_IDENTIFIED) ? "+" : "-"),
                            parv[2]);
        }
#ifdef SILENCE
      }
#endif

      msgs++;
      continue;
    }

  /* Everything below here should be reserved for opers 
   * as pointed out by Mortiis, user%host.name@server.name 
   * syntax could be used to flood without FLUD protection
   * its also a delightful way for non-opers to find users who
   * have changed nicks -Dianora
   *
   * Grrr it was pointed out to me that x@service is valid
   * for non-opers too, and wouldn't allow for flooding/stalking
   * -Dianora
   *
   * Valid or not, @servername is unacceptable, it reveals what server
   * a person is on. Auspexen only.
   *  -- asuffield
   */

        
  /*
  ** the following two cases allow masks in NOTICEs
  ** (for OPERs only) (with +M -- asuffield)
  **
  ** Armin, 8Jun90 (gruner@informatik.tu-muenchen.de)
  */
  if ((*nick == '$' || *nick == '>'))
    {

      if(!HasUmode(sptr,UMODE_MASSNOTICE))
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], nick);
          return -1;
        }

#ifdef NEED_TLD_FOR_MASS_NOTICE
      if (!(s = (char *)strrchr(nick, '.')))
        {
          sendto_one(sptr, form_str(ERR_NOTOPLEVEL),
                     me.name, parv[0], nick);
          msgs++;
	  continue;
        }
      while (*++s)
        if (*s == '.' || *s == '*' || *s == '?')
          break;
      if (*s == '*' || *s == '?')
        {
          sendto_one(sptr, form_str(ERR_WILDTOPLEVEL),
                     me.name, parv[0], nick);
	  msgs++;
	  continue;
        }
#endif /* NEED_TLD_FOR_MASS_NOTICE */
        
      sendto_match_butone(IsServer(cptr) ? cptr : NULL, 
                          sptr, nick + 1,
                          (*nick == '>') ? MATCH_HOST :
                          MATCH_SERVER,
                          ":%s %s %s :%s", parv[0],
                          cmd, nick, parv[2]);
      msgs++;
      continue;
    }
        
  /*
  ** user[%host]@server addressed?
  */
  if ((server = (char *)strchr(nick, '@')) &&
      (acptr = find_server(server + 1)))
    {
      int count = 0;

      /* Disable the whole farping mess for non-auspexen
       *  -- asuffield
       */
      if (!HasUmode(sptr,UMODE_AUSPEX))
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], nick);
 	  msgs++;
	  continue;
        }

      /* Disable the user%host@server form for non-opers
       * -Dianora
       */

      /* Disabled. This isn't very useful and I don't feel like mucking around with privs for it
       *  -- asuffield */
      if((char *)strchr(nick,'%'))
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], nick);
 	  msgs++;
	  continue;
        }
        
      /*
      ** Not destined for a user on me :-(
      */
      if (!IsMe(acptr))
        {
          sendto_one(acptr,":%s %s %s :%s", parv[0],
                     cmd, nick, parv[2]);
          msgs++;
          continue;
        }

      *server = '\0';

      /* special case opers@server */
      /* We don't want this on OPN -- asuffield */
#if 0
      if(!irccmp(nick,"opers") && SendWallops(sptr))
        {
          sendto_realops("To opers: From %s: %s",sptr->name,parv[2]);
          msgs++;
          continue;
        }
#endif
        
      if ((host = (char *)strchr(nick, '%')))
        *host++ = '\0';

      /*
      ** Look for users which match the destination host
      ** (no host == wildcard) and if one and one only is
      ** found connected to me, deliver message!
      */
      acptr = find_userhost(nick, host, NULL, &count);
      if (server)
        *server = '@';
      if (host)
        *--host = '%';
      if (acptr)
        {
          if (count == 1)
            sendto_prefix_one(acptr, sptr,
                              ":%s %s %s :%s",
                              parv[0], cmd,
                              nick, parv[2]);
          else if (!notice)
            sendto_one(sptr,
                       form_str(ERR_TOOMANYTARGETS),
                       me.name, parv[0], nick, MAX_MULTI_MESSAGES);
        }
      if (acptr)
	{
	  msgs++;
	  continue;
	}
    }
  /* Let's not send these remotely for channels */
  /* No, just send it; if the net is desynched that badly, let the user
   * know -- jilles */
  sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, parv[0], nick);
  msgs++;
  }
  if (strtok(NULL, ","))
    sendto_one(sptr, form_str(ERR_TOOMANYTARGETS),
                     me.name, parv[0], cmd, MAX_MULTI_MESSAGES);
  return 0;
}

/*
** m_private
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
*/

int     m_private(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  return m_message(cptr, sptr, parc, parv, 0);
}

/*
** m_notice
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = notice text
*/

int     m_notice(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  if (!IsRegistered(cptr))
    return 0;
  return m_message(cptr, sptr, parc, parv, 1);
}
