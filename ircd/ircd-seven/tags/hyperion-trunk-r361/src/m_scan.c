/************************************************************************
 *   IRC - Internet Relay Chat, src/m_scan.c
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
#include "umodes.h"
#include "m_commands.h"
#include "varparse.h"
#include "channel.h"
#include "umodes.h"
#include "s_conf.h"
#include "m_kline.h"
#include "s_misc.h"

#include <stdlib.h>
#include <string.h>

var_handler_func m_scan_umodes, m_scan_klines, m_scan_cmodes, m_scan_unklines, m_scan_idle;

static struct variable_tier scan_parse_tree[] = {
  {"UMODES", 0, NULL, m_scan_umodes},
  {"CMODES", 0, NULL, m_scan_cmodes},
  {"KLINES", 0, NULL, m_scan_klines},
  {"UNKLINES", 0, NULL, m_scan_unklines},
  {"IDLE", 0, NULL, m_scan_idle},
  {NULL, 0, NULL, NULL}
};

int m_scan(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (IsServer(sptr))
    return 0;
  if (MyClient(sptr) && !HasUmode(sptr,UMODE_EXPERIMENTAL))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no X umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  return variable_parse(cptr, sptr, parc, parv, scan_parse_tree, "SCAN");
}

int
m_scan_umodes(struct Client *cptr, struct Client *sptr, int parc, char *parv[], char *varparv[])
{
  char *umode_string = parv[2], *c;
  user_modes include_modes, exclude_modes;
  int what = MODE_ADD;
  int mode;
  int list_users = 1;
  int list_max = 0;
  int allowed = 1;
  int listed_so_far = 0, count = 0;
  char *mask = NULL;
  struct Client *acptr;
  int i;

  if (!HasUmode(sptr,UMODE_USER_AUSPEX))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no a umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 3)
    {
      if (!IsServer(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "SCAN UMODES");
      return 0;
    }

  ClearBitfield(include_modes);
  ClearBitfield(exclude_modes);

  for (c = umode_string; *c; c++)
    switch(*c)
      {
      case '+':
	what = MODE_ADD;
	break;
      case '-':
	what = MODE_DEL;
	break;
      default:
	if ((mode = user_modes_from_c_to_bitmask[(unsigned char)*c]))
	  {
	    if (what == MODE_ADD)
	      SetBit(include_modes, mode);
	    else
	      SetBit(exclude_modes, mode);
	  }
	else
	  {
	    sendto_one(sptr, form_str(ERR_UNKNOWNMODE), me.name, sptr->name, *c);
	    return 0;
	  }
      }

  for (i = 3; i < parc; i++)
    {
      if (!irccmp(parv[i], "no-list"))
	list_users = 0;
      else if (!irccmp(parv[i], "list"))
	list_users = 1;
      else if (!irccmp(parv[i], "allowed"))
	allowed = 1;
      else if (!irccmp(parv[i], "current"))
	allowed = 0;
      else if (i < (parc - 1))
	{
	  if (!irccmp(parv[i], "list-max"))
	    {
	      list_max = atoi(parv[++i]);
	    }
	  else if (!irccmp(parv[i], "mask"))
	    {
	      mask = parv[++i];
	    }
	}
    }

  for (acptr = GlobalClientList; acptr; acptr = acptr->next)
    {
      char *s;
      user_modes working_umodes;

      if (!IsClient(acptr))
	continue;

      if (allowed)
	AndUmodes(working_umodes, acptr->allowed_umodes, include_modes);
      else
	AndUmodes(working_umodes, acptr->umodes, include_modes);

      if (!SameBits(working_umodes, include_modes))
	continue;

      if (allowed)
	AndUmodes(working_umodes, acptr->allowed_umodes, exclude_modes);
      else
	AndUmodes(working_umodes, acptr->umodes, exclude_modes);

      if (AnyBits(working_umodes))
	continue;

      s = make_nick_user_host(acptr->name, acptr->username,
			      acptr->host);

      if (mask && !match(mask, s))
	continue;

      if (list_users && (!list_max || (listed_so_far < list_max)))
	{
	  char buf[BUFSIZE];
	  char *m = buf;
	  int i;
	  *m++ = '+';
	  for (i = 0; user_mode_table[i].letter && (m - buf < BUFSIZE - 4);i++)
	    if (TestBit(allowed ? acptr->allowed_umodes : acptr->umodes, user_mode_table[i].mode))
	      *m++ = user_mode_table[i].letter;
	  *m = '\0';
	  listed_so_far++;
	  sendto_one(sptr, form_str(RPL_MODE),
		     me.name, sptr->name,
		     acptr->name, buf);
	}
      count++;
    }

  send_markup(sptr, &me, "UMODE-END", "End of user mode list");
/*   send_markup(sptr, &me, "SCAN-SUMMARY", "!begin<1>%d!end<1> matched", count); */
  send_markup(sptr, &me, "SCAN-SUMMARY", "%d matched", count);

  return 0;
}

