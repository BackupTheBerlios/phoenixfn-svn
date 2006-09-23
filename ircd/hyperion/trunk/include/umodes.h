/************************************************************************
 *   IRC - Internet Relay Chat, include/s_user.h
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
 *
 */
#ifndef INCLUDED_umodes_h
#define INCLUDED_umodes_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>      /* time_t */
#define INCLUDED_sys_types_h
#endif

/* This is not common, it's only used here */

typedef struct
{
  int mode;
  char letter;
} FLAG_ITEM;

/* umodes, settable flags */
/* Nice, flashy new bitfield math. These are just indexes into the lookup. Fun :)
 *  -- asuffield */
#define UMODE_INVALID         0 /* Used as a delimeter in lists */
#define UMODE_SERVNOTICE      1 /* server notices such as kill */
#define UMODE_CCONN           2 /* Client Connections */
#define UMODE_REJ             3 /* Bot Rejections */
#define UMODE_SKILL           4 /* Server Killed */
#define UMODE_FULL            5 /* Full messages */
#define UMODE_SPY             6 /* see STATS / LINKS */
#define UMODE_DEBUG           7 /* 'debugging' info */
#define UMODE_NCHANGE         8 /* Nick change notice */
#define UMODE_WALLOP          9 /* send wallops to them */
#define UMODE_OPERWALL       10 /* Operwalls */
#define UMODE_INVISIBLE      11 /* makes user invisible */
#define UMODE_BOTS           12 /* shows bots */
#define UMODE_EXTERNAL       13 /* show servers introduced */
#define UMODE_NOINVITE       14 /* Don't send or receive INVITE messages -- PMA */
#define UMODE_NOCTCP         15 /* Don't receive CTCP messages or notices -- PMA */

/* user information flags, only settable by remote mode or local oper */
/* No longer; these are now privileges proper. You can only gain them
 * if your O:line says you can.
 *  -- asuffield
 */
#define UMODE_IMMUNE         16 /* immune from kick/deop */
#define UMODE_OPER           17 /* Operator */
/*#define UMODE_LOCOP          18 *//* Local operator -- SRB */
#define UMODE_AUSPEX         19 /* See the invisible. */
#define UMODE_IDENTIFIED     20 /* Registered and identified with NickServ -- MGS */
#define UMODE_CHCREATE       21 /* Channel Creation Notices */
#define UMODE_BLOCK_NOTID    22 /* Block messages from non-identified users */

#define UMODE_GOD            23 /* greater deity */
#define UMODE_GLOBAL_KILL    24
#define UMODE_REMOTE         25 /* CONNECT, SQUIT */
#define UMODE_KILL           27 /* KILL, KLINE */
#define UMODE_DIE            28
#define UMODE_REHASH         29
#define UMODE_UNKLINE        30
#define UMODE_SEESOPERS      31
#define UMODE_FLUDPROOF      32
#define UMODE_MASSNOTICE     33
#define UMODE_SEEILINES      34
#define UMODE_SEEKLINES      35
#define UMODE_SEEQLINES      36
#define UMODE_SEESTATST      37
#define UMODE_SEESTATSSERV   38
#define UMODE_TESTLINE       39
#define UMODE_FORCELUSERS    40
#define UMODE_ANYNICK        41
#define UMODE_SENDWALLOPS    42
#define UMODE_SETNAME        43
#define UMODE_HIGHPRIORITY   44
#define UMODE_GRANT          45
#define UMODE_SENDOPERWALL   46
#define UMODE_SEEOPERPRIVS   47
#define UMODE_SEEROUTING     48
#define UMODE_EXPERIMENTAL   49
#define UMODE_REMOTEINFO     50
#define UMODE_SERVCONNECT    51
#define UMODE_CHANGEOTHER    52
#define UMODE_FREESPOOF      53
#define UMODE_USER_AUSPEX    54
#define UMODE_CANLOGCHANNEL  55
#define UMODE_NOFORWARD      56
#define UMODE_AUTODLINE      57
#define UMODE_SHOWASSTAFF    58
#define UMODE_MORECHANS      59
#define UMODE_DONTBLOCK      60

/* Current max umode: 63 */
#define MAX_UMODE_COUNT      64

/* table of ascii char letters to corresponding bitmask */
extern FLAG_ITEM user_mode_table[];

/* DO NOT change this without changing the bitfield macros */
typedef u_int32_t user_modes[2];

/* Support stuff for the bitfields */
/* Change this, change all the boolean macros below */
#define BITFIELD_SIZE 64

#define SetBit(f,b)   (((f)[bitfield_lookup[b].field]) |=  bitfield_lookup[b].bit)
#define ClearBit(f,b) (((f)[bitfield_lookup[b].field]) &= ~bitfield_lookup[b].bit)
#define TestBit(f,b)  (((f)[bitfield_lookup[b].field]) &  bitfield_lookup[b].bit)

/* Written out in full for speed.
 * I never thought I would actually use the , operator. You live and learn.
 *  -- asuffield
 */
