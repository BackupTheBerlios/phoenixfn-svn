/************************************************************************
 *   IRC - Internet Relay Chat, src/channel.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "s_log.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"
#include "m_commands.h"
#include "umodes.h"
#include "flud.h"
#include "m_kline.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef NEED_SPLITCODE

static void check_still_split(void);
int server_was_split=YES;
int got_server_pong;
time_t server_split_time;

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
struct Channel *empty_channel_list=(struct Channel*)NULL;
void remove_empty_channels();
#endif
#endif

struct Channel *channel = NullChn;

#ifndef STRIP_MISC
# define strip_colour(X) (X)
#endif

static  void    add_invite (struct Client *, struct Channel *);
static  int     add_id(struct Client *, struct Channel *, const char *, int, const char *);
static  int     del_id (struct Channel *, const char *, int);
static  void    free_bans_exceptions_denies(struct Channel *);
static  int     is_banned (struct Client *, struct Channel *);
static  void    sub1_from_channel (struct Channel *);

#if defined(INVITE_CHANNEL_FORWARDING) || defined(BAN_CHANNEL_FORWARDING)
int m_join_redirect(struct Client *, struct Client *, char *, char *, struct Channel*);
void attempt_channel_redirection(struct Client *, struct Client *, char *, int, char *, struct Channel *, int);
#endif
struct Channel *get_ban_channel_forward(struct Client *,struct Channel *);

#ifdef BAN_CHANNEL_FORWARDING
char *banstr_remove_channel(char *);
char *banstr_get_channel(const char *);
void add_ban_channel_forward(struct Channel *, const char *, const char *);
void del_ban_channel_forward(struct Channel *, const char *);

char forward_clear[32];
#endif

/* static functions used in set_mode */
static char *fix_key(char *);
static char *fix_key_old(char *);
static void collapse_signs(char *);
static int errsent(int,int *);
static void change_chan_flag(struct Channel *, struct Client *, int );
static void set_deopped(struct Client *,struct Channel *,int);

static  const char    *PartFmt = ":%s PART %s :%s";
/*
 * some buffers for rebuilding channel/nick lists with ,'s
 */

static  char    buf[BUFSIZE];
static  char    modebuf[MODEBUFLEN], modebuf2[MODEBUFLEN];
static  char    parabuf[MODEBUFLEN], parabuf2[MODEBUFLEN];
static  char    clmodebuf[MODEBUFLEN], clparabuf[MODEBUFLEN];
static  char    svmodebuf[MODEBUFLEN], svparabuf[MODEBUFLEN];
static  char    modetmp[MODEBUFLEN], paratmp[MODEBUFLEN];
static  char    maskbuf[256]; /* used in set_channel_mode() when constructing banlist (better ideas?) -- asuffield */
static  char    reasonbuf[1024];


/* 
 * return the length (>=0) of a chain of links.
 */
static  int     list_length(Link *lp)
{
  int   count = 0;

  for (; lp; lp = lp->next)
    count++;
  return count;
}

/*
 *  Fixes a string so that the first white space found becomes an end of
 * string marker (`\0`).  returns the 'fixed' string or "*" if the string
 * was NULL length or a NULL pointer.
 */
static char* check_string(char* s)
{
  static char star[2] = "*";
  char* str = s;

  if (BadPtr(s))
    return star;

  for ( ; *s; ++s) {
    if (IsSpace(*s))
      {
        *s = '\0';
        break;
      }
  }
  return str;
}

/*
 * create a string of form "foo!bar@fubar" given foo, bar and fubar
 * as the parameters.  If NULL, they become "*".
 */
char* make_nick_user_host(const char* nick, 
			  const char* name, const char* host)
{
  static char namebuf[NICKLEN + USERLEN + HOSTLEN + 6];
  int   n;
  char* s;
  const char* p;

  s = namebuf;

  for (p = nick, n = NICKLEN; *p && n--; )
    *s++ = *p++;
  /* This hackery prevents the problem when a ban of *nick or whatever
   * is placed, and the last character is clipped, making a non-matching
   * mask
   *
   * This changes the last character into a * if it is truncated
   */
  if (*p)
    s[-1] = '*';
  *s++ = '!';
  for(p = name, n = USERLEN; *p && n--; )
    *s++ = *p++;
  if (*p)
    s[-1] = '*';
  *s++ = '@';
  for(p = host, n = HOSTLEN; *p && n--; )
    *s++ = *p++;
  if (*p)
    s[-1] = '*';
  *s = '\0';
  return namebuf;
}

/*
 * Ban functions to work with mode +b/e/d/I/q
 */
/* add the specified ID to the channel..
     -is 8/9/00 */
/* backported from ircd-hybrid-7 for +ins 5/11/00 -is */
/* ported to dancer-hybrid 29/1/01 - is */

static int add_id(struct Client *sptr, struct Channel *chptr,
		  const char *banid, int type, const char *fwchan)
{
  struct SLink  *ban;
  struct SLink  **list;

  switch(type)
    {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_QUIET:
      list = &chptr->quietlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_DENY:
      list = &chptr->denylist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_ops_flag(UMODE_DEBUG, get_str(STR_ADD_BAD_BAN_TYPE), type);
      return -1;
    }

  if (MyClient(sptr))
    {
      /* Don't let local clients overflow the banlist or set redundant bans */
      if (chptr->num_bed >= ((chptr->mode.mode & MODE_LARGEBANLIST) ? MAXBANS_PERM : MAXBANS))
	{
	  sendto_one(sptr, form_str(ERR_BANLISTFULL),
		     me.name, sptr->name,
		     chptr->chname, banid);
	  return -1;
	}
      for (ban = *list; ban; ban = ban->next)
	if (match(BANSTR(ban), banid))
	    return -1;
    }
  else
    {
      /* Don't let remote clients or servers set duplicate bans;
       * redundant but not identical stuff must be allowed to avoid desyncs.
       */
      for (ban = *list; ban; ban = ban->next)
        if (irccmp(BANSTR(ban), banid) == 0)
#ifdef BAN_CHANNEL_FORWARDING
	  {
            if (!BANFWD(ban) && !fwchan)
                return -1; /* No forward. Bans are identical, drop it. */
	    if (IsServer(sptr)) /* Negotiate with servers so we don't get forward desyncs. */
	      {
		if (BANFWD(ban) && fwchan && irccmp(BANFWD(ban), fwchan) == 0)
                    return -1; /* Identical. */
                else if (!BANFWD(ban) || (fwchan && irccmp(BANFWD(ban), fwchan) < 0))
                  {
                    /* Forward overrides no-forward, or remote wins fight. */
                    if (BANFWD(ban))
                        strncpy_irc(forward_clear, BANFWD(ban), 32);
                    else
                        *forward_clear = '\0';

                    if (!BANFWD(ban))
                      {
                        expect_malloc;
                        BANFWD(ban) = (char *)MyMalloc(32);
                        malloc_log("add_id() allocating 32 bytes at %p", (void *)BANFWD(ban));
                      }
                    strncpy_irc(BANFWD(ban), fwchan, 32);
                    /* Report back to set_channel_mode that the old mode
                     * needs to be deleted before the new one can be set.
                     * --gxti
                     */
                    return 1;
                  }
                else
                    return -1; /* Local wins. */
              }
            else
                return -1;
	  }
#else
            return -1;
#endif
    }

  ban = make_link();
  memset(ban, 0, sizeof(struct SLink));
  ban->flags = type;
  ban->next = *list;

  expect_malloc;
  ban->value.banptr = (aBan *)MyMalloc(sizeof(struct Ban));
  malloc_log("add_id() allocating struct Ban (%zd bytes) at %p", sizeof(struct Ban), (void *)ban->value.banptr);
  DupString(ban->value.banptr->banstr, banid);

  if (IsPerson(sptr))
    {
      int size = strlen(sptr->name) + strlen(sptr->username) + strlen(sptr->host) + 3;
      expect_malloc;
      ban->value.banptr->who = MyMalloc(size);
      ircsnprintf(ban->value.banptr->who, size, "%s!%s@%s",
		  sptr->name, sptr->username, sptr->host);
      malloc_log("add_id() allocating ban origin %s (%zd bytes) at %p",
                 ban->value.banptr->who, size, ban->value.banptr->who);
    }
  else
    {
      DupString(ban->value.banptr->who, sptr->name);
    }

  ban->value.banptr->when = CurrentTime;

#ifdef BAN_CHANNEL_FORWARDING
  ban->value.banptr->ban_forward_chname = NULL;
  if (fwchan)
    {
      expect_malloc;
      ban->value.banptr->ban_forward_chname = (char *)MyMalloc(32);
      malloc_log("add_id() allocating 32 bytes at %p", (void *)ban->value.banptr->ban_forward_chname);
      strncpy_irc(ban->value.banptr->ban_forward_chname, fwchan, 32);
    }
#endif

  *list = ban;
  chptr->num_bed++;
  return 0;
}

/*
 *
 * "del_id - delete an id belonging to cptr
 * if banid is null, deleteall banids belonging to cptr."
 *
 * from orabidoo
 * modified 8/9/00 by is: now we handle all ban types here
 * (invex/excemp/deny/etc)
 */
/* backported from ircd-hybrid-7 to +ins 5/11/00 -is */
/* ported to dancer-hybrid 29/01/01 -is */

static  int     del_id(struct Channel *chptr, const char *banid, int type)
{
  register struct SLink *ban;
  register struct SLink **list;

#ifdef BAN_CHANNEL_FORWARDING
  *forward_clear = '\0';
#endif

  if (!banid)
    return -1;

  switch(type)
    {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_QUIET:
      list = &chptr->quietlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_DENY:
      list = &chptr->denylist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_ops_flag(UMODE_DEBUG, get_str(STR_DEL_BAD_BAN_TYPE), type);
      return -1;
    }

  for (; (ban = *list); list = &ban->next)
    {
      if (irccmp(banid, ban->value.banptr->banstr) == 0)
        {
          *list = ban->next;
          MyFree(ban->value.banptr->banstr);
          MyFree(ban->value.banptr->who);
#ifdef BAN_CHANNEL_FORWARDING
          if (ban->value.banptr->ban_forward_chname)
	    {
              strncpy_irc(forward_clear, ban->value.banptr->ban_forward_chname, 32);
              MyFree(ban->value.banptr->ban_forward_chname);
	    }
#endif
          MyFree(ban->value.banptr);
          free_link(ban);
          /* num_bed should never be < 0 */
          if(chptr->num_bed > 0)
            chptr->num_bed--;
          else
            chptr->num_bed = 0;
          return 0;
        }
    }
  return 1;
}

/*
 * is_banned -  returns an int 0 if not banned,
 *              CHFL_BAN if banned (or +d'd) (only if cannot join -- asuffield)
 *              CHFL_EXCEPTION if they have a ban exception
 *              CHFL_QUIET if cannot send to channel  -- asuffield
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */

static  int is_banned(struct Client *cptr,struct Channel *chptr)
{
  register Link *actualBan = NULL;
  register Link *t2 = NULL;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;
  int isQuiet = 0;

  if (!IsPerson(cptr))
    return (0);

  strncpy_irc(s, make_nick_user_host(cptr->name, cptr->username, cptr->host),
	      NICKLEN + USERLEN + HOSTLEN + 6);
  s2 = make_nick_user_host(cptr->name, cptr->username,
                           cptr->sockhost);

  /* We must scan to the first ban, not the first quiet */
  for (t2 = chptr->banlist; t2; t2 = t2->next)
    if (match(BANSTR(t2), s) ||
        match(BANSTR(t2), s2))
      {
	actualBan = t2;
	break;
      }

  /* check +d list */
  for (t2 = chptr->denylist; t2; t2 = t2->next)
    {
      if (match(BANSTR(t2), cptr->info))
        {
          actualBan = t2;
	  break;
	}
    }

  /* check +q list */
  if(!actualBan)
    {
      for (t2 = chptr->quietlist; t2; t2 = t2->next)
        if (match(BANSTR(t2), s) ||
	    match(BANSTR(t2), s2))
	  {
            actualBan = t2;
	    isQuiet = 1;
	    break;
	  }
    }

  if (actualBan)
    {
      for (t2 = chptr->exceptlist; t2; t2 = t2->next)
        if (match(BANSTR(t2), s) ||
            match(BANSTR(t2), s2))
          {
            return CHFL_EXCEPTION;
          }
    }

  /* return CHFL_BAN for +b or +d match, we really dont need to be more
     specific */
  /* return CHFL_QUIET as well, since you can't send to channel when 
     banned -- asuffield */
  if (actualBan)
    {
      if(isQuiet)
	return CHFL_QUIET;
      else
	return CHFL_QUIET | CHFL_BAN;
    }
  else
    return 0;
}

static int is_invex(struct Client *cptr,struct Channel *chptr)
{
  register Link *tmp;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return (0);

  strncpy_irc(s, make_nick_user_host(cptr->name, cptr->username, cptr->host),
	      NICKLEN + USERLEN + HOSTLEN + 6);
  s2 = make_nick_user_host(cptr->name, cptr->username,
                           inetntoa((char*) &cptr->ip));

  for (tmp = chptr->invexlist; tmp; tmp = tmp->next)
    if (match(BANSTR(tmp), s) ||
        match(BANSTR(tmp), s2))
      return(1);
      
  return(0);
}

/*
 * adds a user to a channel by adding another link to the channels member
 * chain.
 */
static  void    add_user_to_channel(struct Channel *chptr, struct Client *who, int flags)
{
  Link *ptr;

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  if( chptr->mode.mode & MODE_SPLIT )
    {
      /* Unmark the split mode */
      chptr->mode.mode &= ~MODE_SPLIT;

      /* remove from the empty channel double link list */
      if (chptr->last_empty_channel)
        chptr->last_empty_channel->next_empty_channel =
          chptr->next_empty_channel;
      else
        empty_channel_list = chptr->next_empty_channel;
      if (chptr->next_empty_channel)
        chptr->next_empty_channel->last_empty_channel =
          chptr->last_empty_channel;
    }
#endif

  if (who->user)
    {
      ptr = make_link();
      ptr->flags = flags;
      ptr->value.cptr = who;
      ptr->next = chptr->members;
      chptr->members = ptr;

      chptr->users++;

      ptr = make_link();
      ptr->value.chptr = chptr;
      ptr->next = who->user->channel;
      who->user->channel = ptr;
      who->user->joined++;
    }
}

void    remove_user_from_channel(struct Client *sptr,struct Channel *chptr,int was_kicked)
{
  Link  **curr;
  Link  *tmp;

  for (curr = &chptr->members; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.cptr == sptr)
      {
        /* User was kicked, but had an exception.
         * so, to reduce chatter I'll remove any
         * matching exception now.
         */
        /* Firstly, this is insane
         *
         * Secondly, the code is broken
         */
        *curr = tmp->next;
        free_link(tmp);
        break;
      }
  for (curr = &sptr->user->channel; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
        *curr = tmp->next;
        free_link(tmp);
        break;
      }
  sptr->user->joined--;

  sub1_from_channel(chptr);

}

static  void    change_chan_flag(struct Channel *chptr,struct Client *cptr, int flag)
{
  Link *tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
   {
    if (flag & MODE_ADD)
      {
        tmp->flags |= flag & MODE_FLAGS;
        if (flag & MODE_CHANOP)
          tmp->flags &= ~MODE_DEOPPED;
      }
    else
      {
        tmp->flags &= ~flag & MODE_FLAGS;
      }
   }
}

static  void    set_deopped(struct Client *cptr, struct Channel *chptr,int flag)
{
  Link  *tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
    if ((tmp->flags & flag) == 0)
      tmp->flags |= MODE_DEOPPED;
}

int     is_chan_op(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_CHANOP);
  
  return 0;
}

int     can_send(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  /* Servers may send everywhere */
  if (IsServer(cptr))
    return 0;

  /* If the user is +p, allow */
  if (HasUmode(cptr,UMODE_GOD))
    return 0;

#ifdef JUPE_CHANNEL
  /* If the channel is juped, deny */
  if (chptr->mode.mode & MODE_JUPED)
    {
      return MODE_JUPED;
    }
#endif

  lp = find_user_link(chptr->members, cptr);

  /* If the user is not on the channel */
  if (!lp)
    /* Block if +n, allow if -n */
    return (chptr->mode.mode & MODE_NOPRIVMSGS) ? MODE_NOPRIVMSGS : 0;

  /* If the user is +o or +v on the channel */
  if (lp->flags & (CHFL_CHANOP | CHFL_VOICE))
    /* Always allow */
    return 0;

  if ((chptr->mode.mode & MODE_QUIETUNIDENT) && !HasUmode(cptr, UMODE_IDENTIFIED) && !cptr->user->servlogin[0])
    return MODE_QUIETUNIDENT;

  /* If we get this far, the user is neither opped nor voiced */

  if (chptr->mode.mode & MODE_MODERATED)
    return MODE_MODERATED;

  /* If the user is banned/quieted on the channel */
  if (is_banned(cptr, chptr) & CHFL_QUIET)
    /* Block it */
    return MODE_MODERATED;

  return 0;
}

int     user_channel_mode(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags);
  
  return 0;
}

/*
 * write the "simple" list of channel modes for channel chptr onto buffer mbuf
 * with the parameters in pbuf.
 */
