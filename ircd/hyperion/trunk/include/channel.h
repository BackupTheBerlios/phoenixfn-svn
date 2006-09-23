/************************************************************************
 *
 *   IRC - Internet Relay Chat, include/channel.h
 *   Copyright (C) 1990 Jarkko Oikarinen
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

#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h
#ifndef INCLUDED_config_h
#include "config.h"           /* config settings */
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"        /* buffer sizes */
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>        /* time_t */
#define INCLUDED_sys_types_h
#endif
#include "umodes.h"

struct SLink;
struct Client;


/* mode structure for channels */

struct Mode
{
  unsigned int  mode;
  int   limit;
  char  key[KEYLEN + 1];
  u_int32_t join_throttle_frequency, join_throttle_limit;
  u_int32_t autodline_frequency, autodline_limit;
#ifdef INVITE_CHANNEL_FORWARDING
  /* I need this to sync on SJOIN
   * (well, the way I'm doing it, anyway)
   *  -- asuffield
   */
  char  invite_forward_channel_name[CHANNELLEN + 1];
#endif
};

/* channel structure */

struct Channel
{
  struct Channel* nextch;
  struct Channel* prevch;
  struct Channel* hnextch;
  struct Mode     mode;
  time_t          last_join_time;
  u_int32_t       join_throttle_count;
  /* These mirror last_join_time and last_throttle_count */
  time_t          last_autodline_time;
  u_int32_t       autodline_count;
  char            topic[TOPICLEN + 1];
#ifdef TOPIC_INFO
  char            topic_nick[NICKLEN + 1];
  time_t          topic_time;
#endif
  int             users/*, logcount*/;
  struct SLink*   members;
/*   struct SLink*   loggers; */
  struct SLink*   invites;
  struct SLink*   banlist;
  struct SLink*   exceptlist;
  struct SLink*   denylist;
  struct SLink*   invexlist;
  struct SLink*   quietlist;
  int             num_bed;  /* number of bans+exceptions+denies */
  time_t          channelts;
#ifdef FLUD
  time_t          fludblock;
  struct fludbot* fluders;
  char            lastmsg[32];
  int             repeatcount, lastlen;
#endif
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  struct Channel* last_empty_channel;
  struct Channel* next_empty_channel;
#endif
  char            chname[1];
};

typedef struct  Channel aChannel;

extern  struct  Channel *channel;

#define CREATE 1        /* whether a channel should be
                           created or just tested for existance */

#define MODEBUFLEN      1024 /* Memory is cheap. Let's be safe -- asuffield */

#define NullChn ((aChannel *)0)

#define ChannelExists(n)        (hash_find_channel(n, NullChn) != NullChn)

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS   4

extern struct Channel* find_channel (char *, struct Channel *);
extern void    remove_user_from_channel(struct Client *,struct Channel *,int);
extern void    del_invite (struct Client *, struct Channel *);
extern int     can_send (struct Client *, struct Channel *);
extern int     is_chan_op (struct Client *, struct Channel *);
extern int     user_channel_mode(struct Client *, struct Channel *);
extern int     count_channels (struct Client *);
extern void    send_channel_modes (struct Client *, struct Channel *);
extern int     check_channel_name(const char* name);
extern void    channel_modes(struct Client *, char *, char *, struct Channel*);
extern void    set_channel_mode(struct Client *, struct Client *, 
                                struct Channel *, int, char **);
extern char*   pretty_mask(char *);


#define BANSTR(l)  ((l)->value.banptr->banstr)
#define BANFWD(l)  ((l)->value.banptr->ban_forward_chname)

/*
** Channel Related macros follow
*/

/* Channel related flags */

#define CHFL_CHANOP     0x00000001 /* Channel operator */
#define CHFL_VOICE      0x00000002 /* the power to speak */
#define CHFL_DEOPPED    0x00000004 /* deopped by us, modes need to be bounced */
#define CHFL_BAN        0x00000008 /* ban channel flag */
#define CHFL_EXCEPTION  0x00000010 /* exception to ban channel flag */
#define CHFL_DENY       0x00000020 /* regular expression deny flag */
#define CHFL_INVEX      0x00000040 /* invex channel flag */
#define CHFL_QUIET      0x00000080 /* quieted on channel -- asuffield */

