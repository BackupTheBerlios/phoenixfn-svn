/************************************************************************
 *   IRC - Internet Relay Chat, src/s_debug.c
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
 */

#include "s_debug.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dbuf.h"
#include "dline_conf.h"
#include "hash.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "motd.h"
#include "mtrie_conf.h"
#include "m_kline.h"
#include "numeric.h"
#include "res.h"
#include "s_auth.h"
#include "s_conf.h"
#include "s_log.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/resource.h>

/*
 * 'offsetof' is defined in ANSI-C. The following definition
 * is not absolutely portable (I have been told), but so far
 * it has worked on all machines I have needed it. The type
 * should be size_t but...  --msa
 */
#ifndef offsetof
#define offsetof(t, m) (size_t)((&((t *)0L)->m))
#endif

#define CLIENT_LOCAL_SIZE sizeof(struct Client)
#define CLIENT_REMOTE_SIZE offsetof(struct Client, count)

/*
 * Option string.  Must be before #ifdef DEBUGMODE.
 */
const char serveropts[] = {
#ifdef  DEBUGMODE
  'D',
#endif
#ifdef  SHOW_INVISIBLE_LUSERS
  'i',
#endif
  'M',
#ifdef ZIP_LINKS
  'Z',
#endif
#ifdef IPV6
  '6',
#endif
  ' ',
  'd',
  'n',
  'c',
  'r',
  'T',
  'S',
  '/',
  'v',
#ifdef TS_CURRENT
  '0' + TS_CURRENT,
#endif
  '\0'
};

void
debug(int level, const char *format, ...)
{
  static char debugbuf[1024];
  va_list args;
  int err = errno;

  if ((debuglevel >= 0) && (level <= debuglevel))
    {
      va_start(args, format);

      vsprintf(debugbuf, format, args);
      va_end(args);

      logprintf(L_DEBUG, "%s", debugbuf);
    }
  errno = err;
}				/* debug() */

/*
 * This is part of the STATS replies. There is no offical numeric for this
 * since this isnt an official command, in much the same way as HASH isnt.
 * It is also possible that some systems wont support this call or have
 * different field names for "struct rusage".
 * -avalon
 */
void
send_usage(aClient * cptr, char *nick)
{
  struct rusage rus;
  time_t secs;
  time_t rup;
#ifdef  hz
# define hzz hz
#else
# ifdef HZ
#  define hzz HZ
# else
  int hzz = 1;
# endif
#endif

  if (getrusage(RUSAGE_SELF, &rus) == -1)
    {
      sendto_one(cptr, ":%s NOTICE %s :Getruseage error: %s.",
		 me.name, nick, strerror(errno));
      return;
    }
  secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
  if (0 == secs)
    secs = 1;

  rup = (CurrentTime - me.since) * hzz;
  if (0 == rup)
    rup = 1;


  sendto_one(cptr,
	     ":%s %d %s :CPU Secs %ld:%ld User %ld:%ld System %ld:%ld",
	     me.name, RPL_STATSDEBUG, nick, secs / 60, secs % 60,
	     rus.ru_utime.tv_sec / 60, rus.ru_utime.tv_sec % 60,
	     rus.ru_stime.tv_sec / 60, rus.ru_stime.tv_sec % 60);
  sendto_one(cptr, ":%s %d %s :RSS %ld ShMem %ld Data %ld Stack %ld",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_maxrss,
	     rus.ru_ixrss / rup, rus.ru_idrss / rup, rus.ru_isrss / rup);
  sendto_one(cptr, ":%s %d %s :Swaps %ld Reclaims %ld Faults %ld",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_nswap,
	     rus.ru_minflt, rus.ru_majflt);
  sendto_one(cptr, ":%s %d %s :Block in %ld out %ld",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_inblock, rus.ru_oublock);
  sendto_one(cptr, ":%s %d %s :Msg Rcv %ld Send %ld",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_msgrcv, rus.ru_msgsnd);
  sendto_one(cptr, ":%s %d %s :Signals %ld Context Vol. %ld Invol %ld",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_nsignals,
	     rus.ru_nvcsw, rus.ru_nivcsw);

}