void channel_modes(struct Client *cptr, char *mbuf, char *pbuf, struct Channel *chptr)
{
  *mbuf++ = '+';
  if (chptr->mode.mode & MODE_SECRET)
    *mbuf++ = 's';
  if (chptr->mode.mode & MODE_MODERATED)
    *mbuf++ = 'm';
  if (chptr->mode.mode & MODE_TOPICLIMIT)
    *mbuf++ = 't';
  if (chptr->mode.mode & MODE_INVITEONLY)
    *mbuf++ = 'i';
  if (chptr->mode.mode & MODE_NOPRIVMSGS)
    *mbuf++ = 'n';
  if (chptr->mode.mode & MODE_LOGGING)
    *mbuf++ = 'L';
  if (chptr->mode.mode & MODE_NOCOLOR)
    *mbuf++ = 'c';
  if (chptr->mode.mode & MODE_FREEINVITE)
    *mbuf++ = 'g';
  if (chptr->mode.mode & MODE_PERM)
    *mbuf++ = 'P';
  if (chptr->mode.mode & MODE_NOFORWARD)
    *mbuf++ = 'Q';
  if (chptr->mode.mode & MODE_NOUNIDENT)
    *mbuf++ = 'r';
  if (chptr->mode.mode & MODE_QUIETUNIDENT)
    *mbuf++ = 'R';
  if (chptr->mode.mode & MODE_LARGEBANLIST)
    *mbuf++ = 'L';
#ifdef JUPE_CHANNEL
  if (chptr->mode.mode & MODE_JUPED)
    *mbuf++ = 'j';
#endif
  if (chptr->mode.mode & MODE_OPMODERATE)
    *mbuf++ = 'z';

  if (chptr->mode.limit)
    {
      *mbuf++ = 'l';
      if (IsMember(cptr, chptr) || IsServer(cptr) || HasUmode(cptr, UMODE_USER_AUSPEX))
        {
          ircsnprintf(pbuf, 16, "%d", chptr->mode.limit);
	  strcat(pbuf, " ");
        }
    }
#ifdef INVITE_CHANNEL_FORWARDING
  if (chptr->mode.mode & MODE_FORWARD) /* -goober */
    {
      *mbuf++ = 'f';
      if (IsMember(cptr, chptr) || IsServer(cptr) || HasUmode(cptr, UMODE_USER_AUSPEX))
	{
	  strcat(pbuf, chptr->mode.invite_forward_channel_name);
	  strcat(pbuf, " ");
	}
    }
#endif
  if (chptr->mode.join_throttle_frequency)
    {
      *mbuf++ = 'J';
      if (IsMember(cptr, chptr) || IsServer(cptr) || HasUmode(cptr, UMODE_USER_AUSPEX))
        {
          ircsnprintf(pbuf + strlen(pbuf), MODEBUFLEN - strlen(pbuf) - 1, "%d,%d",
                      chptr->mode.join_throttle_frequency, chptr->mode.join_throttle_limit);
	  strcat(pbuf, " ");
        }
    }
  if (chptr->mode.autodline_frequency)
    {
      *mbuf++ = 'D';
      if (IsMember(cptr, chptr) || IsServer(cptr) || HasUmode(cptr, UMODE_USER_AUSPEX))
        {
          ircsnprintf(pbuf + strlen(pbuf), MODEBUFLEN - strlen(pbuf) - 1, "%d,%d ",
                      chptr->mode.autodline_frequency, chptr->mode.autodline_limit);
	  strcat(pbuf, " ");
        }
    }
  /* This one needs to be at the end of the string, since no space is appended */
  if (*chptr->mode.key)
    {
      *mbuf++ = 'k';
      if (IsMember(cptr, chptr) || IsServer(cptr) || HasUmode(cptr, UMODE_USER_AUSPEX))
        (void)strcat(pbuf, chptr->mode.key);
    }
  *mbuf++ = '\0';
  return;
}

/*
 * only used to send +b and +e now, also +dq
 * 
 */

static  void    send_mode_list(struct Client *cptr,
                               char *chname,
                               Link *top,
                               int mask,
                               char flag)
{
  Link  *lp;
  char  *cp, *name;
  int   count = 0, send = 0;
#ifdef BAN_CHANNEL_FORWARDING
  char *bf_name;
#endif
  int bf_size=0;
  
  cp = modebuf + strlen(modebuf);
  if (*parabuf) /* mode +l or +k xx */
    count = 1;
  for (lp = top; lp; lp = lp->next)
    {
      if (!(lp->flags & mask))
        continue;
      name = BANSTR(lp);
#ifdef BAN_CHANNEL_FORWARDING     
      if ((mask & CHFL_BAN) && (lp->value.banptr->ban_forward_chname))
	{
	  bf_name = lp->value.banptr->ban_forward_chname;
	  bf_size=1+strlen(bf_name);  /* +1 => includes '!' */
	}
      else bf_name=NULL;
#endif

      if (strlen(parabuf) + strlen(name) + bf_size + 10 < (size_t) MODEBUFLEN)
        {
          (void)strcat(parabuf, " ");
          (void)strcat(parabuf, name);
#ifdef BAN_CHANNEL_FORWARDING     
	  if (bf_name) 
	    {
	      (void)strcat(parabuf,"!");
	      (void)strcat(parabuf, bf_name);
	    }
#endif
          count++;
          *cp++ = flag;
          *cp = '\0';
        }
      else if (*parabuf)
        send = 1;
      if (count == 3)
        send = 1;
      if (send)
        {
          sendto_one(cptr, ":%s MODE %s %s %s",
                     me.name, chname, modebuf, parabuf);
          send = 0;
          *parabuf = '\0';
          cp = modebuf;
          *cp++ = '+';
          if (count != 3)
            {
              (void)strcpy(parabuf, name);
#ifdef BAN_CHANNEL_FORWARDING     
	      if (bf_name) 
		{
		  (void)strcat(parabuf,"!");
		  (void)strcat(parabuf, bf_name);
		}
#endif
              *cp++ = flag;
            }
          count = 0;
          *cp = '\0';
        }
    }
}

/*
 * send "cptr" a full list of the modes for channel chptr.
 */
void send_channel_modes(struct Client *cptr, struct Channel *chptr)
{
  Link  *l, *anop = NULL, *skip = NULL;
  char  *t;

  if (*chptr->chname != '#')
    return;

  *modebuf = *parabuf = '\0';
  channel_modes(cptr, modebuf, parabuf, chptr);

  if (*parabuf)
    strcat(parabuf, " ");
  ircsnprintf(buf, BUFSIZE, ":%s SJOIN %lu %s %s %s:", me.name,
          chptr->channelts, chptr->chname, modebuf, parabuf);
  t = buf + strlen(buf);
  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
        anop = l;
        break;
      }
  /* follow the channel, but doing anop first if it's defined
  **  -orabidoo
  */
  l = NULL;
  for (;;)
    {
      if (anop)
        {
          l = skip = anop;
          anop = NULL;
        }
      else 
        {
          if (l == NULL || l == skip)
            l = chptr->members;
          else
            l = l->next;
          if (l && l == skip)
            l = l->next;
          if (l == NULL)
            break;
        }
      if (l->flags & MODE_CHANOP)
        *t++ = '@';
      if (l->flags & MODE_VOICE)
        *t++ = '+';
      strncpy_irc(t, l->value.cptr->name, HOSTLEN + 1);
      t += strlen(t);
      *t++ = ' ';
      /* XXX - GROTESQUE HACK. This means HOSTLEN cannot grow without changing
       *  this expression. How many other places has this happened in?
       *  -- asuffield
       */
      if (t - buf > BUFSIZE - 80)
        {
          *t++ = '\0';
          if (t[-1] == ' ') t[-1] = '\0';
          sendto_one(cptr, "%s", buf);
          ircsnprintf(buf, BUFSIZE, ":%s SJOIN %lu %s 0 :",
		      me.name, chptr->channelts,
		      chptr->chname);
          t = buf + strlen(buf);
        }
    }
      
  *t++ = '\0';
  if (t[-1] == ' ') t[-1] = '\0';
  sendto_one(cptr, "%s", buf);

  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->banlist, CHFL_BAN,'b');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
               me.name, chptr->chname, modebuf, parabuf);

  if(!IsCapable(cptr,CAP_EX))
    return;

  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->exceptlist, CHFL_EXCEPTION,'e');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
               me.name, chptr->chname, modebuf, parabuf);

  if(!IsCapable(cptr,CAP_DE))
      return;
  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->denylist, CHFL_DENY,'d');
  
  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
               me.name, chptr->chname, modebuf, parabuf);

  if (IsCapable(cptr, CAP_IE))
    {
      *parabuf = '\0';
      *modebuf = '+';
      modebuf[1] = '\0';
      send_mode_list(cptr, chptr->chname, chptr->invexlist, CHFL_INVEX,'I');

      if (modebuf[1] || *parabuf)
	sendto_one(cptr, ":%s MODE %s %s %s",
		   me.name, chptr->chname, modebuf, parabuf);
    }
  if (IsCapable(cptr, CAP_QU))
    {
      *parabuf = '\0';
      *modebuf = '+';
      modebuf[1] = '\0';
      send_mode_list(cptr, chptr->chname, chptr->quietlist, CHFL_QUIET,'q');

      if (modebuf[1] || *parabuf)
	sendto_one(cptr, ":%s MODE %s %s %s",
		   me.name, chptr->chname, modebuf, parabuf);
    }
}

/* stolen from Undernet's ircd  -orabidoo
 *
 */

char* pretty_mask(char* mask)
{
  register char* cp = mask;
  register char* user;
  register char* host;

  if ((user = strchr(cp, '!')))
    *user++ = '\0';
  if ((host = strrchr(user ? user : cp, '@')))
    {
      *host++ = '\0';
      if (!user)
        return make_nick_user_host("*", check_string(cp), check_string(host));
    }
  else if (!user && strchr(cp, '.'))
    return make_nick_user_host("*", "*", check_string(cp));
  return make_nick_user_host(check_string(cp), check_string(user), 
                             check_string(host));
}

static void
autodline_clear_channel(struct Channel *chptr)
{
  Link* lp;
  static const char base_reason[] = "Auto-dline channel: ";
  size_t reason_len = strlen(base_reason) + strlen(chptr->chname) + 1;
  char reason[64 + CHANNELLEN + 1];
  ircsnprintf(reason, reason_len, "%s%s", base_reason, chptr->chname);
  
  for (lp = chptr->members; lp; lp = lp->next)
    {
      struct Client *acptr = lp->value.cptr;
      if (!HasUmode(acptr, UMODE_IMMUNE) &&
		      !HasUmode(acptr, UMODE_AUTODLINE) && MyClient(acptr))
        dline_client(acptr, reason);
    }
}

static  char    *fix_key(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
    {
      c &= 0x7f;
      if (c != ':' && c != ',' && c > ' ')
      {
        *t++ = c;
      }
    }
  *t = '\0';
  return arg;
}

/*
 * Here we attempt to be compatible with older non-hybrid servers.
 * We can't back down from the ':' issue however.  --Rodder
 */
static  char    *fix_key_old(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
    { 
      c &= 0x7f;
      if ((c != 0x0a) && (c != ',') && (c != ':'))
        *t++ = c;
    }
  *t = '\0';
  return arg;
}

/*
 * like the name says...  take out the redundant signs in a modechange list
 */
static  void    collapse_signs(char *s)
{
  char  plus = '\0', *t = s, c;
  while ((c = *s++))
    {
      if (c != plus)
        *t++ = c;
      if (c == '+' || c == '-')
        plus = c;
    }
  *t = '\0';
}

/* little helper function to avoid returning duplicate errors */
static  int     errsent(int err, int *errs)
{
  if (err & *errs)
    return 1;
  *errs |= err;
  return 0;
}

/* bitmasks for various error returns that set_mode should only return
 * once per call  -orabidoo
 */

#define SM_ERR_NOTS             0x00000001      /* No TS on channel */
#define SM_ERR_RPL_D            0x00000008
#define SM_ERR_RPL_B            0x00000010
#define SM_ERR_RPL_E            0x00000020
#define SM_ERR_RPL_I            0x00000040
#define SM_ERR_NOTONCHANNEL     0x00000080      /* Not on channel */
#define SM_ERR_RESTRICTED       0x00000100      /* Restricted chanop */
#define SM_ERR_NOOPS		0x00000200	/* Chanops needed - AGB */
#define SM_ERR_UNKNOWN		0x00000400	/* Unknown mode - AGB */
#define SM_ERR_RPL_F            0x00000800

/*
** Apply the mode changes passed in parv to chptr, sending any error
** messages and MODE commands out.  Rewritten to do the whole thing in
** one pass, in a desperate attempt to keep the code sane.  -orabidoo
*/
/*
 * rewritten to remove +h/+c/z 
 * in spirit with the one pass idea, I've re-written how "imnspt"
 * handling was done
 *
 * I've also left some "remnants" of the +h code in for possible
 * later addition.
 * For example, isok could be replaced witout half ops, with ischop() or
 * chan_op depending.
 *
 * -Dianora
 */

void set_channel_mode(struct Client *cptr,
                      struct Client *sptr,
                      struct Channel *chptr,
                      int parc,
                      char *parv[])
{
  int   errors_sent = 0, opcnt = 0, len = 0, tmp, nusers;
  int   keychange = 0, limitset = 0;
  int   whatt = MODE_ADD, the_mode = 0;
  int   done_s = NO;
  int   done_i = NO, done_m = NO, done_n = NO, done_t = NO;
#ifdef INVITE_CHANNEL_FORWARDING
  int done_f = NO;
#endif
  struct Client *who;
  Link  *lp;
  char  *curr = parv[0], c, plus = '+', *tmpc;
  char *arg = NULL;
  char  numeric[16];
  /* mbufw gets the param-less mode chars, always with their sign
   * mbuf2w gets the paramed mode chars, always with their sign
   * pbufw gets the params, in ID form whenever possible
   * pbuf2w gets the params, no ID's
   */
  /* no ID code at the moment
   * pbufw gets the params, no ID's
   * grrrr for now I'll stick the params into pbufw without ID's
   * -Dianora
   */
  char  *mbufw = modebuf, *mbuf2w = modebuf2;
  char  *pbufw = parabuf, *pbuf2w = parabuf2;
  char  *maskbufw = maskbuf;

  /* quiets need to be sent to servers as +q for compatibility
   * reasons, but to clients as +b %foo so they maintain a
   * sane banlist on the client. these hold differing outputs
   * for bans, one for server, one for client, appended to the
   * normal mode string
   * --gxti
   */
  char  *clmbuf = clmodebuf, *clpbuf = clparabuf;
  char  *svmbuf = svmodebuf, *svpbuf = svparabuf;

  int   warned_no_op = 0;
  int   ischop;
  int   isok;
  int   isdeop;
  int   chan_op;
  int   user_mode;
#ifdef BAN_CHANNEL_FORWARDING
  char *fchname;
#endif

  *clmbuf = *clpbuf = *svmbuf = *svpbuf = '\0';

  user_mode = user_channel_mode(sptr, chptr);
  chan_op = (user_mode & CHFL_CHANOP) || HasUmode(sptr,UMODE_GOD);

  /* has ops or is a server */
  ischop = IsServer(sptr) || chan_op;

  /* is client marked as deopped */
  isdeop = !ischop && !IsServer(sptr) && (user_mode & CHFL_DEOPPED);

  /* is an op or server or remote user on a TS channel */
  isok = ischop || (!isdeop && IsServer(cptr) && chptr->channelts);

  /* isok_c calculated later, only if needed */

  /* parc is the number of _remaining_ args (where <0 means 0);
  ** parv points to the first remaining argument
  */
  parc--;
  parv++;

  for ( ; ; )
    {
      if (BadPtr(curr))
        {
          /*
           * Deal with mode strings like "+m +o blah +i"
           */
          if (parc-- > 0)
            {
              curr = *parv++;
              continue;
            }
          break;
        }
      c = *curr++;

      switch (c)
        {
        case '+' :
          whatt = MODE_ADD;
          plus = '+';
          continue;
          /* NOT REACHED */
          break;

        case '-' :
          whatt = MODE_DEL;
          plus = '-';
          continue;
          /* NOT REACHED */
          break;

        case '=' :
          whatt = MODE_QUERY;
          plus = '=';   
          continue;
          /* NOT REACHED */
          break;

        case 'o' :
        case 'v' :
          if (MyClient(sptr))
            {
              if(!IsMember(sptr, chptr) && !HasUmode(sptr,UMODE_GOD))
                {
                  if(!errsent(SM_ERR_NOTONCHANNEL, &errors_sent))
                    sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                               me.name, sptr->name, chptr->chname);
                  /* eat the parameter */
                  parc--;
                  parv++;
                  break;
                }
#ifdef LITTLE_I_LINES
              else
                {
                  if(IsRestricted(sptr) && (whatt == MODE_ADD))
                    {
                      if(!errsent(SM_ERR_RESTRICTED, &errors_sent))
                        {
                          sendto_one(sptr, form_str(ERR_RESTRICTED),
				     me.name,
				     sptr->name);
                        }
                      /* eat the parameter */
                      parc--;
                      parv++;
                      break;
                    }
                }
#endif
            }
          if (whatt == MODE_QUERY)
            break;
          if (parc-- <= 0)
            break;
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!(who = find_chasing(sptr, arg, NULL)))
            break;

          /* there is always the remote possibility of picking up
           * a bogus user, be nasty to core for that. -Dianora
           */

          if (!who->user)
            break;

          /* no more of that mode bouncing crap */
          if (!IsMember(who, chptr))
            {
              if (MyClient(sptr))
                sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL), me.name, 
                           sptr->name, arg, chptr->chname);
              break;
            }

          if ((who == sptr) && (c == 'o'))
            {
              if(whatt == MODE_ADD && !HasUmode(sptr,UMODE_GOD))
                break;
              }

          if (c == 'o')
            the_mode = MODE_CHANOP;
          else if (c == 'v')
            the_mode = MODE_VOICE;

          if (isdeop && (c == 'o') && whatt == MODE_ADD)
            set_deopped(who, chptr, the_mode);

#ifdef HIDE_OPS
	  if(the_mode == MODE_CHANOP && whatt == MODE_DEL)
	    sendto_one(who,":%s MODE %s -o %s",
		       sptr->name,chptr->chname,who->name);