int
m_scan_cmodes(struct Client *cptr, struct Client *sptr, int parc, char *parv[], char *varparv[])
{
  char *cmode_string = parv[2], *c;
  unsigned int include_modes, exclude_modes;
  int require_autodline = 0, require_forward = 0;
  int what = MODE_ADD;
  int list_channels = 1;
  int list_max = 0;
  int listed_so_far = 0, count = 0;
  char *mask = NULL;
  struct Channel *achptr;
  int i;

  if (!HasUmode(sptr,UMODE_USER_AUSPEX))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no a umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 3)
    {
      if (!IsServer(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "SCAN CMODES");
      return 0;
    }

  include_modes = exclude_modes = 0;

  for (c = cmode_string; *c; c++)
    switch(*c)
      {
      case '+':
	what = MODE_ADD;
	break;
      case '-':
	what = MODE_DEL;
	break;
      default:
        {
          int this_mode = 0;
          switch(*c)
            {
            case 'c':
              this_mode = MODE_NOCOLOR;
              break;
            case 'D':
	      require_autodline = what == MODE_ADD ? 1 : -1;
              break;
#ifdef INVITE_CHANNEL_FORWARDING
            case 'f':
	      require_forward = what == MODE_ADD ? 1 : -1;
              break;
#endif
            case 'g':
              this_mode = MODE_FREEINVITE;
              break;
            case 'i':
              this_mode = MODE_INVITEONLY;
              break;
#ifdef JUPE_CHANNEL
            case 'j':
              this_mode = MODE_JUPED;
              break;
#endif
            case 'L':
              this_mode = MODE_LARGEBANLIST;
              break;
            case 'm':
              this_mode = MODE_MODERATED;
              break;
            case 'n':
              this_mode = MODE_NOPRIVMSGS;
              break;
            case 'P':
              this_mode = MODE_PERM;
              break;
            case 'Q':
              this_mode = MODE_NOFORWARD;
              break;
            case 'r':
              this_mode = MODE_NOUNIDENT;
              break;
            case 'R':
              this_mode = MODE_QUIETUNIDENT;
              break;
            case 's':
              this_mode = MODE_SECRET;
              break;
            case 't':
              this_mode = MODE_TOPICLIMIT;
              break;
            case 'z':
              this_mode = MODE_OPMODERATE;
              break;
            default:
	      sendto_one(sptr, form_str(ERR_UNKNOWNMODE), me.name, sptr->name, *c);
	      return 0;
            }

          if (what == MODE_ADD)
            include_modes |= this_mode;
          else
            exclude_modes |= this_mode;
        }
      }

  for (i = 3; i < parc; i++)
    {
      if (!irccmp(parv[i], "no-list"))
	list_channels = 0;
      else if (!irccmp(parv[i], "list"))
	list_channels = 1;
      else if (i < (parc - 1))
	{
	  if (!irccmp(parv[i], "list-max"))
	    {
	      list_max = strtoul(parv[++i], NULL, 0);
	    }
	  else if (!irccmp(parv[i], "mask"))
	    {
	      mask = parv[++i];
	    }
	}
    }

  for (achptr = channel; achptr; achptr = achptr->nextch)
    {
      if ((achptr->mode.mode & include_modes) != include_modes)
        continue;

      if ((achptr->mode.mode & exclude_modes))
        continue;

#ifdef INVITE_CHANNEL_FORWARDING
      if (achptr->mode.invite_forward_channel_name[0] && require_forward == -1)
	continue;
      if (!achptr->mode.invite_forward_channel_name[0] && require_forward == 1)
	continue;
#endif

      if (achptr->mode.autodline_frequency && require_autodline == -1)
	continue;
      if (!achptr->mode.autodline_frequency && require_autodline == 1)
	continue;

      if (list_channels && (!list_max || (listed_so_far < list_max)))
	{
          static char     modebuf[MODEBUFLEN];
          static char     parabuf[MODEBUFLEN];
	  listed_so_far++;
          *modebuf = *parabuf = '\0';
          modebuf[1] = '\0';
          channel_modes(sptr, modebuf, parabuf, achptr);
	  sendto_one(sptr, form_str(RPL_CHANNELMODEIS),
                       me.name, sptr->name,
                       achptr->chname, modebuf, parabuf);
	}
      count++;
    }

  send_markup(sptr, &me, "CMODE-END", "End of channel mode list");
/*   send_markup(sptr, &me, "SCAN-SUMMARY", "!begin<1>%d!end<1> matched", count); */
  send_markup(sptr, &me, "SCAN-SUMMARY", "%d matched", count);

  return 0;
}

