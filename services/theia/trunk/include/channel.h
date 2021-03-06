/*
 * channel.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: channel.h,v 1.4 2001/06/01 21:13:13 kreator Exp $
 */

#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h

#ifndef INCLUDED_sys_time_h
#include <sys/time.h>        /* time_t */
#define INCLUDED_sys_time_h
#endif

#ifndef INCLUDED_hybdefs_h
#include "hybdefs.h"         /* CHANNELLEN ... */
#define INCLUDED_hybdefs_h
#endif

struct Luser;
struct UserChannel;

/*
 * Channel flags
 */
#define CH_VOICED       0x000001 /* user is voiced */
#define CH_OPPED        0x000002 /* user is opped */
#define MODE_O          0x000004 /* someone was +o'd */
#define MODE_V          0x000008 /* someone was +v'd */
#define MODE_L          0x000010 /* channel is +l */
#define MODE_K          0x000020 /* channel is +k */
#define MODE_S          0x000040 /* channel is +s */
#define MODE_P          0x000080 /* channel is +p */
#define MODE_N          0x000100 /* channel is +n */
#define MODE_T          0x000200 /* channel is +t */
#define MODE_M          0x000400 /* channel is +m */
#define MODE_I          0x000800 /* channel is +i */
#define MODE_C          0x001000 /* channel is +c */
#define MODE_F          0x002000 /* channel is +f */
#define MODE_G          0x004000 /* channel is +g */
#define MODE_CAPF	0x008000 /* channel is +F -ugh, not in 1.0.34-silently ignore */
#define MODE_CAPJ	0x010000 /* channel is +J */
#define MODE_CAPP	0x020000 /* channel is +P */
#define MODE_CAPQ	0x040000 /* channel is +Q */
#define MODE_R		0x080000 /* channel is +r */
#define MODE_Z		0x100000 /* channel is +z */
#define MODE_CAPD	0x200000 /* channel is +J */
#define MODE_CAPL	0x400000 /* channel is +L */
#define MODE_CAPR	0x800000 /* channel is +R */

#ifdef HYBRID7
# define CH_HOPPED       0x001000 /* user is halfopped - Janos */
# define MODE_H          0x002000 /* someone was +h - Janos */
# define MODE_A          0x004000 /* channel is +a - Janos */
#endif /* HYBRID7 */

#ifdef GECOSBANS
struct ChannelGecosBan
{
  struct ChannelGecosBan *next, *prev;
  char *who;     /* who set the ban */
  time_t when;   /* when the ban was made */
  char *mask;    /* hostmask of the ban */
};
#endif /* GECOSBANS */

struct ChannelBan
{
  struct ChannelBan *next, *prev;
  char *who;     /* who set the ban */
  time_t when;   /* when the ban was made */
  char *mask;    /* hostmask of the ban */
};

struct Exception
{
  struct Exception *next, *prev;
  char *who;    /* who set the exception */
  time_t when;  /* when it was set */
  char *mask;   /* exception hostmask */
};

#ifdef HYBRID7
struct InviteException
{
  struct InviteException *next, *prev;
  char *who;    /* who set the invite exception */
  time_t when;  /* when it was set */
  char *mask;   /* invite exception hostmask */
};
#endif /* HYBRID7 */

struct ChannelUser
{
  struct ChannelUser *next;
  struct Luser *lptr;   /* pointer to user structure */
  unsigned flags;       /* flags such as opped/voiced */
};

/* Stores info for network channels */
struct Channel

{
  struct Channel *next, *prev, *hnext;

#ifdef BLOCK_ALLOCATION

  char name[CHANNELLEN + 1]; /* channel name */
  char key[KEYLEN + 1];
  char forward[CHANNELLEN + 1];
  char dline[12];
  char throttle[12];

#else

  char *name;             /* channel name */
  char *key;              /* NULL if no key */
  char *forward;          /* NULL if no forwarding */
  char *throttle;

#endif /* BLOCK_ALLOCATION */

  int numusers;           /* number of users in the channel */
  int modes;              /* channel modes */
  int limit;              /* 0 if no limit */
  struct ChannelUser *firstuser; /* pointer to first user in channel */
  time_t since;           /* when the channel was created (TS) */
  struct ChannelBan *firstban; /* pointer to first ban */
#ifdef GECOSBANS
  struct ChannelGecosBan *firstgecosban; /* pointer to first gecos field ban*/
#endif /* GECOSBANS */
  struct Exception *exceptlist; /* pointer to first ban exception */
  struct InviteException *inviteexceptlist; /* ptr to first invite
                                               exception - Janos */

  /*
   * flood_ts[0] is the TS of the first time a *Serv was kicked;
   * flood_ts[1] is the TS of the last time
   */
  time_t flood_ts[2];
  int floodcnt;           /* how many times a *Serv was kicked/deoped */
};

/*
 * Prototypes
 */

#ifdef GECOSBANS
void AddGecosBan(char *, struct Channel *, char *);
void DeleteGecosBan(struct Channel *, char *);
#endif /* GECOSBANS */

void AddBan(char *, struct Channel *, char *);
void DeleteBan(struct Channel *, char *);
void AddException(char *, struct Channel *, char *);
void DeleteException(struct Channel *, char *);
struct Exception *HasException(struct Channel *cptr, struct Luser *lptr);

#ifdef HYBRID7
void AddInviteException(char *, struct Channel *, char *);
void DeleteInviteException(struct Channel *, char *);
#endif /* HYBRID7 */

struct Channel *AddChannel(char **, int, char **);
void DeleteChannel(struct Channel *);
void AddToChannel(struct Channel *, char *);
void RemoveNickFromChannel(char *, char *);
void RemoveFromChannel(struct Channel *, struct Luser *);
void UpdateChanModes(struct Luser *lptr, char *source,
                     struct Channel *chptr, char *modes);
struct UserChannel *FindChannelByUser(struct Luser *lptr,
                                      struct Channel *chptr);
struct ChannelUser *FindUserByChannel(struct Channel *chptr,
                                      struct Luser *lptr);
void DoMode(struct Channel *, char *, int);
void SetModes(char *source, int plus, char mode,
              struct Channel *chptr, char *args);
void KickBan(int, char *, struct Channel *, char *, char *);
struct Channel *IsChan(char *channel);
int IsChannelMember(struct Channel *chptr, struct Luser *lptr);
int IsChannelVoice(struct Channel *chptr, struct Luser *lptr);
int IsChannelOp(struct Channel *chptr, struct Luser *lptr);

#ifdef HYBRID7
int IsChannelHOp(struct Channel *chptr, struct Luser *lptr);
#endif /* HYBRID7 */

struct ChannelBan *MatchBan(struct Channel *, char *);
struct ChannelBan *FindBan(struct Channel *, char *);
struct Exception *MatchException(struct Channel *, char *);
struct Exception *FindException(struct Channel *, char *);

#ifdef HYBRID7
struct InviteException *MatchInviteException(struct Channel *, char *);
struct InviteException *FindInviteException(struct Channel *, char *);
#endif /* HYBRID7 */

#ifdef GECOSBANS
struct ChannelGecosBan *MatchGecosBan(struct Channel *, char *);
struct ChannelGecosBan *FindGecosBan(struct Channel *, char *);
#endif /* GECOSBANS */

/*
 * External declarations
 */

extern struct Channel *ChannelList;

#endif /* INCLUDED_channel_h */