#endif

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          /* Immune users */
          if ((c == 'o') && whatt == MODE_DEL && !(IsServer(sptr) || HasUmode(sptr,UMODE_GOD))
              && HasUmode(who,UMODE_IMMUNE) && sptr != who)
            {
              sendto_one(sptr, form_str(ERR_USERISIMMUNE),
                         me.name, sptr->name,
                         who->name, chptr->chname);
              break;
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          *mbufw++ = plus;
          *mbufw++ = c;
          strncpy_irc(pbufw, who->name, HOSTLEN + 1);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          opcnt++;

          change_chan_flag(chptr, who, the_mode|whatt);

          break;

        case 'k':
          if (whatt == MODE_QUERY)
            break;
          if (parc-- <= 0)
            {
	      static char star[] = "*";
              /* allow arg-less mode -k */
              if (whatt == MODE_DEL)
                arg = star;
              else
                break;
            }
          else
            {
              if (whatt == MODE_DEL)
                {
                  arg = check_string(*parv++);
                }
              else
                {
                  if MyClient(sptr)
                    arg = fix_key(check_string(*parv++));
                  else
                    arg = fix_key_old(check_string(*parv++));
                }
            }

          if (keychange++)
            break;
          /*      if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;
            */
          if (!*arg)
            break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if ( (tmp = strlen(arg)) > KEYLEN)
            {
              arg[KEYLEN] = '\0';
              tmp = KEYLEN;
            }

          if (len + tmp + 2 >= MODEBUFLEN)
            break;

#ifndef OLD_MODE_K
          /* if there is already a key, and the client is adding one
           * remove the old one, then add the new one
           */

          if((whatt == MODE_ADD) && *chptr->mode.key)
            {
              /* If the key is the same, don't do anything */

              if(!strcmp(chptr->mode.key,arg))
                break;

              sendto_channel_butserv(chptr, sptr, ":%s MODE %s -k %s", 
                                     IsServer(sptr) ? NETWORK_NAME : sptr->name, chptr->chname,
                                     chptr->mode.key);

              sendto_match_servs(chptr, cptr, ":%s MODE %s -k %s",
                                 sptr->name, chptr->chname,
                                 chptr->mode.key);
            }
#endif
          if (whatt == MODE_DEL)
            {
              if( (arg[0] == '*') && (arg[1] == '\0'))
                arg = chptr->mode.key;
              else
                {
                  if(strcmp(arg,chptr->mode.key))
                    break;
		}
	    }

	  /* Ignore -k if the channel is already -k */
	  if ((whatt == MODE_DEL) && !*chptr->mode.key)
	    break;

          *mbufw++ = plus;
          *mbufw++ = 'k';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          /*      opcnt++; */

          if (whatt == MODE_DEL)
            {
              *chptr->mode.key = '\0';
            }
          else
            {
              strncpy_irc(chptr->mode.key, arg, KEYLEN + 1);
            }

          break;

        case 'I':
          /* Yes, this really does apply to MODE_QUERY */
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_I, &errors_sent))
                break;
              for (lp = chptr->invexlist; lp; lp = lp->next)
                sendto_one(cptr, form_str(RPL_INVITELIST),
                           me.name, cptr->name,
                           chptr->chname,
                           lp->value.banptr->banstr,
                           lp->value.banptr->who,
                           lp->value.banptr->when);

              sendto_one(sptr, form_str(RPL_ENDOFINVITELIST),
                         me.name, sptr->name,
                         chptr->chname);
              break;
            }

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          arg = check_string(*parv++);

          /* user-friendly ban mask generation, taken
           * from Undernet's ircd  -orabidoo
           */
	  arg = strip_colour(collapse(pretty_mask(arg)));

          if(*arg == ':')
            break;

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if ((whatt & MODE_ADD) && add_id(sptr, chptr, arg, CHFL_INVEX, NULL) < 0)
	    break;
	  if (whatt & MODE_DEL)
	    del_id(chptr, arg, CHFL_INVEX);

          *mbuf2w++ = plus;
          *mbuf2w++ = 'I';
          strcpy(pbuf2w, arg);
          pbuf2w += strlen(pbuf2w);
          *pbuf2w++ = ' ';
          len += tmp + 1;
          opcnt++;

	  break;

          /* There is a nasty here... I'm supposed to have
           * CAP_EX before I can send exceptions to bans to a server.
           * But that would mean I'd have to keep two strings
           * one for local clients, and one for remote servers,
           * one with the 'e' strings, one without.
           * I added another parameter buf and mode buf for "new"
           * capabilities.
           *
           * -Dianora
           */

        case 'e':
          /* don't allow a non chanop to see the exception list
           * suggested by Matt on operlist nov 25 1998
           */
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }
          
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_E, &errors_sent))
                break;
              for (lp = chptr->exceptlist; lp; lp = lp->next)
                sendto_one(cptr, form_str(RPL_EXCEPTLIST),
                           me.name, cptr->name,
                           chptr->chname,
                           lp->value.banptr->banstr,
                           lp->value.banptr->who,
                           lp->value.banptr->when);
              sendto_one(sptr, form_str(RPL_ENDOFEXCEPTLIST),
                         me.name, sptr->name, 
                         chptr->chname);
              break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          /* user-friendly ban mask generation, taken
           * from Undernet's ircd  -orabidoo
	   */
	  arg = strip_colour(collapse(pretty_mask(arg)));

          if(*arg == ':')
            break;

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if ((whatt & MODE_ADD) && add_id(sptr, chptr, arg, CHFL_EXCEPTION, NULL) < 0)
	    break;
          if (whatt & MODE_DEL)
	    del_id(chptr, arg, CHFL_EXCEPTION);

          /* This stuff can go back in when all servers understand +e 
           * with the pbufw_new nonsense removed -Dianora
           */

          *mbuf2w++ = plus;
          *mbuf2w++ = 'e';
          strcpy(pbuf2w, arg);
          pbuf2w += strlen(pbuf2w);
          *pbuf2w++ = ' ';
          len += tmp + 1;
          opcnt++;

          break;


          /* There is a nasty here... I'm supposed to have
           * CAP_DE before I can send exceptions to bans to a server.
           * But that would mean I'd have to keep two strings
           * one for local clients, and one for remote servers,
           * one with the 'e' strings, one without.
           * I added another parameter buf and mode buf for "new"
           * capabilities.
           *
           * -Dianora
           */

        case 'd':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_D, &errors_sent))
                break;
              for (lp = chptr->denylist; lp; lp = lp->next)
                sendto_one(cptr, form_str(RPL_BANLIST),
                           me.name, cptr->name,
                           chptr->chname,
                           lp->value.banptr->banstr,
                           lp->value.banptr->who,
                           lp->value.banptr->when);
              sendto_one(sptr, form_str(RPL_ENDOFBANLIST),
                         me.name, sptr->name, 
                         chptr->chname);
              break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }
          
	  arg = strip_colour(arg);

	  /* Denies are fully free-form, could have an empty string here */
          if(*arg == ':' || *arg == '\0')
            break;

	  /* Local clients cannot add stuff with ** -- jilles */
	  if (MyClient(sptr) && (whatt & MODE_ADD))
	    collapse(arg);

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if ((whatt & MODE_ADD) && add_id(sptr, chptr, arg, CHFL_DENY, NULL) < 0)
	    break;
          if (whatt & MODE_DEL)
	  {
	    if (del_id(chptr, arg, CHFL_DENY) == 1 && MyClient(sptr))
	    {
	      /* If it wasn't found, try removing the collapsed version
	       * This makes sure +d **blah then -d **blah works.
	       * If the collapsed version was present, but only then,
	       * propagate the collapsed version, otherwise the original.
	       * (The same way as -bqeI, you can unban anything.)
	       * -- jilles */
	      strcpy(pbufw, arg);
	      collapse(pbufw);
	      if (del_id(chptr, pbufw, CHFL_DENY) == 0)
	        strcpy(arg, pbufw);
	    }
	  }

          /* This stuff can go back in when all servers understand +e 
           * with the pbufw_new nonsense removed -Dianora
           */

          *mbufw++ = plus;
          *mbufw++ = 'd';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          opcnt++;

          break;

	case 'q':
        case 'b':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;

              if (errsent(SM_ERR_RPL_B, &errors_sent))
                break;
#ifdef HIDE_OPS
	      if(chan_op)
#endif
		{
		  for (lp = chptr->banlist; lp; lp = lp->next)
		    {
#ifdef BAN_CHANNEL_FORWARDING
                      if (lp->value.banptr->ban_forward_chname)
                        {
                          maskbufw = maskbuf;
                          ircsnprintf(maskbufw, 256, "%s!%s", lp->value.banptr->banstr, lp->value.banptr->ban_forward_chname);
                        }
                      else
#endif
	                  maskbufw = lp->value.banptr->banstr;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chptr->chname,
				 maskbufw,
				 lp->value.banptr->who,
				 lp->value.banptr->when);
		    }
		  for (lp = chptr->quietlist; lp; lp = lp->next)
		    {
		      maskbufw = maskbuf;
		      ircsnprintf(maskbufw, 256, "%%%s", lp->value.banptr->banstr);
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chptr->chname,
				 maskbufw,
				 lp->value.banptr->who,
				 lp->value.banptr->when);
		    }
		}
#ifdef HIDE_OPS
	      else
		{
		  for (lp = chptr->banlist; lp; lp = lp->next)
		    {
#ifdef BAN_CHANNEL_FORWARDING
                      if (lp->value.banptr->ban_forward_chname)
                        {
                          maskbufw = maskbuf;
                          ircsnprintf(maskbufw, 256, "%s!%s", lp->value.banptr->banstr, lp->value.banptr->ban_forward_chname);
                        }
                      else
#endif
	                  maskbufw = lp->value.banptr->banstr;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chptr->chname,
				 maskbufw,
				 "*",0);
		    }
		  for (lp = chptr->quietlist; lp; lp = lp->next)
		    {
		      maskbufw = maskbuf;
		      ircsnprintf(maskbufw, 256, "%%%s", lp->value.banptr->banstr);
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chptr->chname,
				 maskbufw,
				 "*",0);
		    }
		}
#endif /* HIDE_OPS */
              sendto_one(sptr, form_str(RPL_ENDOFBANLIST),
                         me.name, sptr->name, 
                         chptr->chname);
              break;
            }

          arg = check_string(*parv++);
	  if (whatt == MODE_DEL)
	    {
	      /* If we're removing a ban, and there is a leading %, odds are that
	       * the client is just deleting something it saw in the ban list.
	       * Silently remove it, as that's the indicator for a quiet ban
	       * -- asuffield
	       */
	      /* Now that quiets have their own banlist, this is no longer possible.
	       * Instead, treat them as -q. See same in MODE_ADD.
	       * -- gxti
	       */
	      if ((c == 'b') && (*arg == '%'))
	        {
		  arg++;
		  c = 'q';
		}
	      while (*arg == '%')
	        arg++;
	    }
	  if (whatt == MODE_ADD)
	    {
	      /* Bans prefixed with a % sign are supposed to be quiets
	       *  -- asuffield
	       */
	      if ((c == 'b') && (*arg == '%'))
		{
		  arg++;
		  c = 'q';
		}
              /* And ditch any more, they're too confusing */
	      while (*arg == '%')
		arg++;
	    }

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          /* user-friendly ban mask generation, taken
           * from Undernet's ircd  -orabidoo
	   */
	  arg = strip_colour(collapse(pretty_mask(arg)));

          /* Ignore colon at beginning of ban string.
           * Unfortunately, I can't ignore all such strings,
           * because otherwise the channel could get desynced.
           * I can at least, stop local clients from placing a ban
           * with a leading colon.
           *
           * Roger uses check_string() combined with an earlier test
           * in his TS4 code. The problem is, this means on a mixed net
           * one can't =remove= a colon prefixed ban if set from
           * an older server.
           * His code is more efficient though ;-/ Perhaps
           * when we've all upgraded this code can be moved up.
           *
           * -Dianora
           */

          if (MyClient(sptr))
            {
              if( (*arg == ':') && (whatt & MODE_ADD) )
                break;
            }

#ifdef BAN_CHANNEL_FORWARDING
	  /* add ban forwarding logic here
	     parse the arg such that if there are
	     2 ! in the banstr, then add a channel forward fchannel
	     as in nick!host!fchannel
	  */

	  if ((fchname=banstr_get_channel(arg))) 
	    {
	      arg = collapse(pretty_mask(banstr_remove_channel(arg)));
	      if (c == 'b' && whatt & MODE_ADD)
		{
		  if (MyClient(sptr) && !ChannelExists(fchname))
		    {
		      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
				 me.name, sptr->name, fchname);
		      break;
		    }
		  /* add test for CHANOPRIVSNEEDED in fchname here if needed */
		}
	      else
		/* It's a quiet, not a ban */
		fchname = NULL;
	    }
	  
