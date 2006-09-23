/************************************************************************
 *   IRC - Internet Relay Chat, include/msg.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
#ifndef INCLUDED_msg_h
#define INCLUDED_msg_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

struct Client;

/* 
 * Message table structure 
 */
struct  Message
{
  const char  *cmd;
  int (*func)(struct Client *,struct Client *,int,char **);
  unsigned int  count;                  /* number of times command used */
  int   parameters;
  char  flags;
  /* bit 0 set means that this command is allowed to be used
   * only on the average of once per 2 seconds -SRB */

  /* I could have defined other bit maps to above instead of the next two
     flags that I added. so sue me. -Dianora */

  char    allow_unregistered_use;       /* flag if this command can be
                                           used if unregistered */

  char    reset_idle;                   /* flag if this command causes
                                           idle time to be reset */
  char    allow_honeypot;               /* true if this command can be used in the honeypot */

  unsigned long bytes;
};

struct MessageTree
{
  const char *final;
  struct Message *msg;
  struct MessageTree *pointers[26];
}; 

typedef struct MessageTree MESSAGE_TREE;

#ifdef TSDELTA
#define MSG_TSDELTA  "TSDELTA"
#define MSG_UTIME    "UTIME"
#endif

#define MSG_BURST    "BURST"
#define MSG_SNICK    "SNICK"
#define MSG_IDLE     "IDLE"
#define MSG_COLLIDE  "COLLIDE"

#ifdef CHALLENGERESPONSE
#define MSG_CHALL    "CHALL"    /* CHALL */
#define MSG_RESP     "RESP"     /* RESP */
#endif

#define MSG_PRIVATE  "PRIVMSG"  /* PRIV */
#define MSG_WHO      "WHO"      /* WHO  -> WHOC */
#define MSG_WHOIS    "WHOIS"    /* WHOI */
#define MSG_WHOWAS   "WHOWAS"   /* WHOW */
#define MSG_USER     "USER"     /* USER */
#define MSG_NICK     "NICK"     /* NICK */
#define MSG_SERVER   "SERVER"   /* SERV */
#define MSG_LIST     "LIST"     /* LIST */
#define MSG_TOPIC    "TOPIC"    /* TOPI */
#define MSG_INVITE   "INVITE"   /* INVI */
#define MSG_VERSION  "VERSION"  /* VERS */
#define MSG_QUIT     "QUIT"     /* QUIT */
#define MSG_SQUIT    "SQUIT"    /* SQUI */
#define MSG_KILL     "KILL"     /* KILL */
#define MSG_INFO     "INFO"     /* INFO */
#define MSG_LINKS    "LINKS"    /* LINK */
#define MSG_MAP      "MAP"      /* MAP */
#define MSG_STATS    "STATS"    /* STAT */
#define MSG_USERS    "USERS"    /* USER -> USRS */
#define MSG_HELP     "HELP"     /* HELP */
#define MSG_ERROR    "ERROR"    /* ERRO */
#define MSG_AWAY     "AWAY"     /* AWAY */
#define MSG_CONNECT  "CONNECT"  /* CONN */
#define MSG_PING     "PING"     /* PING */
#define MSG_PONG     "PONG"     /* PONG */
#define MSG_OPER     "OPER"     /* OPER */
#define MSG_PASS     "PASS"     /* PASS */
#define MSG_WALLOPS  "WALLOPS"  /* WALL */
#define MSG_TIME     "TIME"     /* TIME */
#define MSG_NAMES    "NAMES"    /* NAME */
#define MSG_ADMIN    "ADMIN"    /* ADMI */
#define MSG_TRACE    "TRACE"    /* TRAC */
#define MSG_LTRACE   "LTRACE"   /* LTRA */
#define MSG_NOTICE   "NOTICE"   /* NOTI */
#define MSG_JOIN     "JOIN"     /* JOIN */
#define MSG_LJOIN    "LJOIN"
#define MSG_LPART    "LPART"
#define MSG_PART     "PART"     /* PART */
#define MSG_LUSERS   "LUSERS"   /* LUSE */
#define MSG_MOTD     "MOTD"     /* MOTD */
#define MSG_MODE     "MODE"     /* MODE */
#define MSG_KICK     "KICK"     /* KICK */
#define MSG_REMOVE   "REMOVE"
#define MSG_USERHOST "USERHOST" /* USER -> USRH */
#define MSG_ISON     "ISON"     /* ISON */
#define MSG_REHASH   "REHASH"   /* REHA */
#define MSG_RESTART  "RESTART"  /* REST */
#define MSG_CLOSE    "CLOSE"    /* CLOS */
#define MSG_SVINFO   "SVINFO"   /* SVINFO */
#define MSG_SJOIN    "SJOIN"    /* SJOIN */
#define MSG_CAPAB    "CAPAB"    /* CAPAB */
#define MSG_DIE      "DIE"      /* DIE */
#define MSG_HASH     "HASH"     /* HASH */
#define MSG_DNS      "DNS"      /* DNS  -> DNSS */
#define MSG_OPERWALL "OPERWALL" /* OPERWALL */
#define MSG_KLINE    "KLINE"    /* KLINE */
#define MSG_UNKLINE  "UNKLINE"  /* UNKLINE */
#define MSG_DLINE    "DLINE"    /* DLINE */
#define MSG_HTM      "HTM"      /* HTM */
#define MSG_SET      "SET"      /* SET */
#define MSG_SILENCE  "SILENCE"  /* SILENCE */