#define CopyUmodes(d,s) (((d)[0] = (s)[0]), ((d)[1] = (s)[1]))
#define AndUmodes(d,a,b) (((d)[0] = (a)[0] & (b)[0]), ((d)[1] = (a)[1] & (b)[1]))
#define OrUmodes(d,a,b) (((d)[0] = (a)[0] | (b)[0]), ((d)[1] = (a)[1] | (b)[1]))
#define AndNotUmodes(d,a,b) (((d)[0] = (a)[0] & ~(b)[0]), ((d)[1] = (a)[1] & ~(b)[1]))
#define NotUmodes(d,s) (((d)[0] = ~s[0]), ((d)[1] = ~s[1]))
#define AnyBits(d) ((d)[0] | (d)[1])
#define SameBits(a,b) (((a)[0] == (b)[0]) && ((a)[1] == (b)[1]))

#define ClearBitfield(b) (((b)[0] = 0), ((b)[1] = 0))

/* These are usually used */
#define HasUmode(c,m) (TestBit((c)->umodes,m))
#define SetUmode(c,m) (SetBit((c)->umodes,m))
#define ClearUmode(c,m) (ClearBit((c)->umodes,m))

/* These are for the aliased things and stuff that might change, etc... */
/* umode flags */

#define IsInvisible(x)          (HasUmode(x,UMODE_INVISIBLE))
#define SendOperwall(x)         (HasUmode(x,UMODE_SENDOPERWALL))
#define ReceiveOperwall(x)      (HasUmode(x,UMODE_OPERWALL))

#ifdef CHANNEL_CREATION_NOTICE
#define SeesChannelCreations(x) (HasUmode(x,UMODE_CHCREATE))
#endif

#define SeesOperMessages(x)     (HasUmode(x,UMODE_OPER))
#ifdef OPERHIDE
#define SeesOpers(x)            (HasUmode(x,UMODE_SEESOPERS))
#else
#define SeesOpers(x)            (1)
#endif
#define SeesOperPrivs(x)        (HasUmode(x,UMODE_SEEOPERPRIVS))
#define SendWhileLifeSucks(x)   (HasUmode(x,UMODE_HIGHPRIORITY))
#define IsAlwaysBusy(x)         (HasUmode(x,UMODE_HIGHPRIORITY))
/* This one means no protection against *this* client flooding */
#define NoFloodProtection(x)    (HasUmode(x,UMODE_FLUDPROOF))
#define CanSeeBlines(x)         (HasUmode(x,UMODE_SEEILINES))
#define CanSeeDlines(x)         (HasUmode(x,UMODE_SEEKLINES))
#define CanSeeElines(x)         (HasUmode(x,UMODE_SEEILINES))
#define CanSeeFlines(x)         (HasUmode(x,UMODE_SEEILINES))
#define CanSeeIlines(x)         (HasUmode(x,UMODE_SEEILINES))
#define CanSeeKlines(x)         (HasUmode(x,UMODE_SEEKLINES))
#define CanSeeOlines(x)         (HasUmode(x,UMODE_SEESOPERS))
#define CanSeeQlines(x)         (HasUmode(x,UMODE_SEEQLINES))
#define CanSeeStatsT(x)         (HasUmode(x,UMODE_SEESTATST))
#define CanSeeXlines(x)         (HasUmode(x,UMODE_SEEQLINES))
#define CanSeeYlines(x)         (HasUmode(x,UMODE_SEEILINES))
#define CanSeeStatsServinfo(x)  (HasUmode(x,UMODE_SEESTATSSERV))
#define CanForceLusers(x)       (HasUmode(x,UMODE_FORCELUSERS))
#define CannotBeXlined(x)       (HasUmode(x,UMODE_ANYNICK))
#define SendWallops(x)          (HasUmode(x,UMODE_SENDWALLOPS))
#define ReceiveWallops(x)       (HasUmode(x,UMODE_WALLOP))
#define CanConnect(x)           (HasUmode(x,UMODE_REMOTE))
#define ShowAsStaff(x)          (HasUmode(x,UMODE_SHOWASSTAFF))
/* old crud for !SERVERHIDE -- jilles */
#define SeesAllConnections(x)   (HasUmode(x,UMODE_USER_AUSPEX))
#define IsSetOperAuspex(x)      (HasUmode(x,UMODE_USER_AUSPEX))

struct bitfield_lookup_t
{
  unsigned int field;
  u_int32_t bit;
};

/* This is initialised in init_umodes() in s_user.c, based on the value of BITFIELD_SIZE */
extern struct bitfield_lookup_t bitfield_lookup[];
extern char umode_list[];

/* There is no reason why this cannot be calculated at runtime -- asuffield */
extern int user_modes_from_c_to_bitmask[];

extern void  init_umodes(void);
extern user_modes user_umodes, null_umodes;
extern char *umodes_as_string(user_modes *);
extern void umodes_from_string(user_modes *, char *);
extern char *umode_difference(user_modes *, user_modes *);
extern user_modes *build_umodes(user_modes *, int, ...);

#endif