#endif

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

	  {
	    int type = 0;
#ifdef BAN_CHANNEL_FORWARDING
	    int addret = 0;
#endif
	    if (c == 'b')
	      type = CHFL_BAN;
	    else if (c == 'q')
	      type = CHFL_QUIET;
	    
	    /* Do not propagate a ban that could not be added locally
	     * (avoids desync, overfull ban list, etc); do propagate
	     * all unbans even if they were not present locally to aid
	     * in fixing desyncs
	     */
#ifdef BAN_CHANNEL_FORWARDING
	    if ((whatt & MODE_ADD) && (addret = add_id(sptr, chptr, arg, type, fchname)) < 0)
#else
	    if ((whatt & MODE_ADD) && add_id(sptr, chptr, arg, type, NULL))
#endif
	      break;
            if (whatt & MODE_DEL)
	      {
	        del_id(chptr, arg, type);
#ifdef BAN_CHANNEL_FORWARDING
		fchname = forward_clear[0] != '\0' ? forward_clear : NULL;
#endif
	      }

#ifdef BAN_CHANNEL_FORWARDING
            /* If there was a duplicate ban overridden during a desync, take it out here. */
            if (addret == 1)
              {
                *clmbuf++ = '-';
                *clmbuf++ = c;
                strcpy(clpbuf, arg);
                if (*forward_clear)
                  {
                    strcat(clpbuf,"!");
                    strcat(clpbuf,forward_clear);
                  }
                clpbuf += strlen(clpbuf);
                *clpbuf++ = ' ';
              }
#endif
	  }

	  if (c == 'b')
	    {
	      *mbufw++ = plus;
	      *mbufw++ = c;
	      strcpy(pbufw, arg);
#ifdef BAN_CHANNEL_FORWARDING
	      if (fchname) /* always send forward channel so client banlists stay synced --gxti */
		{
		  strcat(pbufw,"!");
		  strcat(pbufw,fchname);
		}
#endif
	      pbufw += strlen(pbufw);
	      *pbufw++ = ' ';
	    }
	  else
	    {
	      *clmbuf++ = plus;
	      *clmbuf++ = 'b';
	      *svmbuf++ = plus;
	      *svmbuf++ = 'q';
	      *clpbuf++ = '%'; /* prepend the % for clients(+b) */
	      strcpy(clpbuf, arg);
	      strcpy(svpbuf, arg);
	      clpbuf += strlen(clpbuf);
	      svpbuf += strlen(svpbuf);
	      *clpbuf++ = ' ';
	      *svpbuf++ = ' ';
	    }
          len += tmp + 1;
          opcnt++;

          break;

        case 'l':
          if (whatt == MODE_QUERY)
            break;
          if (!isok || limitset++)
            {
              if (whatt == MODE_ADD && parc-- > 0)
                parv++;
              break;
            }

          if (whatt == MODE_ADD)
            {
              if (parc-- <= 0)
                {
                  if (MyClient(sptr))
                    sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                               me.name, sptr->name, "MODE +l");
                  break;
                }
              
              arg = check_string(*parv++);
              /*              if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
                break; */
              if ((nusers = atoi(arg)) <= 0)
                break;
              ircsnprintf(numeric, sizeof(numeric), "%d", nusers);
              if ((tmpc = strchr(numeric, ' ')))
                *tmpc = '\0';
              arg = numeric;

              tmp = strlen(arg);
              if (len + tmp + 2 >= MODEBUFLEN)
                break;

              chptr->mode.limit = nusers;
              chptr->mode.mode |= MODE_LIMIT;

              *mbufw++ = '+';
              *mbufw++ = 'l';
              strcpy(pbufw, arg);
              pbufw += strlen(pbufw);
              *pbufw++ = ' ';
              len += tmp + 1;
              /*              opcnt++;*/
            }
          else
            {
              chptr->mode.limit = 0;
              chptr->mode.mode &= ~MODE_LIMIT;
              *mbufw++ = '-';
              *mbufw++ = 'l';
            }

          break;

          /* Traditionally, these are handled separately
           * but I decided to combine them all into this one case
           * statement keeping it all sane
           *
           * The disadvantage is a lot more code duplicated ;-/
           *
           * -Dianora
           */

        case 'i' :
          if (whatt == MODE_QUERY)      /* shouldn't happen. */
            break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }
          if(MyClient(sptr))
            {
              if(done_i)
                break;
              else
                done_i = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
#ifdef OLD_NON_RED
              if(!(chptr->mode.mode & MODE_INVITEONLY))
#endif
                {
                  chptr->mode.mode |= MODE_INVITEONLY;
                  *mbufw++ = '+';
                  *mbufw++ = 'i';
                  len += 2;
                  /*              opcnt++; */
                }
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;

              while ((lp = chptr->invites))
                del_invite(lp->value.cptr, chptr);

	      chptr->mode.mode &= ~MODE_INVITEONLY;
	      *mbufw++ = '-';
	      *mbufw++ = 'i';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

          /* Un documented for now , I have no idea how this got here ;-) */
#ifdef JUPE_CHANNEL
        case 'j':
          if (whatt == MODE_QUERY)
            break;
          if(HasUmode(sptr,UMODE_GOD))
            {
              if (whatt == MODE_ADD)
                {
                  chptr->mode.mode |= MODE_JUPED;
		  *mbufw++ = '+';
		  *mbufw++ = 'j';
                  len += 2;

                  sendto_local_ops_flag(UMODE_SERVNOTICE, "%s!%s@%s juping channel %s",
				  sptr->name, sptr->username,
				  sptr->host, chptr->chname);
                }
              else if(whatt == MODE_DEL)
                {
                  chptr->mode.mode &= ~MODE_JUPED;
		  *mbufw++ = '-';
		  *mbufw++ = 'j';
                  len += 2;

                  sendto_local_ops_flag(UMODE_SERVNOTICE, "%s!%s@%s unjuping channel %s",
					sptr->name, sptr->username,
					sptr->host, chptr->chname);
                }
            }
          break;
#endif

        case 'm' :
          if (whatt == MODE_QUERY)
            break;
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if(MyClient(sptr))
            {
              if(done_m)
                break;
              else
                done_m = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_MODERATED;
	      *mbufw++ = '+';
	      *mbufw++ = 'm';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_MODERATED;
	      *mbufw++ = '-';
	      *mbufw++ = 'm';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 'n' :
          if (whatt == MODE_QUERY)
            break;
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if(MyClient(sptr))
            {
              if(done_n)
                break;
              else
                done_n = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_NOPRIVMSGS;
	      *mbufw++ = '+';
	      *mbufw++ = 'n';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_NOPRIVMSGS;
	      *mbufw++ = '-';
	      *mbufw++ = 'n';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 's' :
          if (whatt == MODE_QUERY)
            break;
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          /* ickity poo, traditional +p-s nonsense */

          if(MyClient(sptr))
            {
              if(done_s)
                break;
              else
                done_s = YES;
              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_SECRET;
	      *mbufw++ = '+';
	      *mbufw++ = 's';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_SECRET;
	      *mbufw++ = '-';
	      *mbufw++ = 's';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 't' :
          if (whatt == MODE_QUERY)
            break;
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if(MyClient(sptr))
            {
              if(done_t)
                break;
              else
                done_t = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_TOPICLIMIT;
	      *mbufw++ = '+';
	      *mbufw++ = 't';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_TOPICLIMIT;
	      *mbufw++ = '-';
	      *mbufw++ = 't';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

#ifdef INVITE_CHANNEL_FORWARDING
	  /* invite channel forwarding -goober */
        case 'f' :
	  if (whatt == MODE_QUERY || (whatt == MODE_ADD && parc <= 0))
	    {
	      if (!MyClient(sptr))
	        break;
	      if (errsent(SM_ERR_RPL_F, &errors_sent))
	        break;
	      if (chptr->mode.mode & MODE_FORWARD)
	        sendto_one(sptr, ":%s NOTICE %s :%s forward channel is %s",
                           me.name, sptr->name, chptr->chname,
			   chptr->mode.invite_forward_channel_name);
	      else
	        sendto_one(sptr, ":%s NOTICE %s :%s has no forward channel",
                           me.name, sptr->name, chptr->chname);
	      break;
	    }
	  
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if(MyClient(sptr))
            {
              if(done_f)
                break;
              else
                done_f = YES;
            }

          if(whatt == MODE_ADD)
            {
/* 	      struct Channel *fchptr; */

	      if (parc-- <= 0)
                {
                  if (MyClient(sptr))
                    sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),  me.name,
			       sptr->name, "MODE +f");
                  break;
                }

	      arg = check_string(*parv++);

	      tmp = strlen(arg);
	      
	      if (MyClient(sptr) && !ChannelExists(arg))
		{
		  sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
			     me.name, sptr->name, arg);
		  break;
		}

/* 	      fchptr=hash_find_channel(arg,NULL); */
	      /* add test for CHANOPRIVSNEEDED in fchptr here if needed */

	      if (len + tmp + 2 >= MODEBUFLEN)
                break;
		  
	      strncpy_irc(chptr->mode.invite_forward_channel_name, arg, CHANNELLEN + 1);
	      chptr->mode.mode |= MODE_FORWARD;
	      *mbuf2w++ = '+';
	      *mbuf2w++ = 'f';
	      strcpy(pbuf2w,arg);
	      pbuf2w += strlen(pbuf2w);
	      *pbuf2w++ = ' ';
	      len += tmp + 1;
            }
          else
            {
	      chptr->mode.mode &= ~MODE_FORWARD;
	      *mbuf2w++ = '-';
	      *mbuf2w++ = 'f';
	      len += 2;
	      chptr->mode.invite_forward_channel_name[0] = '\0';
            }
          break;
#endif

        case 'J':
          /* Join throttle */
	  if (whatt == MODE_QUERY)
	    break;
	  
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if (whatt == MODE_DEL)
            {
	      if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.join_throttle_frequency = 0;
              chptr->join_throttle_count = 0;
              *mbuf2w++ = '-';
              *mbuf2w++ = 'J';
              len += 2;
              break;
            }

          /* Ok, we're trying to add this */
          if (len + 2 + 12 >= MODEBUFLEN) /* +J 65535,65535 is 2 + 12 */
            break;

          arg = check_string(*parv++);

          {
            char *c = strchr(arg, ',');
            if (!c)
              break;
            *c++ = '\0';
            chptr->mode.join_throttle_frequency = strtoul(arg, NULL, 0);
            /* Setting +J 0,y is equivalent to setting -J */
            if (chptr->mode.join_throttle_frequency == 0)
              {
                chptr->mode.join_throttle_frequency = 0;
                chptr->join_throttle_count = 0;
                *mbuf2w++ = '-';
                *mbuf2w++ = 'J';
                len += 2;
              }
            else
              {
                if (chptr->mode.join_throttle_frequency > 65535)
                  chptr->mode.join_throttle_frequency = 65535;
                chptr->mode.join_throttle_limit = strtoul(c, NULL, 0);
                if (chptr->mode.join_throttle_limit > 65535)
                  chptr->mode.join_throttle_limit = 65535;
                if (chptr->join_throttle_count > chptr->mode.join_throttle_limit)
                  chptr->join_throttle_count = chptr->mode.join_throttle_limit;
                chptr->last_join_time = CurrentTime;
                *mbuf2w++ = '+';
                *mbuf2w++ = 'J';
                len += 2;
                ircsnprintf(pbuf2w, 16, "%d,%d", chptr->mode.join_throttle_frequency, chptr->mode.join_throttle_limit);
                len += strlen(pbuf2w) + 1;
                pbuf2w += strlen(pbuf2w);
	        *pbuf2w++ = ' ';
              }
          }          

          break;

        case 'D':
          /* Autodline */
	  if (whatt == MODE_QUERY)
	    break;
	  
	  if(!HasUmode(sptr,UMODE_AUTODLINE))
	    {
	      if(MyClient(sptr))
		sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
			   me.name, sptr->name);
	      break;
	    }
	  
          if (whatt == MODE_DEL)
            {
	      if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.autodline_frequency = 0;
              chptr->autodline_count = 0;
              *mbuf2w++ = '-';
              *mbuf2w++ = 'D';
              len += 2;
	      /* Note: can't -D/+D from a server */
	      sendto_local_ops_flag(UMODE_SERVNOTICE, "%s!%s@%s disabling autodline on channel %s",
			      sptr->name, sptr->username,
			      sptr->host, chptr->chname);
              break;
            }

          /* Ok, we're trying to add this */
          if (len + 2 + 12 >= MODEBUFLEN) /* +D 65535,65535 is 2 + 12 */
            break;

          arg = check_string(*parv++);

          {
            char *c = strchr(arg, ',');
            if (!c)
              break;
            *c++ = '\0';
            chptr->mode.autodline_frequency = strtoul(arg, NULL, 0);
            /* Setting +D 0,y is equivalent to setting -D */
            if (chptr->mode.autodline_frequency == 0)
              {
                chptr->mode.autodline_frequency = 0;
                chptr->autodline_count = 0;
                *mbuf2w++ = '-';
                *mbuf2w++ = 'D';
                len += 2;
		sendto_local_ops_flag(UMODE_SERVNOTICE, "%s!%s@%s disabling autodline on channel %s",
				sptr->name, sptr->username,
				sptr->host, chptr->chname);
              }
            else
              {
                /* We're really trying to set it */
                if (chptr->mode.autodline_frequency > 65535)
                  chptr->mode.autodline_frequency = 65535;
                chptr->mode.autodline_limit = strtoul(c, NULL, 0);
                if (chptr->mode.autodline_limit > 65535)
                  chptr->mode.autodline_limit = 65535;
                if (chptr->autodline_count > chptr->mode.autodline_limit)
                  chptr->autodline_count = chptr->mode.autodline_limit;
                chptr->last_autodline_time = CurrentTime;
                *mbuf2w++ = '+';
                *mbuf2w++ = 'D';
                len += 2;
                ircsnprintf(pbuf2w, 16, "%d,%d", chptr->mode.autodline_frequency, chptr->mode.autodline_limit);
		sendto_local_ops_flag(UMODE_SERVNOTICE, "%s!%s@%s setting autodline (%s) on channel %s",
				sptr->name, sptr->username,
				sptr->host, pbuf2w, chptr->chname);
                len += strlen(pbuf2w) + 1;
                pbuf2w += strlen(pbuf2w);
	        *pbuf2w++ = ' ';
              }
          }          

          break;

	case 'g':
	  if(whatt == MODE_QUERY)
	    break;
	  
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }
	  
	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_FREEINVITE;
	      *mbufw++ = '+';
	      *mbufw++ = 'g';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_FREEINVITE;
	      *mbufw++ = '-';
	      *mbufw++ = 'g';
	    }
          break;

	case 'c':  /* AGB */
	  if(whatt == MODE_QUERY)
	    break;
	  
          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }
	  
	  if(whatt == MODE_ADD)
	    {
	      /* Trying to add c mode. */
	      chptr->mode.mode |= MODE_NOCOLOR;
	      *mbufw++ = '+';
	      *mbufw++ = 'c';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_NOCOLOR;
	      *mbufw++ = '-';
	      *mbufw++ = 'c';
	    }
          break;

    case 'z':
          if (whatt == MODE_QUERY)
              break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

          if (whatt == MODE_ADD)
            {
              /* Trying to add r mode. */
              chptr->mode.mode |= MODE_OPMODERATE;
              *mbufw++ = '+';
              *mbufw++ = 'z';
            }
          else
            {
              chptr->mode.mode &= ~MODE_OPMODERATE;
              *mbufw++ = '-';
              *mbufw++ = 'z';
            }
          
          break;

	case 'P':
	  if(whatt == MODE_QUERY)
	    break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

	  if(!HasUmode(sptr,UMODE_GOD))
	    {
	      if(MyClient(sptr))
		sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
			   me.name, sptr->name);
	      break;
	    }
	  
	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_PERM;
	      *mbufw++ = '+';
	      *mbufw++ = 'P';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_PERM;
	      *mbufw++ = '-';
	      *mbufw++ = 'P';
	    }
          break;

	case 'L':
	  if(whatt == MODE_QUERY)
	    break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

	  if(!HasUmode(sptr,UMODE_EXPERIMENTAL))
	    {
	      if(MyClient(sptr))
		sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
			   me.name, sptr->name);
	      break;
	    }
	  
	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_LARGEBANLIST;
	      *mbufw++ = '+';
	      *mbufw++ = 'L';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_LARGEBANLIST;
	      *mbufw++ = '-';
	      *mbufw++ = 'L';
	    }
          break;

	case 'Q':
	  if(whatt == MODE_QUERY)
	    break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_NOFORWARD;
	      *mbufw++ = '+';
	      *mbufw++ = 'Q';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_NOFORWARD;
	      *mbufw++ = '-';
	      *mbufw++ = 'Q';
	    }
          break;

	case 'r':
	  if(whatt == MODE_QUERY)
	    break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_NOUNIDENT;
	      *mbufw++ = '+';
	      *mbufw++ = 'r';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_NOUNIDENT;
	      *mbufw++ = '-';
	      *mbufw++ = 'r';
	    }
          break;

	case 'R':
	  if(whatt == MODE_QUERY)
	    break;

          if (!isok)
            {
              if (MyClient(sptr) && !warned_no_op)
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
	      warned_no_op = 1;
	      break;
            }

	  if(whatt == MODE_ADD)
	    {
	      chptr->mode.mode |= MODE_QUIETUNIDENT;
	      *mbufw++ = '+';
	      *mbufw++ = 'R';
	    }
	  else
	    {
	      chptr->mode.mode &= ~MODE_QUIETUNIDENT;
	      *mbufw++ = '-';
	      *mbufw++ = 'R';
	    }
          break;

        default:
          if (whatt == MODE_QUERY)
            break;

          /* only one "UNKNOWNMODE" per mode... we don't want
          ** to generate a storm, even if it's just to a 
          ** local client  -orabidoo
          ** Instead of the cute hackery you can just abort the mode
          ** command entirely on the first error.. this has the advangage 
          ** of not garbling the channel modes on typo.
          ** 	-InnerFIRE
          */
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_UNKNOWNMODE), me.name, sptr->name, c);
          /* And when we do this, we'll do it by terminating the loop
           * and sending out the notifications, not by silently
           * changing the modes without telling anybody. Sheesh.
           *  -- asuffield
           */
          parc = 0;
          curr = NULL;
          break;
        }
    }

  /*
  ** WHEW!!  now all that's left to do is put the various bufs
  ** together and send it along.
  */


  *mbuf2w = *pbuf2w = '\0';
  *clmbuf = *clpbuf = *svmbuf = *svpbuf = '\0';
  strncpy_irc(mbufw, modebuf2, MODEBUFLEN - strlen(modebuf));
  strncpy_irc(pbufw, parabuf2, MODEBUFLEN - strlen(parabuf));

  if(*modebuf || *clmodebuf || *svmodebuf) /* yaright? assert(1). -- asuffield */
    {
      /* construct the client response */
      strncpy_irc(modetmp, modebuf, MODEBUFLEN);
      strncpy_irc(modetmp + strlen(modetmp), clmodebuf, MODEBUFLEN - strlen(modetmp));
      strncpy_irc(paratmp, parabuf, MODEBUFLEN);
      strncpy_irc(paratmp + strlen(paratmp), clparabuf, MODEBUFLEN - strlen(paratmp));
      collapse_signs(modetmp);

      /* We suppress the broadcast to channel members if this comes
       * from a server and is comprised entirely of ban types
       *
       * Otherwise the amount of data broadcast is too large to be
       * useful, and desync attacks should be impossible anyway.
       */
      /* No, just send it on to avoid confusion about bans. Since we
       * drop any identical bans, there will only be much traffic in case
       * of recreated channels (restarted servers and some other cases).
       * A problem is that nonops get the full +eI lists in that case.
       * -- jilles
       */
      /*if (!IsServer(sptr) || non_bans_present)*/
        {
#ifdef HIDE_OPS
          sendto_channel_chanops_butserv(chptr, sptr, ":%s MODE %s %s %s", 
                                         IsServer(sptr) ? NETWORK_NAME : sptr->name, chptr->chname,
                                         modetmp, paratmp);
#else
          sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", 
                                 IsServer(sptr) ? NETWORK_NAME : sptr->name, chptr->chname,
                                 modetmp, paratmp);
#endif
        }


      /* and finally the server response */
      strncpy_irc(modetmp, modebuf, MODEBUFLEN);
      strncpy_irc(modetmp + strlen(modetmp), svmodebuf, MODEBUFLEN - strlen(modetmp));
      strncpy_irc(paratmp, parabuf, MODEBUFLEN);
      strncpy_irc(paratmp + strlen(paratmp), svparabuf, MODEBUFLEN - strlen(paratmp));
      collapse_signs(modetmp);

      sendto_match_servs(chptr, cptr, ":%s MODE %s %s %s",
                         sptr->name, chptr->chname,
                         modetmp, paratmp);
    }
  return;
}