#define MSG_SETHOST  "SETHOST"  /* SETHOST */
#define MSG_SETIDENT "SETIDENT" /* SETIDENT */
#define MSG_SETNAME  "SETNAME"  /* SETNAME */
#define MSG_MAKEPASS "MAKEPASS" /* MAKEPASS */
#define MSG_STOPIC   "STOPIC"   /* STOPIC */

#define MSG_KNOCK          "KNOCK"  /* KNOCK */

#ifdef USE_SERVICES
#define MSG_CHANSERV    "CHANSERV"      /* CHANSERV */
#define MSG_CS                  "CS"                    /* CS */
#define MSG_NICKSERV    "NICKSERV"      /* NICKSERV */
#define MSG_NS                  "NS"                    /* NS */
#define MSG_MEMOSERV    "MEMOSERV"      /* MEMOSERV */
#define MSG_MS                  "MS"                    /* MS */
#define MSG_OPERSERV    "OPERSERV"      /* OPERSERV */
#define MSG_OS                  "OS"                    /* OS */
#define MSG_SEENSERV    "SEENSERV"      /* SEENSERV */
#define MSG_LS                  "LS"                    /* LS */
#define MSG_HELPSERV    "HELPSERV"      /* HELPSERV */
#define MSG_HS                  "HS"                    /* HS */
#define MSG_STATSERV    "STATSERV"      /* STATSERV */
#define MSG_SS                  "SS"                    /* SS */
#define MSG_GLOBAL      "GLOBALNOTICE"  /* GLOBALNOTICE */
#define MSG_GN                  "GN"                    /* GN */
#endif

#define MSG_SPINGTIME   "SPINGTIME"
#define MSG_SCAN        "SCAN"
#define MSG_DUMP        "DUMP"
#define MSG_LANG        "LANG"

#define MAXPARA    15 

#define MSG_TESTLINE "TESTLINE"

#define MSG_SIGNON   "SIGNON"
#define MSG_SVSLOGIN "SVSLOGIN"

#define MSG_UMODE	"UMODE"

#ifdef MSGTAB
#ifndef INCLUDED_m_commands_h
#include "m_commands.h"       /* m_xxx */
#endif
struct Message msgtab[] = {
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 1, 0, 0L },

  /*                                           ^
                                               |__ reset idle time when 1 */
#ifdef TSDELTA
  { MSG_TSDELTA, m_tsdelta,  0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_UTIME,   m_utime,    0, MAXPARA, 1, 0, 0, 0, 0L },
#endif

  { MSG_NICK,    m_nick,     0, MAXPARA, 1, 1, 1, 1, 0L },
  { MSG_NOTICE,  m_notice,   0, MAXPARA, 1, 1, 0, 0, 0L },
  { MSG_JOIN,    m_join,     0, MAXPARA, 1, 0, 0, 0, 0L },
/*   { MSG_LJOIN,   m_ljoin,    0, MAXPARA, 1, 0, 1, 0, 0L }, */
/*   { MSG_LPART,   m_lpart,    0, MAXPARA, 1, 0, 1, 0, 0L }, */
  { MSG_MODE,    m_mode,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_QUIT,    m_quit,     0, MAXPARA, 1, 1, 0, 1, 0L },
  { MSG_PART,    m_part,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_KNOCK,   m_knock,    0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_TOPIC,   m_topic,    0, MAXPARA, 1, 0, 1, 1, 0L },
  { MSG_INVITE,  m_invite,   0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_KICK,    m_kick,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_REMOVE,  m_remove,   0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_WALLOPS, m_wallops,  0, MAXPARA, 1, 0, 1, 0, 0L },

  /* Only m_private has reset idle flag set */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 0, 1, 0L },

  { MSG_ERROR,   m_error,    0, MAXPARA, 1, 1, 0, 1, 0L },
  { MSG_KILL,    m_kill,     0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_COLLIDE, m_collide,  0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_USER,    m_user,     0, MAXPARA, 1, 1, 0, 1, 0L },
  { MSG_AWAY,    m_away,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_ISON,    m_ison,     0, 1,       1, 0, 0, 0, 0L },
  { MSG_SERVER,  m_server,   0, MAXPARA, 1, 1, 0, 0, 0L },
  { MSG_SQUIT,   m_squit,    0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_WHOIS,   m_whois,    0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_WHO,     m_who,      0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_WHOWAS,  m_whowas,   0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_LIST,    m_list,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_NAMES,   m_names,    0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_USERHOST,m_userhost, 0, 1,       1, 0, 0, 1, 0L },
  { MSG_TRACE,   m_trace,    0, MAXPARA, 1, 0, 0, 1, 0L },