int
m_scan_klines(struct Client *cptr, struct Client *sptr, int parc, char *parv[], char *varparv[])
{
  char *host_mask = NULL, *user_mask = NULL;
  int list = 1, count = 0, listed_so_far = 0;
  int list_max = 100;
  int expired = 0;
  char *placed_by = NULL;
  int i;
  struct ConfItem *aconf;

  if (!HasUmode(sptr,UMODE_SEEKLINES))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no 2 umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 2)
    {
      if (!IsServer(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "SCAN KLINES");
      return 0;
    }

  for (i = 2; i < parc; i++)
    {
      if (!irccmp(parv[i], "no-list"))
	list = 0;
      else if (!irccmp(parv[i], "list"))
	list = 1;
      else if (!irccmp(parv[i], "not-expired"))
	expired = 0;
      else if (!irccmp(parv[i], "expired"))
	expired = 1;
      else if (i < (parc - 1))
	{
	  if (!irccmp(parv[i], "list-max"))
	    {
	      list_max = strtoul(parv[++i], NULL, 0);
	    }
	  else if (!irccmp(parv[i], "host-mask"))
	    {
	      host_mask = parv[++i];
	    }
	  else if (!irccmp(parv[i], "user-mask"))
	    {
	      user_mask = parv[++i];
	    }
	  else if (!irccmp(parv[i], "placed-by"))
	    {
	      placed_by = parv[++i];
	    }
	}
    }

  for (aconf = kline_list; aconf; aconf = aconf->kline_next)
    {
      char *p;
      if (aconf->status != CONF_KILL)
	continue;
      if (host_mask && !match(host_mask, aconf->host))
	continue;
      if (user_mask && !match(user_mask, aconf->user))
	continue;
      /* extract the user who placed the K:line */
      if ((p = strchr(aconf->passwd, ';')))
	{
	  int skip = 0;
	  *p = '\0';
	  skip = placed_by && !match(placed_by, aconf->passwd);
	  *p = ';';
	  if (skip)
	    continue;
	}
      /* true if:
       *            (can expire     (and has expired           ))
       * (we want expired and have expired, or vice versa        )
       */
      if (expired ^ (aconf->hold && (aconf->hold <= CurrentTime)))
	continue;

      count++;
      if (list && (list_max > ++listed_so_far))
	{
          /* p points to the semicolon in the comment field, if there is one */
	  if (p)
	    *p = '\0';
          if (aconf->hold)
            {
              if (p)
                {
/*                   send_markup(sptr, &me, "SCAN-KLINE", */
/*                               "!begin<1>%s@%s!end<1> klined " */
/*                               "until !begin<2>!date<%ld>!end<2> (!begin<3>!time<%ld>!end<3>) " */
/*                               "by !begin<4>%s!end<4>, because: %s", */
/*                               aconf->user, aconf->host, aconf->hold, aconf->hold - CurrentTime, aconf->passwd, p + 2); */
                  send_markup(sptr, &me, "SCAN-KLINE",
                              "%s@%s klined "
                              "until %s (%s) "
                              "by %s, because: %s",
                              aconf->user, aconf->host, aconf->hold == 1 ? "?" : smalldate(aconf->hold), aconf->hold == 1 ? "unklined" : smalltime(aconf->hold - CurrentTime), aconf->passwd, p + 2);
                }
              else
                {
/*                   send_markup(sptr, &me, "SCAN-KLINE", */
/*                               "!begin<1>%s@%s!end<1> klined " */
/*                               "until !begin<2>!date<%ld>!end<2> (!begin<3>!time<%ld>!end<3>) " */
/*                               "by !begin<4>%s!end<4>", */
/*                               aconf->user, aconf->host, aconf->hold, aconf->hold - CurrentTime, aconf->passwd); */
                  send_markup(sptr, &me, "SCAN-KLINE",
                              "%s@%s klined "
                              "until %s (%s) "
                              "by %s",
                              aconf->user, aconf->host, aconf->hold == 1 ? "?" : smalldate(aconf->hold), aconf->hold == 1 ? "unklined" : smalltime(aconf->hold - CurrentTime), aconf->passwd);
                }
            }
          else
            {
              if (p)
                {
/*                   send_markup(sptr, &me, "SCAN-KLINE", */
/*                               "!begin<1>%s@%s!end<1> klined !begin<2>permanently!end<2> " */
/*                               "by !begin<4>%s!end<4>, because: %s", */
/*                               aconf->user, aconf->host, aconf->passwd, p + 2); */
                  send_markup(sptr, &me, "SCAN-KLINE",
                              "%s@%s klined permanently "
                              "by %s, because: %s",
                              aconf->user, aconf->host, aconf->passwd, p + 2);
                }
              else
                {
/*                   send_markup(sptr, &me, "SCAN-KLINE", */
/*                               "!begin<1>%s@%s!end<1> klined !begin<2>permanently!end<2> " */
/*                               "by !begin<4>%s!end<4>", */
/*                               aconf->user, aconf->host, aconf->passwd); */
                  send_markup(sptr, &me, "SCAN-KLINE",
                              "%s@%s klined permanently "
                              "by %s",
                              aconf->user, aconf->host, aconf->passwd);
                }
            }
	  if (p)
	    {
	      *p = ';';
	    }
	}
    }

  send_markup(sptr, &me, "KLINE-END", "End of kline list");
/*   send_markup(sptr, &me, "SCAN-SUMMARY", "!begin<1>%d!end<1> matched", count); */
  send_markup(sptr, &me, "SCAN-SUMMARY", "%d matched", count);

  return 0;
}