int
can_join(struct Client *sptr, struct Channel *chptr, char *key, int *flags)
{
  Link  *lp;
  int ban_or_exception;

  if (HasUmode(sptr,UMODE_GOD)) return 0;
#ifdef JUPE_CHANNEL
  if(chptr->mode.mode & MODE_JUPED)
    {
      sendto_ops_flag(UMODE_SPY, get_str(STR_TRY_JOIN_JUPE), /* "User %s (%s@%s) is attempting to join juped channel %s" */
		      sptr->name,
		      sptr->username, sptr->host,chptr->chname);
      return (ERR_BADCHANNAME);
    }
#endif

  /* watch this if is_banned() changes, at present there is no way that
     CHFL_QUIET can be returned unless CHFL_BAN is also returned,
     but this may need rewriting if that ceases to be the case
     -- asuffield */
  if ( (ban_or_exception = is_banned(sptr, chptr)) & CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);
  else
    *flags |= ban_or_exception; /* Mark this client as "charmed" */

  if ((chptr->mode.mode & MODE_INVITEONLY) && !is_invex(sptr, chptr))
    {
      for (lp = sptr->user->invited; lp; lp = lp->next)
        if (lp->value.chptr == chptr)
          break;
      if (!lp)
        return (ERR_INVITEONLYCHAN);
    }

  if ((chptr->mode.mode & MODE_NOUNIDENT) && !HasUmode(sptr, UMODE_IDENTIFIED) && !sptr->user->servlogin[0] && !is_invex(sptr, chptr))
    {
#if 0
      /* This never does anything at all because you can only
       * effectively /invite to +i chans -- jilles */
      for (lp = sptr->user->invited; lp; lp = lp->next)
        if (lp->value.chptr == chptr)
          break;
      if (!lp)
#endif
        return (ERR_NEEDREGGEDNICK);
    }

  if (*chptr->mode.key && (BadPtr(key) || irccmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);

  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  /* Check join throttle, if any */
  /* matching a +I allows you in through any +J, but you are still counted
   * -- jilles */
  if (chptr->mode.join_throttle_frequency && !is_invex(sptr, chptr))
    if (chptr->join_throttle_count >=
         chptr->mode.join_throttle_limit + ((CurrentTime - chptr->last_join_time) / chptr->mode.join_throttle_frequency))
      return ERR_THROTTLED;

  return 0;
}

/*
 * check_channel_name - check channel name for invalid characters
 * return true (1) if name ok, false (0) otherwise
 */
int check_channel_name(const char* name)
{
  assert(0 != name);
  
  for ( ; *name; ++name) {
    if (!IsChanChar(*name))
      return 0;
  }
  return 1;
}

/*
**  Get Channel block for chname (and allocate a new channel
**  block, if it didn't exist before).
*/
static struct Channel* get_channel(struct Client *cptr, const char *chname, int flag)
{
  struct Channel *chptr;
  int   len;

  if (BadPtr(chname))
    return NULL;

  len = strlen(chname);
  if (MyClient(cptr) && len > CHANNELLEN)
    return NULL;

  if ((chptr = hash_find_channel(chname, NULL)))
    return (chptr);

  /*
   * If a channel is created during a split make sure its marked
   * as created locally 
   * Also make sure a created channel has =some= timestamp
   * even if it get over-ruled later on. Lets quash the possibility
   * an ircd coder accidentally blasting TS on channels. (grrrrr -db)
   *
   * Actually, it might be fun to make the TS some impossibly huge value (-db)
   */

  if (flag == CREATE)
    {
      /* WHO THE HELL DID THIS?
       * It must die in 1.1
       *  -- asuffield
       */
      expect_malloc;
      chptr = (struct Channel*) MyMalloc(sizeof(struct Channel) + len + 1);
      malloc_log("get_channel() allocated struct Channel for %s (%zd bytes) at %p",
                 chname, sizeof(struct Channel) + len + 1, (void *)chptr);
      memset(chptr, 0, sizeof(struct Channel));
      /*
       * NOTE: strcpy ok here, we have allocated strlen + 1
       */
      strcpy(chptr->chname, chname);
      if (channel)
        channel->prevch = chptr;
      chptr->prevch = NULL;
      chptr->nextch = channel;
      channel = chptr;
      chptr->channelts = CurrentTime;     /* doesn't hurt to set it here */
      add_to_channel_hash_table(chname, chptr);
      Count.chan++;
    }
  return chptr;
}

static  void    add_invite(struct Client *cptr,struct Channel *chptr)
{
  Link  *inv, **tmp;

  del_invite(cptr, chptr);
  /*
   * delete last link in chain if the list is max length
   */
  if (list_length(cptr->user->invited) >= MAXCHANNELSPERUSER)
    {
      /*                This forgets the channel side of invitation     -Vesa
                        inv = cptr->user->invited;
                        cptr->user->invited = inv->next;
                        free_link(inv);
*/
      del_invite(cptr, cptr->user->invited->value.chptr);
 
    }
  /*
   * add client to channel invite list
   */
  inv = make_link();
  inv->value.cptr = cptr;
  inv->next = chptr->invites;
  chptr->invites = inv;
  /*
   * add channel to the end of the client invite list
   */
  for (tmp = &(cptr->user->invited); *tmp; tmp = &((*tmp)->next))
    ;
  inv = make_link();
  inv->value.chptr = chptr;
  inv->next = NULL;
  (*tmp) = inv;
}

/*
 * Delete Invite block from channel invite list and client invite list
 */
void    del_invite(struct Client *cptr,struct Channel *chptr)
{
  Link  **inv, *tmp;

  for (inv = &(chptr->invites); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.cptr == cptr)
      {
        *inv = tmp->next;
        free_link(tmp);
        break;
      }

  for (inv = &(cptr->user->invited); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
        *inv = tmp->next;
        free_link(tmp);
        break;
      }
}

/*
**  Subtract one user from channel (and free channel
**  block, if channel became empty).
*/
static  void    sub1_from_channel(struct Channel *chptr)
{
  Link *tmp;

  if (--chptr->users <= 0)
    {
      chptr->users = 0; /* if chptr->users < 0, make sure it sticks at 0
                         * It should never happen but...
                         */

      if (chptr->mode.mode & MODE_PERM)
	/* This is a permanent channel. Do not delete it. */
	return;

#if 0
      while ((tmp = chptr->loggers))
        {
          Link **curr, *target;
          for (curr = &tmp->value.cptr->user->logging; (target = *curr); curr = &target->next)
            {
              if (target->value.chptr == chptr)
                {
                  *curr = target->next;
                  free_link(target);
                  tmp->value.cptr->user->logcount--;
                  chptr->logcount--;
                  break;
                }
            }
          chptr->loggers = tmp->next;
          sendto_one(tmp->value.cptr, ":%s LPART %s",
                     me.name, chptr->chname);
          free_link(tmp);
        }
#endif

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
      if(server_was_split)
        {
          /*
           * Now, find all invite links from channel structure
           */
          /* The idea here is, not to "forget" the channel mode
           * the ban list, exception lists, and not to release
           * the channel at this time.
           * The invite list should be forgotten now, as well
           * as the flooder lists
           * -db
           */

          while ((tmp = chptr->invites))
            del_invite(tmp->value.cptr, chptr);

#ifdef FLUD
          free_fluders(NULL, chptr);
#endif
          /* flag the channel as split */
          chptr->mode.mode |= MODE_SPLIT;

          /* Add to double linked empty channel list */
          if(empty_channel_list)
            empty_channel_list->last_empty_channel = chptr;
          chptr->last_empty_channel = (struct Channel *)NULL;
          chptr->next_empty_channel = empty_channel_list;
          empty_channel_list = chptr;
        }
      else
#endif
#ifdef JUPE_CHANNEL
        if( chptr->mode.mode & MODE_JUPED )
          {
            while ((tmp = chptr->invites))
              del_invite(tmp->value.cptr, chptr);
            
#ifdef FLUD
            free_fluders(NULL, chptr);
#endif
          }
        else
#endif
       {
          /*
           * Now, find all invite links from channel structure
           */
          while ((tmp = chptr->invites))
            del_invite(tmp->value.cptr, chptr);

	  /* free all bans/exceptions/denies */
	  free_bans_exceptions_denies( chptr );

          if (chptr->prevch)
            chptr->prevch->nextch = chptr->nextch;
          else
            channel = chptr->nextch;
          if (chptr->nextch)
            chptr->nextch->prevch = chptr->prevch;

#ifdef FLUD
          free_fluders(NULL, chptr);
#endif
          del_from_channel_hash_table(chptr->chname, chptr);
          MyFree((char*) chptr);
          Count.chan--;
        }
    }
}

/*
 * free_bans_exceptions_denies
 *
 * inputs	- pointer to channel structure
 * output	- none
 * side effects	- all bans/exceptions denies are freed for channel
 */

static void free_bans_exceptions_denies(struct Channel *chptr)
{
  Link *ban;
  Link *next_ban;

  for(ban = chptr->banlist; ban; ban = next_ban)
    {
      next_ban = ban->next;
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
      free_link(ban);
    }

  for(ban = chptr->quietlist; ban; ban = next_ban)
    {
      next_ban = ban->next;
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
      free_link(ban);
    }

  for(ban = chptr->exceptlist; ban; ban = next_ban)
    {
      next_ban = ban->next;
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
      free_link(ban);
    }

  for(ban = chptr->denylist; ban; ban = next_ban)
    {
      next_ban = ban->next;
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
      free_link(ban);
    }

  chptr->banlist = chptr->quietlist = chptr->exceptlist = chptr->denylist = NULL;
  chptr->num_bed = 0;
}


#ifdef NEED_SPLITCODE

/*
 * check_still_split()
 *
 * inputs       -NONE
 * output       -NONE
 * side effects -
 * Check to see if the server split timer has expired, if so
 * check to see if there are now a decent number of servers connected
 * and users present, so I can consider this split over.
 *
 * -Dianora
 */

static void check_still_split()
{
  if((server_split_time + SPLITDELAY) < CurrentTime)
    {
      if((Count.server >= SPLITNUM) &&
#ifdef SPLIT_PONG
         (got_server_pong == YES) &&
#endif
         (Count.total >= SPLITUSERS))
        {
          /* server hasn't been split for a while.
           * -Dianora
           */
          server_was_split = NO;
          sendto_local_ops_flag(UMODE_SERVNOTICE, get_str(STR_SPLIT_MODE_OFF));
          cold_start = NO;
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
          remove_empty_channels();
#endif
        }
      else
        {
          server_split_time = CurrentTime; /* still split */
          server_was_split = YES;
        }
    }
}
#endif

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
/*
 * remove_empty_channels
 *
 * inputs       - none
 * output       - none
 * side effects - remove all channels on empty_channel_list that have
 * 
 * Any channel struct on this link list, is here because it had
 * no members, hence normally would not exist through a split.
 * If after the split is over, there are any channels left in this
 * list, they must be removed. Whenever a channel gains a member
 * whether locally or from a remote SJOIN it is removed from this list.
 */

void remove_empty_channels()
{
  Link *tmp;
  Link  *obtmp;
  struct Channel *next_empty_channel;

  for(;empty_channel_list;
      empty_channel_list = next_empty_channel )
    {
      next_empty_channel = empty_channel_list->next_empty_channel;

      if(empty_channel_list->users)             /* sanity test */
        {
	  /* This is an oddity, rather than an out and out error
	   * if this happens, a client managed to join the channel
	   * making it non zero users, and I didn't notice. That means
	   * strictly speaking its an error. However, if this entry is
	   * ignored, its a non fatal one.
	   */
          empty_channel_list->next_empty_channel = (struct Channel *)NULL;
          empty_channel_list->last_empty_channel = (struct Channel *)NULL;
          continue;
        }

      /*
       * Now, find all invite links from channel structure
       */
      while ((tmp = empty_channel_list->invites))
        del_invite(tmp->value.cptr, empty_channel_list);

      /* This code used to be an exact clone of free_bans_exceptions_denies,
       * except for a lack of deny freeing(leak?), so was changed to a call.
       * -- gxti
       */
      free_bans_exceptions_denies(empty_channel_list);
      
      if (empty_channel_list->prevch)
        empty_channel_list->prevch->nextch = empty_channel_list->nextch;
      else
        channel = empty_channel_list->nextch;
      if (empty_channel_list->nextch)
        empty_channel_list->nextch->prevch = empty_channel_list->prevch;
      
#ifdef FLUD
      free_fluders(NULL, empty_channel_list);
#endif
      del_from_channel_hash_table(empty_channel_list->chname, 
                                        empty_channel_list);
      MyFree((char*) empty_channel_list);
      Count.chan--;
    }
}
#endif

static int do_join(struct Client *cptr,
                   struct Client *sptr,
                   char *jbuf, char *key_string)
{
  Link  *lp;
  struct Channel *chptr = NULL;
  char  *name, *key = NULL;
  int   i, flags = 0;
#ifdef NO_CHANOPS_ON_SPLIT
  int   allow_op=YES;
#endif
  char  *p = NULL, *p2 = NULL;
#ifdef ANTI_SPAMBOT
  int   successful_join_count = 0; /* Number of channels successfully joined */
#endif

  p = NULL;
  if (key_string)
    key = strtoken(&p2, key_string, ",");
  for (name = strtoken(&p, jbuf, ","); name;
       key = (key) ? strtoken(&p2, NULL, ",") : NULL,
         name = strtoken(&p, NULL, ","))
    {
      /*
      ** JOIN 0 sends out a part for all channels a user
      ** has joined.
      */
      if (*name == '0' && !atoi(name))
        {
          if (sptr->user->channel == NULL)
            continue;
          while ((lp = sptr->user->channel))
            {
              chptr = lp->value.chptr;
              sendto_channel_butserv(chptr, sptr, PartFmt,
                                     sptr->name, chptr->chname, "");
              remove_user_from_channel(sptr, chptr, 0);
            }

#ifdef ANTI_SPAMBOT       /* Dianora */

          if( MyConnect(sptr) && !NoFloodProtection(sptr) )
            {
              if(SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
                {
                  sendto_ops_flag(UMODE_BOTS, get_str(STR_BOT_WARN), /* "User %s (%s@%s) is a possible spambot" */
				  sptr->name,
				  sptr->username, sptr->host);
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
              else
                {
                  int t_delta;

                  if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
                      JOIN_LEAVE_COUNT_EXPIRE_TIME)
                    {
                      int decrement_count;
                      decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

                      if(decrement_count > sptr->join_leave_count)
                        sptr->join_leave_count = 0;
                      else
                        sptr->join_leave_count -= decrement_count;
                    }
                  else
                    {
                      if((CurrentTime - (sptr->last_join_time)) < SPAMTIME)
                        {
                          /* oh, its a possible spambot */
                          sptr->join_leave_count++;
                        }
                    }
                  sptr->last_leave_time = CurrentTime;
                }
            }
#endif
          sendto_match_servs(NULL, cptr, ":%s JOIN 0", sptr->name);
          continue;
        }
      
      if (MyConnect(sptr))
        {
          /*
          ** local client is first to enter previously nonexistent
          ** channel so make them (rightfully) the Channel
          ** Operator.
          */
           /*     flags = (ChannelExists(name)) ? 0 : CHFL_CHANOP; */

          /* To save a redundant hash table lookup later on */
           
           if((chptr = hash_find_channel(name, NullChn)))
             flags = 0;
           else
             flags = CHFL_CHANOP;

           /* if its not a local channel, or isn't an oper
            * and server has been split
            */

#ifdef NO_CHANOPS_ON_SPLIT
          if(!HasUmode(sptr,UMODE_GOD) && server_was_split)
            {
              allow_op = NO;

              if(!IsRestricted(sptr) && (flags & CHFL_CHANOP))
                sendto_one(sptr, form_str(ERR_NO_OP_SPLIT),
			   me.name,
			   sptr->name);
            }
#endif

          if ((sptr->user->joined >= MAXCHANNELSPERUSER) &&
             (!HasUmode(sptr,UMODE_MORECHANS) || (sptr->user->joined >= MAXCHANNELSMORE)))
            {
              sendto_one(sptr, form_str(ERR_TOOMANYCHANNELS),
                         me.name, sptr->name, name);
#ifdef ANTI_SPAMBOT
              if(successful_join_count)
                sptr->last_join_time = CurrentTime;
#endif
              return 0;
            }

#ifdef ANTI_SPAMBOT       /* Dianora */
          if(flags == 0)        /* if channel doesn't exist, don't penalize */
            successful_join_count++;
          if( SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
            { 
              /* Its already known as a possible spambot */
 
              if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
                sptr->oper_warn_count_down--;
              else
                sptr->oper_warn_count_down = 0;
 
              if(sptr->oper_warn_count_down == 0)
                {
                  sendto_ops_flag(UMODE_BOTS, get_str(STR_BOT_WARN_JOIN), /* "User %s (%s@%s) trying to join %s is a possible spambot" */
				  sptr->name,
				  sptr->username,
				  sptr->host,
				  name);     
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
#ifndef ANTI_SPAMBOT_WARN_ONLY
              return 0; /* Don't actually JOIN anything, but don't let
                           spambot know that */
#endif
            }
#endif
        }
      else
        {
          /*
          ** complain for remote JOINs to non-existing channels
          ** (they should be SJOINs) -orabidoo
          */
          if (!ChannelExists(name))
            ts_warn("User on %s remotely JOINing new channel", 
                    sptr->user->server);
        }

#if defined(NO_JOIN_ON_SPLIT)
      if(server_was_split)
        {
          if( chptr )   /* The channel existed, so I can't join it */
            {
              if (IsMember(sptr, chptr)) /* already a member, ignore this */
                continue;

              /* allow local joins to this channel */
              if( (chptr->users == 0) && !HasUmode(sptr, UMODE_GOD) )
                {
                  sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                             me.name, sptr->name, name);
                  continue;
                }
            }
          else
	    {
	      /* The channel did not exist. Create it. */
	      if (!(chptr = get_channel(sptr, name, CREATE)))
		{
		  sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
			     me.name, sptr->name, name);
#ifdef ANTI_SPAMBOT
		  if(successful_join_count > 0)
		    successful_join_count--;
#endif
		  continue;
		}
#ifdef CHANNEL_CREATION_NOTICE
	      sendto_ops_flag(UMODE_CHCREATE, "Channel %s created by %s!%s@%s on %s", chptr->chname, cptr->name, 
				    cptr->username, cptr->host, me.name);
#endif /* CHANNEL_CREATION_NOTICE */
	    }
        }
      else
#endif
      if(!chptr)        /* If I already have a chptr, no point doing this */
	{
	  /* The channel did not exist. Create it. */
	  if (!(chptr = get_channel(sptr, name, CREATE)))
	    {
	      sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
			 me.name, sptr->name, name);
#ifdef ANTI_SPAMBOT
	      if(successful_join_count > 0)
		successful_join_count--;
#endif
	      continue;
	    }
#ifdef CHANNEL_CREATION_NOTICE
	  sendto_ops_flag(UMODE_CHCREATE, "Channel %s created by %s!%s@%s on %s", chptr->chname, cptr->name, 
				cptr->username, cptr->host, me.name);
#endif /* CHANNEL_CREATION_NOTICE */
	}

      if(chptr)
        {
          if (IsMember(sptr, chptr))    /* already a member, ignore this */
            continue;
        }
      else
        {
           sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                      me.name, sptr->name, name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /*
       * can_join checks for +i key, bans.
       * If a ban is found but an exception to the ban was found
       * flags will have CHFL_EXCEPTION set
       */

      if (MyConnect(sptr) && (i = can_join(sptr, chptr, key, &flags)))
        {
#ifdef INVITE_CHANNEL_FORWARDING
	  if ((i == ERR_INVITEONLYCHAN || i == ERR_NEEDREGGEDNICK || i == ERR_THROTTLED)
              && (chptr->mode.mode & MODE_FORWARD))
	    {
	      attempt_channel_redirection(cptr, sptr, key, i, name, chptr, INVITE_FORWARD);
	      continue;
	    }
	  else
#endif
#ifdef BAN_CHANNEL_FORWARDING
	    if (i==ERR_BANNEDFROMCHAN)
	    {
	      attempt_channel_redirection(cptr, sptr, key, i, name, chptr, BAN_FORWARD);
	      continue;
	    }
	  else
#endif
	    sendto_one(sptr, form_str(i), me.name, sptr->name, name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /* Do the auto-dline thing */
      if (chptr->mode.autodline_frequency)
	{
          u_int32_t reduction = (CurrentTime - chptr->last_autodline_time) / chptr->mode.autodline_frequency;
          if (reduction > chptr->autodline_count)
            {
              chptr->autodline_count = 0;
              chptr->last_autodline_time = CurrentTime;
            }
          else
            {
              chptr->autodline_count -= reduction;
              chptr->last_autodline_time = CurrentTime - ((CurrentTime - chptr->last_autodline_time) % chptr->mode.autodline_frequency);
            }
          /* Note that we always increment count */
	  chptr->autodline_count++;
          /* If doing that pushed us over limit, clear the channel and leave count == limit */
          if (chptr->autodline_count > chptr->mode.autodline_limit)
            {
              autodline_clear_channel(chptr);
              chptr->autodline_count = chptr->mode.autodline_limit;
            }
          /* Note that the user currently joining is not dlined yet -
           * but if another client joins before audodline_frequency,
           * they will be
           */
	}
      else
        {
          chptr->autodline_count = 0;
          chptr->last_autodline_time = CurrentTime;
        }

      /*
      **  Complete user entry to the new channel (if any)
      */

#ifdef NO_CHANOPS_ON_SPLIT
      if(allow_op)
        {
          add_user_to_channel(chptr, sptr, flags);
        }
      else
        {
          add_user_to_channel(chptr, sptr, flags & CHFL_EXCEPTION);
        }
#else
      add_user_to_channel(chptr, sptr, flags);
#endif

      if (chptr->mode.join_throttle_frequency)
	{
          /* First, apply the decrement based on time */
          u_int32_t reduction = (CurrentTime - chptr->last_join_time) / chptr->mode.join_throttle_frequency;
          if (reduction > chptr->join_throttle_count)
            {
              /* Clip at the bottom end - the effective count should never drop below zero */
              chptr->join_throttle_count = 0;
              chptr->last_join_time = CurrentTime;
            }
          else
            {
              /* Smoothly decrement count, and keep the remained in last_join_time so we don't get rounding errors */
              chptr->join_throttle_count -= reduction;
              chptr->last_join_time = CurrentTime - ((CurrentTime - chptr->last_join_time) % chptr->mode.join_throttle_frequency);
            }

          /* Then, increment for this join */
	  chptr->join_throttle_count++;
	}
      else
        {
          chptr->join_throttle_count = 0;
          chptr->last_join_time = CurrentTime;
        }

      /*
      **  Set timestamp if appropriate, and propagate
      */
      if (MyClient(sptr) && (flags & CHFL_CHANOP) )
        {
          chptr->channelts = CurrentTime;
#ifdef NO_CHANOPS_ON_SPLIT
          if(allow_op)
            {
              sendto_match_servs(chptr, cptr,
                                 ":%s SJOIN %lu %s +ns :@%s", me.name,
                                 (long unsigned)chptr->channelts, name, sptr->name);
            }                                
          else
            sendto_match_servs(chptr, cptr,
                               ":%s SJOIN %lu %s +ns :%s", me.name,
                               (long unsigned)chptr->channelts, name, sptr->name);
#else
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s +ns :@%s", me.name,
                             (long unsigned)chptr->channelts, name, sptr->name);
#endif
        }
      else if (MyClient(sptr))
        {
	  /* send the modes in case this SJOIN remotely creates the channel
	   * this seems to do more harm than good
	   * (cannot turn off modes while someone is joining, and still
	   * desyncs badly) -- jilles */
#if 0
	  *modebuf = *parabuf = '\0';
	  channel_modes(cptr, modebuf, parabuf, chptr);
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s +%s %s :%s", me.name,
                             (long unsigned)chptr->channelts, name, modebuf,
			     parabuf, sptr->name);
#endif
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s + :%s", me.name,
                             (long unsigned)chptr->channelts, name, sptr->name);
        }
      else
        sendto_match_servs(chptr, cptr, ":%s JOIN :%s", sptr->name,
                           name);

      /*
      ** notify all other users on the new channel
      */
      sendto_channel_butserv(chptr, sptr, ":%s JOIN :%s",
                             sptr->name, name);

      if (MyClient(sptr))
        {
          if( flags & CHFL_CHANOP )
            {
              chptr->mode.mode |= MODE_NOPRIVMSGS | MODE_SECRET;

              sendto_channel_butserv(chptr, sptr,
                                     ":%s MODE %s +ns",
                                     me.name, chptr->chname);
            }

          del_invite(sptr, chptr);

          if (chptr->topic[0] != '\0')
            {
              sendto_one(sptr, form_str(RPL_TOPIC), me.name,
                         sptr->name, name, chptr->topic);
#ifdef TOPIC_INFO
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, sptr->name, name,
                         chptr->topic_nick,
                         chptr->topic_time);
#endif
            }
          {
            char *parv[2];
            parv[0] = sptr->name;
            parv[1] = name;
            m_names(cptr, sptr, 2, parv);
          }
        }
    }

#ifdef ANTI_SPAMBOT
  if(MyConnect(sptr) && successful_join_count)
    sptr->last_join_time = CurrentTime;
#endif

  return 0;
}