void
count_memory(aClient * cptr, char *nick)
{
  struct Client *acptr;
  Link *gen_link;
  struct Channel *chptr;
  struct ConfItem *aconf;
  struct Class *cltmp;
  struct Listener *listener;
  struct unkline_record *ukr;
  struct AuthRequest *auth;

  int lc = 0;			/* local clients */
  int ch = 0;			/* channels */
  int lcc = 0;			/* local client conf links */
  int rc = 0;			/* remote clients */
  int us = 0;			/* user structs */
  int ss = 0;                   /* serv structs */
  int chu = 0;			/* channel users */
  int chi = 0;			/* channel invites */
  int chb = 0;			/* channel bans */
  int wwu = 0;			/* whowas users */
  int cl = 0;			/* classes */
  int co = 0;			/* conf lines */

  int usi = 0;			/* users invited */
  int usc = 0;			/* users in channels */
  int aw = 0;			/* aways set */
  int number_ips_stored;	/* number of ip addresses hashed */
  int number_servers_cached;	/* number of servers cached by scache */
  int listeners = 0;
  int auth_requests = 0;

  size_t chm = 0;		/* memory used by channels */
  size_t chbm = 0;		/* memory used by channel bans */
  size_t lcm = 0;		/* memory used by local clients */
  size_t rcm = 0;		/* memory used by remote clients */
  size_t awm = 0;		/* memory used by aways */
  size_t wwm = 0;		/* whowas array memory used */
  size_t com = 0;		/* memory used by conf lines */
/*  size_t rm = 0;        *//* res memory used */
  size_t mem_servers_cached;	/* memory used by scache */
  size_t mem_ips_stored;	/* memory used by ip address hash */

  int unklines = 0;
  size_t unkline_memory = 0, dline_memory = 0, ip_kline_memory = 0;
  size_t mtrie_memory = 0;

  size_t dbuf_allocated = 0;
  size_t dbuf_used = 0;
  size_t dbuf_alloc_count = 0;
  size_t dbuf_used_count = 0;

  size_t client_hash_table_size = 0;
  size_t channel_hash_table_size = 0;
  size_t totcl = 0;
  size_t totch = 0;
  size_t totww = 0;

  size_t local_client_memory_used = 0;
  size_t local_client_memory_allocated = 0;
  size_t local_client_memory_overheads = 0;

  size_t remote_client_memory_used = 0;
  size_t remote_client_memory_allocated = 0;
  size_t remote_client_memory_overheads = 0;

  size_t user_memory_used = 0;
  size_t user_memory_allocated = 0;
  size_t user_memory_overheads = 0;

  size_t links_memory_used = 0;
  size_t links_memory_allocated = 0;
  size_t links_memory_overheads = 0;

#ifdef FLUD
  size_t flud_memory_used = 0;
  size_t flud_memory_allocated = 0;
  size_t flud_memory_overheads = 0;
#endif

  size_t message_files = 0;
  size_t qline_memory = 0;

  size_t tot = 0;

  count_whowas_memory(&wwu, &wwm);	/* no more away memory to count */

  for (acptr = GlobalClientList; acptr; acptr = acptr->next)
    {
      if (MyConnect(acptr))
	{
	  lc++;
	  for (gen_link = acptr->confs; gen_link; gen_link = gen_link->next)
	    lcc++;
	}
      else
	rc++;
      if (acptr->user)
	{
	  us++;
	  for (gen_link = acptr->user->invited; gen_link;
	       gen_link = gen_link->next)
	    usi++;
	  for (gen_link = acptr->user->channel; gen_link;
	       gen_link = gen_link->next)
	    usc++;
	  if (acptr->user->away)
	    {
	      aw++;
	      awm += (strlen(acptr->user->away) + 1);
	    }
	}
      if (acptr->serv)
	ss++;
    }
  lcm = lc * CLIENT_LOCAL_SIZE;
  rcm = rc * CLIENT_REMOTE_SIZE;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      ch++;
      chm += (strlen(chptr->chname) + sizeof(struct Channel) + 1);
      for (gen_link = chptr->members; gen_link; gen_link = gen_link->next)
	chu++;
      for (gen_link = chptr->invites; gen_link; gen_link = gen_link->next)
	chi++;
      for (gen_link = chptr->banlist; gen_link; gen_link = gen_link->next)
	{
	  chb++;
	  chbm += sizeof(aBan);
	  chbm += strlen(gen_link->value.banptr->banstr) + 1;
	  chbm += strlen(gen_link->value.banptr->who) + 1;
	}
    }

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      co++;
      com += aconf->host ? strlen(aconf->host) + 1 : 0;
      com += aconf->passwd ? strlen(aconf->passwd) + 1 : 0;
      com += aconf->name ? strlen(aconf->name) + 1 : 0;
      com += sizeof(aConfItem);
    }

  for (cltmp = ClassList; cltmp; cltmp = cltmp->next)
    cl++;

  for (listener = ListenerPollList; listener; listener = listener->next)
    listeners++;

  for (auth = AuthPollList; auth; auth = auth->next)
    auth_requests++;

  for (auth = AuthIncompleteList; auth; auth = auth->next)
    auth_requests++;

  for (ukr = recorded_unklines; ukr; ukr = ukr->next)
    {
      unklines++;
      unkline_memory += sizeof(struct unkline_record);
      unkline_memory += strlen(ukr->mask) + 1;
    }

  /*
   * need to set dbuf_count here because we use a dbuf when we send
   * the results. since sending the results results in a dbuf being used,
   * the count would be wrong if we just used the globals
   */
  count_dbuf_memory(&dbuf_allocated, &dbuf_used);
  dbuf_alloc_count = DBufCount;
  dbuf_used_count = DBufUsedCount;

  sendto_one(cptr, ":%s %d %s :Client Local %d(%zd) Remote %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, lc, lcm, rc, rcm);
  sendto_one(cptr, ":%s %d %s :Users %d(%zd) Invites %d(%zd) Servers %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, us, us * sizeof(anUser), usi,
	     usi * sizeof(Link), ss, ss * sizeof(struct Server));
  sendto_one(cptr, ":%s %d %s :User channels %d(%zd) Aways %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, usc, usc * sizeof(Link), aw, awm);
  sendto_one(cptr, ":%s %d %s :Attached confs %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, lcc, lcc * sizeof(Link));

  totcl = lcm + rcm + us * sizeof(anUser) + usc * sizeof(Link) + awm;
  totcl += lcc * sizeof(Link) + usi * sizeof(Link);
  totcl += ss * sizeof(struct Server);

  sendto_one(cptr, ":%s %d %s :Conflines %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, co, com);

  sendto_one(cptr, ":%s %d %s :Unklines %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, unklines, unkline_memory);

  qline_memory = count_qlines();
  sendto_one(cptr, ":%s %d %s :Qlines %zd",
	     me.name, RPL_STATSDEBUG, nick, qline_memory);

  mtrie_memory = count_mtrie_conf_links();
  sendto_one(cptr, ":%s %d %s :Mtrie conflines %zd",
             me.name, RPL_STATSDEBUG, nick, mtrie_memory);

  dline_memory = count_dlines();
  ip_kline_memory = count_ip_klines();
  sendto_one(cptr, ":%s %d %s :D:lines %zd IP K:lines %zd",
	     me.name, RPL_STATSDEBUG, nick, dline_memory, ip_kline_memory);

  sendto_one(cptr, ":%s %d %s :Auth requests %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, auth_requests, auth_requests * sizeof(struct AuthRequest));

  sendto_one(cptr, ":%s %d %s :Classes %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, cl, cl * sizeof(aClass));

  sendto_one(cptr, ":%s %d %s :Listeners %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, listeners, listeners * sizeof(struct Listener));

  sendto_one(cptr, ":%s %d %s :Channels %d(%zd) Bans %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, ch, chm, chb, chbm);
  sendto_one(cptr, ":%s %d %s :Channel members %d(%zd) invite %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, chu, chu * sizeof(Link),
	     chi, chi * sizeof(Link));

  totch = chm + chbm + chu * sizeof(Link) + chi * sizeof(Link);

  sendto_one(cptr, ":%s %d %s :Whowas users %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, wwu, wwu * sizeof(anUser));

  sendto_one(cptr, ":%s %d %s :Whowas array %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick, NICKNAMEHISTORYLENGTH, wwm);

  totww = wwu * sizeof(anUser) + wwm;

  message_files = count_message_file(&ConfigFileEntry.motd) + 
    count_message_file(&ConfigFileEntry.opermotd) + 
    count_message_file(&ConfigFileEntry.helpfile);

  sendto_one(cptr, ":%s %d %s :Message files %zd",
	     me.name, RPL_STATSDEBUG, nick, message_files);

  client_hash_table_size = hash_get_client_table_size();
  channel_hash_table_size = hash_get_channel_table_size();

  sendto_one(cptr, ":%s %d %s :Hash: client %d(%zd) chan %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick,
	     U_MAX, client_hash_table_size, CH_MAX, channel_hash_table_size);

  sendto_one(cptr, ":%s %d %s :Dbuf blocks allocated %zd(%zd), used %zd(%zd)",
	     me.name, RPL_STATSDEBUG, nick, dbuf_alloc_count, dbuf_allocated,
	     dbuf_used_count, dbuf_used);

/*   rm = cres_mem(cptr); */

  count_scache(&number_servers_cached, &mem_servers_cached);

  sendto_one(cptr, ":%s %d %s :scache %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick,
	     number_servers_cached, mem_servers_cached);

  count_ip_hash(&number_ips_stored, &mem_ips_stored);
  sendto_one(cptr, ":%s %d %s :iphash %d(%zd)",
	     me.name, RPL_STATSDEBUG, nick,
	     number_ips_stored, mem_ips_stored);

  tot =
    totww + totch + totcl + com + cl * sizeof(aClass) +
    dbuf_allocated /* + rm */ + unkline_memory + listeners * sizeof(struct Listener) +
    auth_requests * sizeof(struct AuthRequest) + mtrie_memory + message_files +
    dline_memory + ip_kline_memory;
  tot += client_hash_table_size;
  tot += channel_hash_table_size;

  tot += mem_servers_cached;
  sendto_one(cptr, ":%s %d %s :Total: ww %zd ch %zd cl %zd co %zd db %zd",
	     me.name, RPL_STATSDEBUG, nick, totww, totch, totcl, com,
	     dbuf_allocated);


  count_local_client_memory(&local_client_memory_used,
			    &local_client_memory_allocated,
			    &local_client_memory_overheads);
  tot += local_client_memory_allocated + local_client_memory_overheads;
  sendto_one(cptr,
	     ":%s %d %s :Local client Memory in use: %zd Local client Memory allocated: %zd Overheads: %zd",
	     me.name, RPL_STATSDEBUG, nick, local_client_memory_used,
	     local_client_memory_allocated, local_client_memory_overheads);


  count_remote_client_memory(&remote_client_memory_used,
			     &remote_client_memory_allocated,
			     &remote_client_memory_overheads);
  tot += remote_client_memory_allocated + user_memory_overheads;
  sendto_one(cptr,
	     ":%s %d %s :Remote client Memory in use: %zd Remote client Memory allocated: %zd Overheads: %zd",
	     me.name, RPL_STATSDEBUG, nick, remote_client_memory_used,
	     remote_client_memory_allocated, remote_client_memory_overheads);


  count_user_memory(&user_memory_used,
		    &user_memory_allocated,
		    &user_memory_overheads);
  tot += user_memory_allocated + user_memory_overheads;
  sendto_one(cptr,
	     ":%s %d %s :anUser Memory in use: %zd anUser Memory allocated: %zd Overheads: %zd",
	     me.name, RPL_STATSDEBUG, nick, user_memory_used,
	     user_memory_allocated, user_memory_overheads);


  count_links_memory(&links_memory_used,
		     &links_memory_allocated,
		     &links_memory_overheads);
  tot += links_memory_allocated + links_memory_overheads;
  sendto_one(cptr,
	     ":%s %d %s :Links Memory in use: %zd Links Memory allocated: %zd Overheads: %zd",
	     me.name, RPL_STATSDEBUG, nick, links_memory_used,
	     links_memory_allocated, links_memory_overheads);

#ifdef FLUD
  count_flud_memory(&flud_memory_used,
		    &flud_memory_allocated,
		    &flud_memory_overheads);
  tot += flud_memory_allocated + flud_memory_overheads;
  sendto_one(cptr,
	     ":%s %d %s :FLUD Memory in use: %zd FLUD Memory allocated: %zd Overheads: %zd",
	     me.name, RPL_STATSDEBUG, nick, flud_memory_used,
	     flud_memory_allocated, flud_memory_overheads);
#endif

  sendto_one(cptr,
	     ":%s %d %s :TOTAL: %zd Available:  Current max RSS: %zd",
	     me.name, RPL_STATSDEBUG, nick, tot, get_maxrss());

}