int
m_scan_unklines(struct Client *cptr, struct Client *sptr, int parc, char *parv[], char *varparv[])
{
  char *mask = NULL;
  int list = 1, count = 0, listed_so_far = 0;
  int list_max = 100;
  int i;
  struct unkline_record **ukr, *ukr2;

  if (!HasUmode(sptr,UMODE_SEEKLINES))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no 2 umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 2)
    {
      if (!IsServer(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "SCAN UNKLINES");
      return 0;
    }

  for (i = 2; i < parc; i++)
    {
      if (!irccmp(parv[i], "no-list"))
	list = 0;
      else if (!irccmp(parv[i], "list"))
	list = 1;
      else if (i < (parc - 1))
	{
	  if (!irccmp(parv[i], "list-max"))
	    {
	      list_max = atoi(parv[++i]);
	    }
	  else if (!irccmp(parv[i], "mask"))
	    {
	      mask = parv[++i];
	    }
	}
    }

  for (ukr = &recorded_unklines; (ukr2 = *ukr); ukr = &ukr2->next)
    {
      if ((ukr2->placed + UNKLINE_CACHE_TIME) < CurrentTime)
	  {
	    *ukr = ukr2->next;
	    MyFree(ukr2->mask);
	    MyFree(ukr2);
	    /* And put stuff back, safety in case we can't loop again */
	    if (!(ukr2 = *ukr))
	      break;
	  }
      else
        {
          if (mask && !match(mask, ukr2->mask))
            continue;
          count++;
          if (list && (list_max > ++listed_so_far))
/*             send_markup(sptr, &me, "SCAN-UNKLINE", */
/*                         "!begin<1>%s!end<1> unklined at !begin<2>!date<%ld>!end<2> (!begin<3>!time<%ld>!end<3>)", */
/*                         ukr2->mask, ukr2->placed, ukr2->placed - CurrentTime); */
            send_markup(sptr, &me, "SCAN-UNKLINE",
                        "%s unklined at %s (%s ago)",
                        ukr2->mask, smalldate(ukr2->placed), smalltime(CurrentTime - ukr2->placed));
        }
    }

  send_markup(sptr, &me, "UNKLINE-END", "End of unkline list");
/*   send_markup(sptr, &me, "SCAN-SUMMARY", "!begin<1>%d!end<1> matched", count); */
  send_markup(sptr, &me, "SCAN-SUMMARY", "%d matched", count);

  return 0;
}