/*
** m_join
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = channel password (key)
*/
int     m_join(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  static char   jbuf[BUFSIZE];
  char  *name = NULL;
  int   i = 0;
  char *p = NULL;
  
  if (!(sptr->user))
    {
      /* something is *fucked* - bail */
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "JOIN");
      return 0;
    }


#ifdef NEED_SPLITCODE

  /* Check to see if the timer has timed out, and if so, see if
   * there are a decent number of servers now connected 
   * to consider any possible split over.
   * -Dianora
   */

  if (server_was_split)
    check_still_split();

#endif

  *jbuf = '\0';
  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, parv[1], ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      if (!check_channel_name(name))
        {
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                       me.name, parv[0], (unsigned char*) name);
          continue;
        }
      if (*name == '0' && !atoi(name))
        *jbuf = '\0';
      else if (!IsChannelName(name))
        {
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                       me.name, parv[0], name);
          continue;
        }


#ifdef NO_JOIN_ON_SPLIT_SIMPLE
      if (server_was_split && MyClient(sptr) && !HasUmode(sptr, UMODE_GOD))
        {
              sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                         me.name, parv[0], name);
              continue;
        }
#endif /* NO_JOIN_ON_SPLIT_SIMPLE */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
      /* If from a cold start, there were never any channels
       * joined, hence all of them must be considered off limits
       * until this server joins the network
       *
       * cold_start is set to NO if SPLITDELAY is set to 0 in m_set()
       */

      if(cold_start && MyClient(sptr) && !HasUmode(sptr, UMODE_GOD))
        {
              sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                         me.name, parv[0], name);
              continue;
        }
#endif
      if (*jbuf)
        (void)strcat(jbuf, ",");
      (void)strncat(jbuf, name, sizeof(jbuf) - i - 1);
      i += strlen(name)+1;
    }

  return do_join(cptr, sptr, jbuf, (parc > 2) ? parv[2] : NULL);
}

/*
 * m_part
 *      parv[0] = sender prefix
 *      parv[1] = channel
 */
int     m_part(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Channel *chptr;
  char  *p, *name;

  if (parc < 2 || parv[1][0] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "PART");
      return 0;
    }

  name = strtoken( &p, parv[1], ",");

#ifdef ANTI_SPAMBOT     /* Dianora */
      /* if its my client, and isn't an oper */

      if (name && MyConnect(sptr) && !NoFloodProtection(sptr))
        {
          if(SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
            {
              sendto_ops_flag(UMODE_BOTS, get_str(STR_BOT_WARN), /* "User %s (%s@%s) is a possible spambot" */
			      sptr->name,
			      sptr->username, sptr->host);
              sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
            }
          else
            {
              int t_delta;

              if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
                  JOIN_LEAVE_COUNT_EXPIRE_TIME)
                {
                  int decrement_count;
                  decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

                  if(decrement_count > sptr->join_leave_count)
                    sptr->join_leave_count = 0;
                  else
                    sptr->join_leave_count -= decrement_count;
                }
              else
                {
                  if( (CurrentTime - (sptr->last_join_time)) < SPAMTIME)
                    {
                      /* oh, its a possible spambot */
                      sptr->join_leave_count++;
                    }
                }
              sptr->last_leave_time = CurrentTime;
            }
        }
#endif

  while ( name )
    {
      chptr = get_channel(sptr, name, 0);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                     me.name, parv[0], name);
          name = strtoken(&p, (char *)NULL, ",");
          continue;
        }

      if (!IsMember(sptr, chptr))
        {
          sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                     me.name, parv[0], name);
          name = strtoken(&p, (char *)NULL, ",");
          continue;
        }
      /*
       *  Remove user from the old channel (if any)
       */

      /* construct the reason string, if any */
#ifdef TIDY_PART
      buf[0] = '\0';
      if ((parc == 3) && parv[2] && (can_send(sptr, chptr) == 0))
	{
	  if (strlen(parv[2]) > (MAX_PART_LENGTH - 2))
	    parv[2][MAX_PART_LENGTH - 2] = '\0';
	  /* Don't add quotes to a null message */
	  if (MyConnect(sptr) && *parv[2])
	    {
	      strncpy_irc(buf, "\"", 2);
	      strncat(buf, strip_colour(parv[2]), MAX_PART_LENGTH - 2);
	      strncat(buf, "\"", 1);
	    }
	  else
	    strcpy(buf, parv[2]);
	}

      sendto_match_servs(chptr, cptr, PartFmt, parv[0], name, buf);
            
      sendto_channel_butserv(chptr, sptr, PartFmt, parv[0], name, buf);
#else
      sendto_match_servs(chptr, cptr, PartFmt, parv[0], name, ((parc == 3) && (can_send(sptr, chptr) == 0)) ? parv[2] : "");
            
      sendto_channel_butserv(chptr, sptr, PartFmt, parv[0], name, ((parc == 3) && (can_send(sptr, chptr) == 0)) ? parv[2] : "");
#endif
      remove_user_from_channel(sptr, chptr, 0);
      name = strtoken(&p, (char *)NULL, ",");
    }
  return 0;
}

/*
** m_kick
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = client to kick
**      parv[3] = kick comment
*/
/*
 * I've removed the multi channel kick, and the multi user kick
 * though, there are still remnants left ie..
 * "name = strtoken(&p, parv[1], ",");" in a normal kick
 * it will just be "KICK #channel nick"
 * A strchr() is going to be faster than a strtoken(), so rewritten
 * to use a strchr()
 *
 * It appears the original code was supposed to support 
 * "kick #channel1,#channel2 nick1,nick2,nick3." For example, look at
 * the original code for m_topic(), where 
 * "topic #channel1,#channel2,#channel3... topic" was supported.
 *
 * -Dianora
 */
int     m_kick(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Client *who;
  struct Channel *chptr;
  int   chasing = 0;
  char  *comment;
  char  *name;
  char  *p = (char *)NULL;
  char  *user;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KICK");
      return 0;
    }
  comment = (BadPtr(parv[3])) ? parv[0] : parv[3];
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

  *buf = '\0';
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  chptr = get_channel(sptr, name, !CREATE);
  if (!chptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
      return(0);
    }

  /* You either have chan op privs, or you don't -Dianora */
  /* orabidoo and I discussed this one for a while...
   * I hope he approves of this code, (he did) users can get quite confused...
   *    -Dianora
   */

  if (!IsServer(sptr) && !is_chan_op(sptr, chptr) && !HasUmode(sptr,UMODE_GOD))
    { 
      /* was a user, not a server, and user isn't seen as a chanop here */
      
      if(MyConnect(sptr))
        {
          /* user on _my_ server, with no chanops.. so go away */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      if(chptr->channelts == 0)
        {
          /* If its a TS 0 channel, do it the old way */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      /* Its a user doing a kick, but is not showing as chanop locally
       * its also not a user ON -my- server, and the channel has a TS.
       * There are two cases we can get to this point then...
       *
       *     1) connect burst is happening, and for some reason a legit
       *        op has sent a KICK, but the SJOIN hasn't happened yet or 
       *        been seen. (who knows.. due to lag...)
       *
       *     2) The channel is desynced. That can STILL happen with TS
       *        
       *     Now, the old code roger wrote, would allow the KICK to 
       *     go through. Thats quite legit, but lets weird things like
       *     KICKS by users who appear not to be chanopped happen,
       *     or even neater, they appear not to be on the channel.
       *     This fits every definition of a desync, doesn't it? ;-)
       *     So I will allow the KICK, otherwise, things are MUCH worse.
       *     But I will warn it as a possible desync.
       *
       *     -Dianora
       */

      /*          sendto_one(sptr, form_str(ERR_DESYNC),
       *           me.name, parv[0], chptr->chname);
       */

      /*
       * After more discussion with orabidoo...
       *
       * The code was sound, however, what happens if we have +h (TS4)
       * and some servers don't understand it yet? 
       * we will be seeing servers with users who appear to have
       * no chanops at all, merrily kicking users....
       * -Dianora
       */
    }

  if(chptr->mode.mode & MODE_NOCOLOR)
    comment = strip_colour(comment);

  p = strchr(parv[2],',');
  if(p)
    *p = '\0';
  user = parv[2]; /* strtoken(&p2, parv[2], ","); */

  if (!(who = find_chasing(sptr, user, &chasing)))
    {
      return(0);
    }

  if (IsMember(who, chptr))
    {
      /* Immune users */
      if( !(IsServer(sptr) || HasUmode(sptr,UMODE_GOD)) && HasUmode(who,UMODE_IMMUNE) )
        {
          sendto_one(sptr, form_str(ERR_USERISIMMUNE),
                     me.name, parv[0], user, name);
          return(0);
        }

#ifdef HIDE_OPS
      sendto_channel_chanops_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", parv[0],
                             name, who->name, comment);
      sendto_channel_non_chanops_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", NETWORK_NAME,
                             name, who->name, comment);

#else
      sendto_channel_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", parv[0],
                             name, who->name, comment);
#endif
      sendto_match_servs(chptr, cptr,
                         ":%s KICK %s %s :%s",
                         parv[0], name,
                         who->name, comment);
      remove_user_from_channel(who, chptr, 1);
    }
  else
    sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL),
               me.name, parv[0], user, name);

  return (0);
}

/* OK, this is just like KICK, but it sends out PARTs. For testing of this
 * idea, I want to see how well clients handle it
 *  -- asuffield
 */

int     m_remove(struct Client *cptr,
		 struct Client *sptr,
		 int parc,
		 char *parv[])
{
  struct Client *who;
  struct Channel *chptr;
  int   chasing = 0;
  char  *comment;
  char  *name;
  char  *p = (char *)NULL;
  char  *user;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "REMOVE");
      return 0;
    }

  *buf = '\0';
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  chptr = get_channel(sptr, name, !CREATE);
  if (!chptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
      return(0);
    }

  comment = reasonbuf;
  if (BadPtr(parv[3]))
    {
      ircsnprintf(comment, 1024, "requested by %s", parv[0]);
    }
  else
    {
      ircsnprintf(comment, 1024, "requested by %s: \"%s\"", parv[0],
		  (chptr->mode.mode & MODE_NOCOLOR) ? strip_colour(parv[3]) : parv[3]);
    }
/*   comment = (BadPtr(parv[3])) ? parv[0] : parv[3]; */
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

  /* You either have chan op privs, or you don't -Dianora */
  /* orabidoo and I discussed this one for a while...
   * I hope he approves of this code, (he did) users can get quite confused...
   *    -Dianora
   */

  if (!IsServer(sptr) && !is_chan_op(sptr, chptr) && !HasUmode(sptr,UMODE_GOD)) 
    { 
      /* was a user, not a server, and user isn't seen as a chanop here */
      
      if(MyConnect(sptr))
        {
          /* user on _my_ server, with no chanops.. so go away */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      if(chptr->channelts == 0)
        {
          /* If its a TS 0 channel, do it the old way */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      /* Its a user doing a kick, but is not showing as chanop locally
       * its also not a user ON -my- server, and the channel has a TS.
       * There are two cases we can get to this point then...
       *
       *     1) connect burst is happening, and for some reason a legit
       *        op has sent a KICK, but the SJOIN hasn't happened yet or 
       *        been seen. (who knows.. due to lag...)
       *
       *     2) The channel is desynced. That can STILL happen with TS
       *        
       *     Now, the old code roger wrote, would allow the KICK to 
       *     go through. Thats quite legit, but lets weird things like
       *     KICKS by users who appear not to be chanopped happen,
       *     or even neater, they appear not to be on the channel.
       *     This fits every definition of a desync, doesn't it? ;-)
       *     So I will allow the KICK, otherwise, things are MUCH worse.
       *     But I will warn it as a possible desync.
       *
       *     -Dianora
       */

      /*          sendto_one(sptr, form_str(ERR_DESYNC),
       *           me.name, parv[0], chptr->chname);
       */

      /*
       * After more discussion with orabidoo...
       *
       * The code was sound, however, what happens if we have +h (TS4)
       * and some servers don't understand it yet? 
       * we will be seeing servers with users who appear to have
       * no chanops at all, merrily kicking users....
       * -Dianora
       */
    }

  p = strchr(parv[2],',');
  if(p)
    *p = '\0';
  user = parv[2]; /* strtoken(&p2, parv[2], ","); */

  if (!(who = find_chasing(sptr, user, &chasing)))
    {
      return(0);
    }

  if (IsMember(who, chptr))
    {
      /* Immune users */
      if( !(IsServer(sptr) || HasUmode(sptr,UMODE_GOD)) && HasUmode(who,UMODE_IMMUNE))
        {
          sendto_one(sptr, form_str(ERR_USERISIMMUNE),
                     me.name, parv[0], user, name);
          return(0);
        }

#ifdef HIDE_OPS
      sendto_channel_chanops_butserv(chptr, who,
                             ":%s PART %s :%s", who->name,
                             name, comment);
      sendto_channel_non_chanops_butserv(chptr, who,
                             ":%s PART %s :%s", who->name,
                             name, comment);

#else
      sendto_channel_butserv(chptr, who,
                             ":%s PART %s :%s", who->name,
                             name, comment);
#endif
      sendto_match_servs(chptr, cptr,
                         ":%s REMOVE %s %s :%s",
                         sptr->name, chptr->chname,
			 who->name, BadPtr(parv[3]) ? "" : parv[3]);
      remove_user_from_channel(who, chptr, 1);
    }
  else
    sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL),
               me.name, parv[0], user, name);

  return (0);
}

int     count_channels(struct Client *sptr)
{
  struct Channel      *chptr;
  int   count = 0;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    count++;
  return (count);
}

/* m_knock
**    parv[0] = sender prefix
**    parv[1] = channel
**  The KNOCK command has the following syntax:
**   :<sender> KNOCK <channel>
**  If a user is not banned from the channel they can use the KNOCK
**  command to have the server NOTICE the channel operators notifying
**  they would like to join.  Helpful if the channel is invite-only, the
**  key is forgotten, or the channel is full (INVITE can bypass each one
**  of these conditions.  Concept by Dianora <db@db.net> and written by
**  <anonymous>
**
** Just some flood control added here, five minute delay between each
** KNOCK -Dianora
**/
int     m_knock(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Channel      *chptr;
  char  *p, *name;

  /* anti flooding code,
   * I did have this in parse.c with a table lookup
   * but I think this will be less inefficient doing it in each
   * function that absolutely needs it
   *
   * -Dianora
   */
  static time_t last_used=0L;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS), me.name, parv[0],
                 "KNOCK");
      return 0;
    }


  /* We will cut at the first comma reached, however we will not *
   * process anything afterwards.                                */

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  if (!IsChannelName(name) || !(chptr = hash_find_channel(name, NullChn)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
                 name);
      return 0;
    }

  if(!((chptr->mode.mode & MODE_INVITEONLY) ||
       (*chptr->mode.key) ||
       (chptr->mode.limit && chptr->users >= chptr->mode.limit )
       ))
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Channel is open!",
                 me.name,
                 sptr->name);
      return 0;
    }

  /* don't allow a knock if the user is banned, or the channel is secret */
  /*  or if the user is quieted on the channel  -- asuffield */
  if ((chptr->mode.mode & MODE_SECRET) || 
      (is_banned(sptr, chptr) & (CHFL_BAN | CHFL_QUIET)))
    {
      sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN), me.name, parv[0],
                 name);
      return 0;
    }

  /* if the user is already on channel, then a knock is pointless! */
  if (IsMember(sptr, chptr))
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- You are on channel already!",
                 me.name,
                 sptr->name);
      return 0;
    }

  /* flood control server wide, clients on KNOCK
   * opers are not flood controlled.
   *
   * sure they are, just not godopers -- asuffield
   */

  if(!HasUmode(sptr,UMODE_GOD))
    {
      if((last_used + PACE_WAIT) > CurrentTime)
        return 0;
      else
        last_used = CurrentTime;
    }

  /* flood control individual clients on KNOCK
   * the ugly possibility still exists, 400 clones could all KNOCK
   * on a channel at once, flooding all the ops. *ugh*
   * Remember when life was simpler?
   * -Dianora
   */

  /* opers are not flow controlled here */
  /* <ahem> *god*opers are not flow controlled -- asuffield */
  if(!HasUmode(sptr,UMODE_GOD) && (sptr->last_knock + KNOCK_DELAY) > CurrentTime)
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Wait %.1ld seconds before another knock",
                 me.name,
                 sptr->name,
                 KNOCK_DELAY - (CurrentTime - sptr->last_knock));
      return 0;
    }

  sptr->last_knock = CurrentTime;

  sendto_one(sptr,":%s NOTICE %s :*** Notice -- Your KNOCK has been delivered",
                 me.name,
                 sptr->name);

  /* using &me and me.name won't deliver to clients not on this server
   * so, the notice will have to appear from the "knocker" ick.
   *
   * Ideally, KNOCK would be routable. Also it would be nice to add
   * a new channel mode. Perhaps some day.
   * For now, clients that don't want to see KNOCK requests will have
   * to use client side filtering. 
   *
   * -Dianora
   */

  {
    char message[NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30];

    /* bit of paranoid, be a shame if it cored for this -Dianora */
    if(sptr->user)
      {
        ircsnprintf(message, NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30, "KNOCK: %s (%s [%s@%s] has asked for an invite)",
		    chptr->chname,
		    sptr->name,
		    sptr->username,
		    sptr->host);
        sendto_channel_type_notice(cptr, chptr, MODE_CHANOP, message);
      }
  }
  return 0;
}