#ifdef LTRACE
  { MSG_LTRACE,  m_ltrace,   0, MAXPARA, 1, 0, 0, 1, 0L },
#endif /* LTRACE */
#ifdef CHALLENGERESPONSE
  { MSG_CHALL,   m_chall,    0, MAXPARA, 1, 1, 0, 0, 0L },  
  { MSG_RESP,    m_resp,     0, MAXPARA, 1, 1, 0, 0, 0L },
#endif
  { MSG_PASS,    m_pass,     0, MAXPARA, 1, 1, 0, 1, 0L },
  { MSG_LUSERS,  m_lusers,   0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_TIME,    m_time,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_OPER,    m_oper,     0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_CONNECT, m_connect,  0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_VERSION, m_version,  0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_STATS,   m_stats,    0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_LINKS,   m_links,    0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_MAP,     m_map,      0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_ADMIN,   m_admin,    0, MAXPARA, 1, 1, 0, 1, 0L },
  { MSG_USERS,   m_users,    0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_HELP,    m_help,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_INFO,    m_info,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_MOTD,    m_motd,     0, MAXPARA, 1, 0, 0, 1, 0L },
  { MSG_SVINFO,  m_svinfo,   0, MAXPARA, 1, 1, 0, 0, 0L },
  { MSG_SJOIN,   m_sjoin,    0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_CAPAB,   m_capab,    0, MAXPARA, 1, 1, 0, 0, 0L },
  { MSG_OPERWALL, m_operwall,0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_CLOSE,   m_close,    0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_KLINE,   m_kline,    0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_UNKLINE, m_unkline,  0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_DLINE,   m_dline,    0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_HASH,    m_hash,     0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_DNS,     m_dns,      0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_REHASH,  m_rehash,   0, MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_RESTART, m_restart,  0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_DIE, m_die,          0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_HTM,    m_htm,       0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SET,    m_set,       0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SILENCE,  m_silence, 0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_TESTLINE, m_testline,0, MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_CHANSERV, m_chanserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_CS,       m_chanserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_NICKSERV, m_nickserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_NS,       m_nickserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_MEMOSERV, m_memoserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_MS,       m_memoserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_OPERSERV, m_operserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_OS,       m_operserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_SEENSERV, m_seenserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_LS,       m_seenserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_STATSERV, m_statserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_SS,       m_statserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_HELPSERV, m_helpserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_HS,       m_helpserv,      0, 1, 1, 0, 1, 1, 0L },
  { MSG_GLOBAL,   m_global,        0, 1, 1, 0, 1, 1, 0L },
  { MSG_GN,       m_global,        0, 1, 1, 0, 1, 1, 0L },
  { MSG_SETHOST,  m_sethost,       0, 2, 1, 0, 0, 0, 0L },
  { MSG_SETIDENT, m_setident,      0, 2, 1, 0, 0, 0, 0L },
  { MSG_SETNAME,  m_setname,       0, 2, 1, 0, 0, 0, 0L },
  { MSG_MAKEPASS, m_makepass,      0, 2, 1, 0, 0, 1, 0L },
  { MSG_STOPIC,   m_stopic,   0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SPINGTIME,m_spingtime,0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SCAN,     m_scan,     0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_DUMP,     m_dump,     0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_BURST,    m_burst,    0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SNICK,    m_snick,    0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_IDLE,     m_idle,     0,MAXPARA, 1, 0, 1, 0, 0L },
  { MSG_SIGNON,   m_signon,   0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_SVSLOGIN, m_svslogin, 0,MAXPARA, 1, 0, 0, 0, 0L },
  { MSG_UMODE,    m_umode,    0,MAXPARA, 1, 0, 0, 1, 0L },

  { (char *) 0, (int (*)(struct Client *,struct Client *,int,char **)) 0 , 0, 0,    0, 0, 0, 0, 0L }
};

struct MessageTree* msg_tree_root;

#else
extern struct Message       msgtab[];
extern struct MessageTree*  msg_tree_root;
#endif

#endif /* INCLUDED_msg_h */