int
m_scan_idle(struct Client *cptr, struct Client *sptr, int parc, char *parv[], char *varparv[])
{
	struct Client *ptr, *target = NULL;
	char *eptr, buffer[321];
	int idle_time, check_time, len = 0, count = 0;

	if(MyClient(sptr) && !HasUmode(sptr, UMODE_USER_AUSPEX))
	{
		if(SeesOperMessages(sptr))
			sendto_one(sptr,":%s NOTICE %s :You have no a umode", me.name, parv[0]);
		else
			sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
		return 0;
	}

	if(parc < 3)
	{
		if (!IsServer(sptr))
			sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
					me.name, parv[0], "SCAN IDLE");
		return 0;
	}

	idle_time = strtoul(parv[2], &eptr, 10);
	if(eptr == parv[2])
		return 0;
	/* Store the timestamp which last_sent should be >= to save time */
	check_time = CurrentTime - idle_time;

	/* If the query is for another server, pass it on and return. */
	if(parc > 3)
	{
		if(MyClient(sptr) && !HasUmode(sptr, UMODE_REMOTEINFO))
		{
			if(SeesOperMessages(sptr))
				sendto_one(sptr,":%s NOTICE %s :You have no S umode(cannot send remote)", me.name, parv[0]);
			else
				sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
			return 0;
		}

		if(!irccmp(parv[3], "GLOBAL") || !irccmp(parv[3], "*"))
			sendto_serv_butone(cptr, ":%s SCAN IDLE %d *", parv[0], idle_time);
		else if((target = find_server(parv[3])) == NULL) {
			sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
					me.name, parv[0], parv[3]);
			return 0;
		}else if(!IsMe(target)) { /* But only if the query is not on us... */

			sendto_prefix_one(target, sptr, ":%s SCAN IDLE %d %s", parv[0],
					idle_time, target->name);
			return 0;
		}
	}

	buffer[0] = '\0';
	for(ptr = local_cptr_list; ptr; ptr = ptr->next_local_client)
	{
		if(ptr->user->last_sent < check_time)
			continue;

		if(len + strlen(ptr->name) > 319)
		{
			buffer[len - 1] = '\0'; /* Strip the trailing space */
			send_markup(sptr, &me, "SCAN-IDLE", "%d %s", idle_time, buffer);
			buffer[0] = '\0';
			len = 0;
		}

		strcat(buffer, ptr->name);
		strcat(buffer, " ");
		len += strlen(ptr->name) + 1;
		count++;
	}

	if(buffer[0])
	{
		buffer[len - 1] = '\0';
		send_markup(sptr, &me, "SCAN-IDLE", "%d %s", idle_time, buffer);
	}

	send_markup(sptr, &me, "IDLE-END", "End of idle listing");
	if(count > 0 || target || parc == 3) /* Don't give a summary for globals if no results matched */
		send_markup(sptr, &me, "SCAN-SUMMARY", "%d matched", count);

	return 0;
}