/*
 * m_topic
 *      parv[0] = sender prefix
 *      parv[1] = topic text
 */
int     m_topic(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr = NullChn;
  char  *topic = (char *)NULL, *name, *p = (char *)NULL;
  
  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "TOPIC");
      return 0;
    }

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* multi channel topic's are now known to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  if (name && IsChannelName(name))
    {
      chptr = hash_find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (!IsMember(sptr, chptr) && !HasUmode(sptr,UMODE_USER_AUSPEX) && SecretChannel(chptr))
        {
          sendto_one(sptr, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (parc > 2) /* setting topic */
        topic = parv[2];

      if(topic) /* a little extra paranoia never hurt */
        {
          if (can_send(sptr, chptr) != 0)
            {
              sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
                         me.name, parv[0], sptr->name);
              return 0;
            }
          else if ((chptr->mode.mode & MODE_TOPICLIMIT) == 0 ||
               is_chan_op(sptr, chptr) || HasUmode(sptr,UMODE_GOD))
            {
              /* setting a topic */
              /*
               * chptr zeroed
               */
              strncpy_irc(chptr->topic, topic, TOPICLEN + 1);
#ifdef STRIP_MISC
	      strip_colour(chptr->topic);
#endif
#ifdef TOPIC_INFO
              /*
               * XXX - this truncates the topic_nick if
               * strlen(sptr->name) > NICKLEN
               */
              strncpy_irc(chptr->topic_nick, sptr->name, NICKLEN + 1);
              chptr->topic_time = CurrentTime;
#endif
              sendto_match_servs(chptr, cptr, ":%s STOPIC %s %s %.1ld %.1ld :%s",
				 sptr->name, chptr->chname, sptr->name,
				 chptr->topic_time, chptr->channelts,
				 chptr->topic);
              sendto_channel_butserv(chptr, sptr, ":%s TOPIC %s :%s",
				     IsServer(sptr) ? NETWORK_NAME : sptr->name, chptr->chname,
				     chptr->topic);
            }
          else
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], chptr->chname);
        }
      else  /* only asking for topic  */
        {
          if (chptr->topic[0] == '\0')
            sendto_one(sptr, form_str(RPL_NOTOPIC),
                       me.name, parv[0], chptr->chname);
          else
            {
              sendto_one(sptr, form_str(RPL_TOPIC),
                         me.name, parv[0],
                         chptr->chname, chptr->topic);
#ifdef TOPIC_INFO
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], chptr->chname,
                         chptr->topic_nick,
                         chptr->topic_time);
#endif
            }
        }
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
    }

  return 0;
}

/*
** m_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
int     m_invite(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  struct Client *acptr;
  struct Channel *chptr;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "INVITE");
      return -1;
    }

  /* A little sanity test here */
  if(!sptr->user)
    return 0;

  if (!(acptr = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  if (!check_channel_name(parv[2]))
    { 
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
                 me.name, parv[0], (unsigned char *)parv[2]);
      return 0;
    }

  if (!IsChannelName(parv[2]))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  if (!(chptr = hash_find_channel(parv[2], NullChn)))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  /* By this point, chptr is non NULL */  

  if (!IsMember(sptr, chptr) && !HasUmode(sptr,UMODE_GOD))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  if (IsMember(acptr, chptr))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_USERONCHANNEL),
                   me.name, parv[0], parv[1], parv[2]);
      return 0;
    }


  if (!is_chan_op(sptr, chptr) && !HasUmode(sptr,UMODE_GOD) && !(chptr->mode.mode & MODE_FREEINVITE))
    {
      if (MyClient(sptr))
	sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
		   me.name, parv[0], parv[2]);
      return -1;
    }

   if (HasUmode(sptr,UMODE_NOINVITE))
     {
	if (MyClient(sptr))
	  sendto_one(sptr, form_str(ERR_SOURCENINVITE),
		     me.name, parv[0]);
	return 0;
     }
   
   if (HasUmode(acptr,UMODE_NOINVITE))
     {
	if (MyClient(sptr))
	  sendto_one(sptr, form_str(ERR_TARGETNINVITE),
		     me.name, parv[0]);
	return 0;
     }
   
  /*
   * due to some whining I've taken out the need for the channel
   * being +i before sending an INVITE. It was intentionally done this
   * way, it makes no sense (to me at least) letting the server send
   * an unnecessary invite when a channel isn't +i !
   * bah. I can't be bothered arguing it
   * -Dianora
   */
  if (MyConnect(sptr))
    {
      sendto_one(sptr, form_str(RPL_INVITING), me.name, parv[0],
                 acptr->name, ((chptr) ? (chptr->chname) : parv[2]));
      if (acptr->user->away)
        sendto_one(sptr, form_str(RPL_AWAY), me.name, parv[0],
                   acptr->name, acptr->user->away);
    }

  if(MyConnect(acptr) && (chptr->mode.mode & MODE_INVITEONLY))
    add_invite(acptr, chptr);

  sendto_prefix_one(acptr, sptr, ":%s INVITE %s :%s",
                    parv[0], acptr->name, parv[2]);
  return 0;
}


/************************************************************************
 * m_names() - Added by Jto 27 Apr 1989
 ************************************************************************/

/*
** m_names
**      parv[0] = sender prefix
**      parv[1] = channel
*/
/*
 * Modified to report possible names abuse
 * drastically modified to not show all names, just names
 * on given channel names.
 *
 * -Dianora
 */
/* maximum names para to show to opers when abuse occurs */
#define TRUNCATED_NAMES 20

int m_names(struct Client *cptr,
	    struct Client *sptr,
	    int parc,
	    char *parv[])
{ 
  struct Channel *chptr;
  struct Client *c2ptr;
  Link  *lp;
  struct Channel *ch2ptr = NULL;
  int   idx, flag = 0, len, mlen;
  char  *s, *para = parc > 1 ? parv[1] : NULL;
  int comma_count=0;
  int char_count=0;

  /* And throw away non local names requests that do get here -Dianora */
  if(!MyConnect(sptr))
    return 0;

  /*
   * names is called by m_join() when client joins a channel,
   * hence I cannot easily rate limit it.. perhaps that won't
   * be necessary now that remote names is prohibited.
   *
   * -Dianora
   */

  mlen = strlen(me.name) + NICKLEN + 7;

  if (!BadPtr(para))
    {
      /* Here is the lamer detection code
       * P.S. meta, GROW UP
       * -Dianora 
       */
      for(s = para; *s; s++)
        {
          char_count++;
          if(*s == ',')
            comma_count++;
          if(comma_count > 1)
            {
              if(char_count > TRUNCATED_NAMES)
                para[TRUNCATED_NAMES] = '\0';
              else
                {
                  s++;
                  *s = '\0';
                }
              sendto_one(sptr, form_str(ERR_TOOMANYTARGETS),
                         me.name, sptr->name, "NAMES",1);
              return 0;
            }
        }

      s = strchr(para, ',');
      if (s)
        *s = '\0';
      if (!check_channel_name(para))
        { 
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                     me.name, parv[0], (unsigned char *)para);
          return 0;
        }

      ch2ptr = hash_find_channel(para, NULL);
    }

  *buf = '\0';
  
  /* 
   *
   * First, do all visible channels (public and the one user self is)
   */

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      if ((chptr != ch2ptr) && !BadPtr(para))
        continue; /* -- wanted a specific channel */
      if (!MyConnect(sptr) && BadPtr(para))
        continue;
      if (!ShowChannel(sptr, chptr))
        continue; /* -- users on this are not listed */
      
      /* Find users on same channel (defined by chptr) */

      strncpy_irc(buf, "* ", BUFSIZE);
      len = strlen(chptr->chname);
      strncpy_irc(buf + 2, chptr->chname, BUFSIZE - 2);
      strncpy_irc(buf + 2 + len, " :", BUFSIZE - 2 - len);

      if (PubChannel(chptr))
        *buf = '=';
      else if (SecretChannel(chptr))
        *buf = '@';
      idx = len + 4;
      flag = 1;
      for (lp = chptr->members; lp; lp = lp->next)
        {
          c2ptr = lp->value.cptr;
          if (IsInvisible(c2ptr) && !HasUmode(sptr,UMODE_USER_AUSPEX) && !IsMember(sptr,chptr))
            continue;
#ifdef HIDE_OPS
	  if(is_chan_op(sptr,chptr))
#endif
	    {
	      if (lp->flags & CHFL_CHANOP)
		{
		  strcat(buf, "@");
		  idx++;
		}
	      else if (lp->flags & CHFL_VOICE)
		{
		  strcat(buf, "+");
		  idx++;
		}
	    }
          strncat(buf, c2ptr->name, NICKLEN);
          idx += strlen(c2ptr->name) + 1;
          flag = 1;
          strcat(buf," ");
          if (mlen + idx + NICKLEN > BUFSIZE - 3)
            {
              sendto_one(sptr, form_str(RPL_NAMREPLY),
                         me.name, parv[0], buf);
              strncpy_irc(buf, "* ", 3);
              strncpy_irc(buf + 2, chptr->chname, len + 1);
              strcat(buf, " :");
              if (PubChannel(chptr))
                *buf = '=';
              else if (SecretChannel(chptr))
                *buf = '@';
              idx = len + 4;
              flag = 0;
            }
        }
      if (flag)
        sendto_one(sptr, form_str(RPL_NAMREPLY),
                   me.name, parv[0], buf);
    }
  if (!BadPtr(para))
    {
      sendto_one(sptr, form_str(RPL_ENDOFNAMES), me.name, parv[0],
                 para);
      return(1);
    }

  /* Second, do all non-public, non-secret channels in one big sweep */
  /* This block is for showing users who aren't in a channel or aren't
  ** in any channel sptr can see.
  */

  strncpy_irc(buf, "* * :", BUFSIZE);
  idx = 5;
  flag = 0;
  for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next)
    {
      struct Channel *ch3ptr;
      int       showflag = 0, secret = 0;

      if (!IsPerson(c2ptr) || IsInvisible(c2ptr))
        continue;
      lp = c2ptr->user->channel;
      /*
       * dont show a client if they are on a secret channel or
       * they are on a channel sptr is on since they have already
       * been show earlier. -avalon
       */
      while (lp)
        {
          ch3ptr = lp->value.chptr;
          if (PubChannel(ch3ptr) || HasUmode(sptr,UMODE_USER_AUSPEX) || IsMember(sptr, ch3ptr))
            showflag = 1;
          if (SecretChannel(ch3ptr) && !HasUmode(sptr,UMODE_USER_AUSPEX))
            secret = 1;
          lp = lp->next;
        }
      if (showflag) /* have we already shown them ? */
        continue;
      if (secret) /* on any secret channels ? */
        continue;
      (void)strncat(buf, c2ptr->name, NICKLEN);
      idx += strlen(c2ptr->name) + 1;
      (void)strcat(buf," ");
      flag = 1;
      if (mlen + idx + NICKLEN > BUFSIZE - 3)
        {
          sendto_one(sptr, form_str(RPL_NAMREPLY),
                     me.name, parv[0], buf);
          strncpy_irc(buf, "* * :", 6);
          idx = 5;
          flag = 0;
        }
    }

  if (flag)
    sendto_one(sptr, form_str(RPL_NAMREPLY), me.name, parv[0], buf);

  sendto_one(sptr, form_str(RPL_ENDOFNAMES), me.name, parv[0], "*");
  return(1);
}

static  void sjoin_sendit(struct Client *from,
                          struct Client *sptr,
                          struct Channel *chptr)
{
#ifdef SERVERHIDE
  sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", NETWORK_NAME,
                         chptr->chname, modebuf, parabuf);
#else
  sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", sptr->name,
                         chptr->chname, modebuf, parabuf);
#endif
}

/*
 * m_sjoin
 * parv[0] - sender
 * parv[1] - TS
 * parv[2] - channel
 * parv[3] - modes + n arguments (key and/or limit)
 * parv[4+n] - flags+nick list (all in one parameter)
 * 
 * process a SJOIN, taking the TS's into account to either ignore the
 * incoming modes or undo the existing ones or merge them, and JOIN
 * all the specified users while sending JOIN/MODEs to non-TS servers
 * and to clients
 */