/* Channel Visibility macros */

#define MODE_CHANOP        CHFL_CHANOP
#define MODE_VOICE         CHFL_VOICE
#define MODE_DEOPPED       CHFL_DEOPPED
#define MODE_SECRET        0x00000010
#define MODE_MODERATED     0x00000020
#define MODE_TOPICLIMIT    0x00000040
#define MODE_INVITEONLY    0x00000080
#define MODE_NOPRIVMSGS    0x00000100
#define MODE_KEY           0x00000200
#define MODE_BAN           0x00000400
#define MODE_EXCEPTION     0x00000800
#define MODE_DENY          0x00001000
#define MODE_INVEX         0x00002000

/* This is for +o/+v, really should be smaller */
#define MODE_FLAGS         0x00001fff

#define MODE_LIMIT         0x00004000
#ifdef INVITE_CHANNEL_FORWARDING
#define MODE_FORWARD       0x00008000
#endif
#define MODE_NOCOLOR       0x00010000
#define MODE_PERM          0x00020000
#ifdef JUPE_CHANNEL
#define MODE_JUPED         0x00080000
#endif
#define MODE_LOGGING       0x00100000
#define MODE_FREEINVITE    0x00200000
#define MODE_OPMODERATE    0x00400000
#define MODE_NOFORWARD     0x00800000
#define MODE_NOUNIDENT     0x01000000
#define MODE_LARGEBANLIST  0x02000000
#define MODE_QUIETUNIDENT  0x04000000

#ifdef NEED_SPLITCODE

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
#define MODE_SPLIT         0x10000000
extern void remove_empty_channels();
#endif

extern int server_was_split;
extern time_t server_split_time;

#ifdef SPLIT_PONG
extern int got_server_pong;
#endif /* SPLIT_PONG */

#endif /* NEED_SPLITCODE */

/* used in SetMode() in channel.c and m_umode() in s_msg.c */

#define MODE_NULL      0
#define MODE_QUERY     0x10000000
#define MODE_ADD       0x40000000
#define MODE_DEL       0x20000000

#define HoldChannel(x)          (!(x))
/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel visible */
#define ShowChannel(v,c)        ((c)->users && \
				 (HasUmode(v,UMODE_USER_AUSPEX) || PubChannel(c) || IsMember((v),(c))))
#define PubChannel(x)           ((!x) || ((x)->mode.mode & MODE_SECRET) == 0)

#define IsMember(blah,chan) ((blah && blah->user && \
                find_channel_link((blah->user)->channel, chan)) ? 1 : 0)
#if 0
#define IsLogger(cptr,chptr) ((cptr && cptr->user && \
                find_user_link(chptr->loggers, cptr)) ? 1 : 0)
#endif
#define IsLogger(cptr,chptr) 0

#define IsChannelName(name) ((name) && (*(name) == '#'))

/*
  Move BAN_INFO information out of the SLink struct
  its _only_ used for bans, no use wasting the memory for it
  in any other type of link. Keep in mind, doing this that
  it makes it slower as more Malloc's/Free's have to be done, 
  on the plus side bans are a smaller percentage of SLink usage.
  Over all, the th+hybrid coding team came to the conclusion
  it was worth the effort.

  - Dianora
*/
typedef struct Ban      /* also used for exceptions -orabidoo */ /* and for "quieting" -- asuffield */
{
  char *banstr;
  char *who;
  time_t when;
#ifdef BAN_CHANNEL_FORWARDING
  char *ban_forward_chname;
#endif
} aBan;

#if defined(INVITE_CHANNEL_FORWARDING) || defined(BAN_CHANNEL_FORWARDING)
#define INVITE_FORWARD 1
#define BAN_FORWARD 2
#endif

extern char* make_nick_user_host(const char *, const char *, const char *);
extern int can_join(struct Client *, struct Channel *, char *, int *);

#endif  /* INCLUDED_channel_h */