int     m_sjoin(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr;
  struct Client       *acptr;
  time_t        newts;
  time_t        oldts;
  time_t        tstosend;
  static        Mode mode, *oldmode;
  Link  *l;
  int   args = 0, keep_our_modes = 1, keep_new_modes = 1;
  int   what = 0, pargs = 0, fl = 0, people = 0, isnew;
  /* loop unrolled this is now redundant */
  /*  int ip; */
  register      char *s, *s0;
  static        char numeric[16], sjbuf[BUFSIZE];
  char  *mbuf = modebuf, *t = sjbuf, *p;

  /* wipe sjbuf so we don't use old nicks if we get an empty SJOIN */
  *sjbuf = '\0';

  if (IsClient(sptr) || parc < 5)
    return 0;
  if (!IsChannelName(parv[2]))
    return 0;

  if (!check_channel_name(parv[2]))
     { 
       return 0;
     }

  newts = atol(parv[1]);
  memset(&mode, 0, sizeof(mode));

  s = parv[3];
  while (*s)
    switch(*(s++))
      {
      case 'i':
        mode.mode |= MODE_INVITEONLY;
        break;
#ifdef JUPE_CHANNEL
      case 'j':
        mode.mode |= MODE_JUPED;
        break;
#endif
      case 'L':
        mode.mode |= MODE_LARGEBANLIST;
        break;
      case 'n':
        mode.mode |= MODE_NOPRIVMSGS;
        break;
      case 's':
        mode.mode |= MODE_SECRET;
        break;
      case 'm':
        mode.mode |= MODE_MODERATED;
        break;
      case 't':
        mode.mode |= MODE_TOPICLIMIT;
        break;
      case 'c':
	mode.mode |= MODE_NOCOLOR;
	break;
      case 'z':
	mode.mode |= MODE_OPMODERATE;
	break;
      case 'g':
	mode.mode |= MODE_FREEINVITE;
	break;
      case 'P':
	mode.mode |= MODE_PERM;
	break;
      case 'Q':
	mode.mode |= MODE_NOFORWARD;
	break;
      case 'r':
	mode.mode |= MODE_NOUNIDENT;
	break;
      case 'R':
	mode.mode |= MODE_QUIETUNIDENT;
	break;
      case 'k':
        strncpy_irc(mode.key, parv[4 + args], KEYLEN + 1);
        args++;
        if (parc < 5+args) return 0;
        break;
      case 'l':
        mode.limit = atoi(parv[4+args]);
        args++;
        if (parc < 5+args) return 0;
        break;
#ifdef INVITE_CHANNEL_FORWARDING
      case 'f':
	mode.mode |= MODE_FORWARD;
	strncpy_irc(mode.invite_forward_channel_name, parv[4+args], CHANNELLEN + 1);
	args++;
	if (parc < 5+args) return 0;
	break;
#endif
      case 'J':
        {
          char *freq = parv[4+args];
          char *limit = strchr(freq, ',');
          /* Invalid -> bad, throw it away */
          if (!limit)
            {
              sendto_ops_flag(UMODE_SERVNOTICE, "Invalid SJOIN from %s for %s (bad +J string '%s')",
                              sptr->name, parv[2], freq);
              return 0;
            }
          *limit++ = '\0';
          mode.join_throttle_frequency = strtoul(freq, NULL, 0);
          mode.join_throttle_limit = strtoul(limit, NULL, 0);
          args++;
          if (parc < 5+args) return 0;
          break;
        }
      case 'D':
        {
          char *freq = parv[4+args];
          char *limit = strchr(freq, ',');
          /* Invalid -> bad, throw it away */
          if (!limit)
            {
              sendto_ops_flag(UMODE_SERVNOTICE, "Invalid SJOIN from %s for %s (bad +D string '%s')",
                              sptr->name, parv[2], freq);
              return 0;
            }
          *limit++ = '\0';
          mode.autodline_frequency = strtoul(freq, NULL, 0);
          mode.autodline_limit = strtoul(limit, NULL, 0);
          args++;
          if (parc < 5+args) return 0;
          break;
        }
      }

  *parabuf = '\0';

  isnew = ChannelExists(parv[2]) ? 0 : 1;
  /* The channel did not exist. Create it. */
  if (!(chptr = get_channel(sptr, parv[2], CREATE)))
    {
      /* Hmm, not sure what to do here */
      /* For now, let's ignore it */
      return 0;
    }

  oldts = chptr->channelts;

  /* If the TS goes to 0 for whatever reason, flag it
   * ya, I know its an invasion of privacy for those channels that
   * want to keep TS 0 *shrug* sorry
   * -Dianora
   */

  if(!isnew && !newts && oldts)
    {
      sendto_channel_butserv(chptr, &me,
			     ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to 0",
			     me.name, chptr->chname, chptr->chname, (long unsigned)oldts);
      sendto_ops_flag(UMODE_SERVNOTICE, "Server %s changing TS on %s from %lu to 0",
		      sptr->name, parv[2], (long unsigned)oldts);
    }

  oldmode = &chptr->mode;

  if (isnew)
    chptr->channelts = tstosend = newts;
  else if (newts == 0 || oldts == 0)
    chptr->channelts = tstosend = 0;
  else if (newts == oldts)
    tstosend = oldts;
  else if (newts < oldts)
    {
      keep_our_modes = NO;

      chptr->channelts = tstosend = newts;
    }
  else
    {
      keep_new_modes = NO;

      mode = *oldmode;

      tstosend = oldts;
    }

  if (keep_new_modes && keep_our_modes)
    {
      /* ie, timestamps were the same */
      mode.mode |= oldmode->mode;
      if (oldmode->limit > mode.limit)
        mode.limit = oldmode->limit;
      if (strcmp(mode.key, oldmode->key) < 0)
        strncpy_irc(mode.key, oldmode->key, KEYLEN + 1);
#ifdef INVITE_CHANNEL_FORWARDING
      if (strcmp(mode.invite_forward_channel_name, oldmode->invite_forward_channel_name) < 0)
 	strncpy_irc(mode.invite_forward_channel_name, oldmode->invite_forward_channel_name, CHANNELLEN + 1);
#endif
      if (oldmode->join_throttle_frequency > mode.join_throttle_frequency)
        mode.join_throttle_frequency = oldmode->join_throttle_frequency;
      if (oldmode->join_throttle_limit > mode.join_throttle_limit)
        mode.join_throttle_limit = oldmode->join_throttle_limit;
      if (oldmode->autodline_frequency > mode.autodline_frequency)
        mode.autodline_frequency = oldmode->autodline_frequency;
      if (oldmode->autodline_limit > mode.autodline_limit)
        mode.autodline_limit = oldmode->autodline_limit;
    }

  /* This loop unrolled below for speed
   */
  /*
  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & mode.mode) && !(flags[ip].mode & oldmode->mode))
      {
        if (what != 1)
          {
            *mbuf++ = '+';
            what = 1;
          }
        *mbuf++ = flags[ip].letter;
      }
      */

  if((MODE_SECRET     & mode.mode) && !(MODE_SECRET     & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 's';
    }
  if((MODE_MODERATED  & mode.mode) && !(MODE_MODERATED  & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & mode.mode) && !(MODE_NOPRIVMSGS & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'n';
    }
#ifdef JUPE_CHANNEL
  if((MODE_JUPED & mode.mode) && !(MODE_JUPED & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'j';
    }
#endif
  if((MODE_LARGEBANLIST & mode.mode) && !(MODE_LARGEBANLIST & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'L';
    }
  if((MODE_TOPICLIMIT & mode.mode) && !(MODE_TOPICLIMIT & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 't';
    }
  if((MODE_NOCOLOR & mode.mode) && !(MODE_NOCOLOR & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'c';
    }
  if((MODE_FREEINVITE & mode.mode) && !(MODE_FREEINVITE & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'g';
    }
  if((MODE_PERM & mode.mode) && !(MODE_PERM & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'P';
    }
  if((MODE_NOFORWARD & mode.mode) && !(MODE_NOFORWARD & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'Q';
    }
  if((MODE_NOUNIDENT & mode.mode) && !(MODE_NOUNIDENT & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'r';
    }
  if((MODE_QUIETUNIDENT & mode.mode) && !(MODE_QUIETUNIDENT & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'R';
    }
  if((MODE_INVITEONLY & mode.mode) && !(MODE_INVITEONLY & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;             /* This one is actually redundant now */
        }
      *mbuf++ = 'i';
    }
  if((MODE_OPMODERATE & mode.mode) && !(MODE_OPMODERATE & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'z';
    }

  /* This loop unrolled below for speed
   */
  /*
  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & oldmode->mode) && !(flags[ip].mode & mode.mode))
      {
        if (what != -1)
          {
            *mbuf++ = '-';
            what = -1;
          }
        *mbuf++ = flags[ip].letter;
      }
      */
  if((MODE_SECRET     & oldmode->mode) && !(MODE_SECRET     & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 's';
    }
  if((MODE_MODERATED  & oldmode->mode) && !(MODE_MODERATED  & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & oldmode->mode) && !(MODE_NOPRIVMSGS & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'n';
    }
#ifdef JUPE_CHANNEL
  if((MODE_JUPED & oldmode->mode) && !(MODE_JUPED & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'j';
    }
#endif
  if((MODE_LARGEBANLIST & oldmode->mode) && !(MODE_LARGEBANLIST & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'L';
    }
  if((MODE_TOPICLIMIT & oldmode->mode) && !(MODE_TOPICLIMIT & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 't';
    }
  if((MODE_NOCOLOR & oldmode->mode) && !(MODE_NOCOLOR & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'c';
    }
  if((MODE_FREEINVITE & oldmode->mode) && !(MODE_FREEINVITE & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'g';
    }
  if((MODE_PERM & oldmode->mode) && !(MODE_PERM & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'P';
    }
  if((MODE_NOFORWARD & oldmode->mode) && !(MODE_NOFORWARD & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'Q';
    }
  if((MODE_NOUNIDENT & oldmode->mode) && !(MODE_NOUNIDENT & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'r';
    } 
  if((MODE_QUIETUNIDENT & oldmode->mode) && !(MODE_QUIETUNIDENT & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'R';
    } 
  if((MODE_INVITEONLY & oldmode->mode) && !(MODE_INVITEONLY & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'i';
    }
  if((MODE_OPMODERATE & oldmode->mode) && !(MODE_OPMODERATE & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'z';
    }
  if (oldmode->limit && !mode.limit)
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'l';
    }
  if (oldmode->key[0] && !mode.key[0])
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, oldmode->key);
      strcat(parabuf, " ");
      pargs++;
    }
  if (oldmode->join_throttle_frequency && !mode.join_throttle_frequency)
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'J';
    }
  if (oldmode->autodline_frequency && !mode.autodline_frequency)
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'D';
    }
#ifdef INVITE_CHANNEL_FORWARDING
  if (oldmode->invite_forward_channel_name[0] && !mode.invite_forward_channel_name[0])
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'f';
    }
#endif
  if (mode.limit && oldmode->limit != mode.limit)
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'l';
      ircsnprintf(numeric, sizeof(numeric), "%d", mode.limit);
      if ((s = strchr(numeric, ' ')))
        *s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.key[0] && strcmp(oldmode->key, mode.key))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, mode.key);
      strcat(parabuf, " ");
      pargs++;
    }
#ifdef INVITE_CHANNEL_FORWARDING
  if (mode.invite_forward_channel_name[0] && strcmp(oldmode->invite_forward_channel_name, mode.invite_forward_channel_name))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'f';
      strcat(parabuf, mode.invite_forward_channel_name);
      strcat(parabuf, " ");
      pargs++;
    }
#endif
  if (mode.join_throttle_frequency && (mode.join_throttle_frequency != oldmode->join_throttle_frequency))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'J';
      ircsnprintf(numeric, sizeof(numeric), "%d,%d", mode.join_throttle_frequency, mode.join_throttle_limit);
      if ((s = strchr(numeric, ' ')))
        *s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.autodline_frequency && (mode.autodline_frequency != oldmode->autodline_frequency))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'D';
      ircsnprintf(numeric, sizeof(numeric), "%d,%d", mode.autodline_frequency, mode.autodline_limit);
      if ((s = strchr(numeric, ' ')))
        *s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  
  chptr->mode = mode;

  if (!keep_our_modes)
    {
      what = 0;
      for (l = chptr->members; l && l->value.cptr; l = l->next)
        {
          if (l->flags & MODE_CHANOP)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'o';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
                  sjoin_sendit(cptr, sptr, chptr);
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_CHANOP;
            }
          if (l->flags & MODE_VOICE)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'v';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
                  sjoin_sendit(cptr, sptr, chptr);
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_VOICE;
            }
        }
        sendto_channel_butserv(chptr, &me,
            ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to %lu",
            me.name, chptr->chname, chptr->chname, (long unsigned)oldts, (long unsigned)newts);
    }
  if (mbuf != modebuf)
    {
      *mbuf = '\0';
      sjoin_sendit(cptr, sptr, chptr);
    }

  /* *Before* we increment the count for new users, first decrement it
   * based on the time since last join, to avoid rounding errors later
   */
  if (chptr->mode.join_throttle_frequency)
    {
      u_int32_t reduction = (CurrentTime - chptr->last_join_time) / chptr->mode.join_throttle_frequency;
      if (reduction > chptr->join_throttle_count)
        {
          /* Clip at the bottom end - the effective count should never drop below zero */
          chptr->join_throttle_count = 0;
          chptr->last_join_time = CurrentTime;
        }
      else
        {
          /* Smoothly decrement count, and keep the remained in last_join_time so we don't get rounding errors */
          chptr->join_throttle_count -= reduction;
          chptr->last_join_time = CurrentTime - ((CurrentTime - chptr->last_join_time) % chptr->mode.join_throttle_frequency);
        }
    }

  if (chptr->mode.autodline_frequency)
    {
      u_int32_t reduction = (CurrentTime - chptr->last_autodline_time) / chptr->mode.autodline_frequency;
      if (reduction > chptr->autodline_count)
        {
          chptr->autodline_count = 0;
          chptr->last_autodline_time = CurrentTime;
        }
      else
        {
          chptr->autodline_count -= reduction;
          chptr->last_autodline_time = CurrentTime - ((CurrentTime - chptr->last_autodline_time) % chptr->mode.autodline_frequency);
        }
    }

  *modebuf = *parabuf = '\0';
  if (parv[3][0] != '0' && keep_new_modes)
    /* XXX This may make the SJOIN line longer than 512 chars!
     * Example: A --- B --- <Rest of net>
     * <Rest of net> has enough nicks in the channel to fill 512 chars
     * B splits off the net; while it is split an op on A or B
     * sets a mode with parameters. When the net rejoins, the SJOIN
     * from B to A will be truncated, losing nicks.
     * There is no time to fix this before 1.0 but it's documented now.
     * -- jilles */
    channel_modes(sptr, modebuf, parabuf, chptr);
  else
    {
      modebuf[0] = '0';
      modebuf[1] = '\0';
    }

  ircsnprintf(t, BUFSIZE, ":%s SJOIN %lu %s %s %s :", sptr->name, (long unsigned)tstosend, parv[2],
	      modebuf, parabuf);
  t += strlen(t);

  mbuf = modebuf;
  parabuf[0] = '\0';
  pargs = 0;
  *mbuf++ = '+';

  for (s = s0 = strtoken(&p, parv[args+4], " "); s;
       s = s0 = strtoken(&p, (char *)NULL, " "))
    {
      fl = 0;
      if (*s == '@' || s[1] == '@')
        fl |= MODE_CHANOP;
      if (*s == '+' || s[1] == '+')
        fl |= MODE_VOICE;
      if (!keep_new_modes)
       {
        if (fl & MODE_CHANOP)
          {
            fl = MODE_DEOPPED;
          }
        else
          {
            fl = 0;
          }
       }
      while (*s == '@' || *s == '+')
        s++;
      if (!(acptr = find_chasing(sptr, s, NULL)))
        continue;
      if (acptr->from != cptr)
        continue;
      people++;
      if (!IsMember(acptr, chptr))
        {
          add_user_to_channel(chptr, acptr, fl);
          sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s",
                                 s, parv[2]);
        }
      if (keep_new_modes)
        strcpy(t, s0);
      else
        strcpy(t, s);
      t += strlen(t);
      *t++ = ' ';
      if (fl & MODE_CHANOP)
        {
          *mbuf++ = 'o';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
              sjoin_sendit(cptr, sptr, chptr);
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
      if (fl & MODE_VOICE)
        {
          *mbuf++ = 'v';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
              sjoin_sendit(cptr, sptr, chptr);
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
    }
  
  *mbuf = '\0';
  if (pargs)
  sjoin_sendit(cptr, sptr, chptr);
  if (t[-1] == ' ')
    t[-1] = '\0';
  else
    *t = '\0';
  /* Remove the channel again if it is empty and not persistent
   * This takes care of the case where all joining users were
   * invalid or killed -- jilles */
  if (chptr->users == 0)
    {
      chptr->users++;
      sub1_from_channel(chptr);
      chptr = hash_find_channel(parv[2], NULL);
      if (chptr == NULL)
	{
	  if (!isnew)
	    /* +P/+j lost to TS, send it on (channel mask isn't used
	     * anyway) XXX this desyncs but it's not important enough */
	    sendto_match_servs(NULL, cptr, "%s", sjbuf);
	  return 0;
	}
    }
  sendto_match_servs(chptr, cptr, "%s", sjbuf);

  /* OK, let's try this here. If this channel is juped, and we have people
   * in it, kick 'em. This prevents people from ducking network-wide jupes
   * on split servers
   *
   * We do this on every server and only kick the local clients. This cuts
   * down on network traffic during a burst.
   *
   *  -- asuffield
   */
  /* Only do this if the TS changed, i.e. the channel was recreated.
   * Let's avoid the possibility of +j kicking everyone in an existing
   * channel. -- jilles */
#ifdef JUPE_CHANNEL
  {
    Link* lp;
    static char reason[] = "Juped channel";
    char* aparv[] = {NULL, NULL, NULL, reason};
    aparv[0] = me.name;
    aparv[1] = chptr->chname;

    if (chptr->mode.mode & MODE_JUPED && !keep_our_modes)
      for (lp = chptr->members; lp; lp = lp->next)
	{
	  acptr = lp->value.cptr;
	  if (MyClient(acptr) && !HasUmode(acptr,UMODE_IMMUNE) &&
			  !HasUmode(acptr,UMODE_GOD))
	    {
	      aparv[2] = acptr->name;
	      m_kick(&me, &me, 4, aparv);
	    }
	}
  }
#endif

  /* Increment join throttle if a single peon joined.
   * This can be a normal join or a netjoin with a single peon, oh well.
   * Anyway, this prevents the channel being closed for minutes after
   * netjoins. Server restarts/crashes are still annoying; that is harder
   * to fix. -- jilles */
  if (chptr->mode.join_throttle_frequency)
    {
      if (people == 1 && fl == 0)
        chptr->join_throttle_count++;
    }
  else
    {
      chptr->join_throttle_count = 0;
      chptr->last_join_time = CurrentTime;
    }

  if (chptr->mode.autodline_frequency)
    {
      if (people == 1 && fl == 0)
        chptr->autodline_count++;
      if (chptr->autodline_count > chptr->mode.autodline_limit)
        {
          /* That pushed us over the limit, so clear it out */
          autodline_clear_channel(chptr);
          chptr->autodline_count = chptr->mode.autodline_limit;
        }
    }
  else
    {
      chptr->autodline_count = 0;
      chptr->last_autodline_time = CurrentTime;
    }

  return 0;
}


#if defined(INVITE_CHANNEL_FORWARDING) || defined(BAN_CHANNEL_FORWARDING)
/*
** m_join_redirect called recursively from m_join 
* -goober
*/
int m_join_redirect(struct Client *cptr,
                    struct Client *sptr,
                    char *from_channel,
                    char *key,
                    struct Channel* to_chptr)
{
  sendto_one(sptr, form_str(ERR_LINKCHANNEL), me.name, sptr->name, from_channel, to_chptr->chname);

  if (ChannelExists(to_chptr->chname))
    {
      char chname[CHANNELLEN + 1];
      strncpy_irc(chname, to_chptr->chname, CHANNELLEN + 1);
      do_join(cptr, sptr, chname, key);
      return 1;
    }
  else
    return 0;
}
#endif

#ifdef BAN_CHANNEL_FORWARDING
char *banstr_remove_channel(char *banstr)
{
  char *pos;
  if ((pos=strrchr(banstr,'!'))!=strchr(banstr,'!')) *pos='\0';
  return banstr;
}

char *banstr_get_channel(const char *banstr)
{
  char *pos;
  if ((pos=strrchr(banstr,'!'))!=strchr(banstr,'!')) 
    return pos+1;
  else
    return 0;
}

struct Channel *get_ban_channel_forward(struct Client *cptr,struct Channel *chptr)
{
  register Link *tmp;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return (0);

  strncpy_irc(s, make_nick_user_host(cptr->name, cptr->username, cptr->host),
	      NICKLEN + USERLEN + HOSTLEN + 6);
  s2 = make_nick_user_host(cptr->name, cptr->username,
                           inetntoa((char*) &cptr->ip));

  for (tmp = chptr->banlist; tmp; tmp = tmp->next)
    if (match(BANSTR(tmp), s) ||
        match(BANSTR(tmp), s2))
      break;

  if (tmp && tmp->value.banptr->ban_forward_chname) 
    return hash_find_channel(tmp->value.banptr->ban_forward_chname,NULL);
  else
    return 0;
}
#else
struct Channel *get_ban_channel_forward(struct Client *cptr,struct Channel *chptr)
{
  return 0;
}
#endif

#if defined(INVITE_CHANNEL_FORWARDING) || defined(BAN_CHANNEL_FORWARDING)
void attempt_channel_redirection(struct Client *cptr,
                                 struct Client *sptr,
                                 char *key,
                                 int err_flag,
                                 char *name,
                                 struct Channel *chptr,
                                 int redir_type)
{
  if (!HasUmode(sptr, UMODE_NOFORWARD))
    {
      static int recurse_level=0;
      struct Channel *fchptr;
  
      if (redir_type == INVITE_FORWARD) 
#ifdef INVITE_CHANNEL_FORWARDING
        fchptr = hash_find_channel(chptr->mode.invite_forward_channel_name, NULL);
#else
        return;
#endif
      else
        fchptr = get_ban_channel_forward(cptr,chptr);
  
      if (fchptr && !(fchptr->mode.mode & MODE_NOFORWARD))
        {
          if (recurse_level<MAX_FORWARDING_RECURSION)
            {
              recurse_level++;
              if (!m_join_redirect(cptr, sptr, chptr->chname, key, fchptr))
                sendto_one(sptr, form_str(err_flag), me.name, sptr->name, name);
              recurse_level=0;
            }
          else
            {
              sendto_one(sptr, form_str(ERR_MAXFORWARDING), me.name, sptr->name);
              sendto_one(sptr, form_str(err_flag), me.name, sptr->name, name);
            }
          return;
        }
    }

  sendto_one(sptr, form_str(err_flag), me.name, sptr->name, name);
}
#endif

