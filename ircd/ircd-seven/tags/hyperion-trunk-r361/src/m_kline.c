/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kline.c
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
 */

#include "m_kline.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "irc_string.h"
#include "ircd.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "struct.h"
#include "hash.h"
#include "umodes.h"
#include "m_commands.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static char no_reason[] = "No Reason";

/* Local function prototypes */
static int isnumber(char *);    /* return 0 if not, else return number */
static char *cluster(char *);

/*
 * Linked list of pending klines that need to be written to
 * the conf
 */
aPendingLine *PendingLines = (aPendingLine *) NULL;

/*
 * LockFile routines
 */
static aPendingLine *AddPending(void);
static void DelPending(aPendingLine *);
static int LockedFile(const char *);
static void WritePendingLines(const char *);
static void WriteKline(const char *, aClient *, aClient *,
                       const char *, const char *, const char *, 
		       time_t until,
                       time_t when);
static void WriteDline(const char *, aClient *,
                       const char *, const char *, const char *);

/*
AddPending()
 Add a pending K/D line to our linked list
*/

static aPendingLine *
AddPending(void)

{
  aPendingLine *temp;

  expect_malloc;
  temp = (aPendingLine *) MyMalloc(sizeof(aPendingLine));
  malloc_log("AddPending() allocating aPendingLine (%zd bytes) at %p",
             sizeof(aPendingLine), (void *)temp);

  /*
   * insert the new entry into our list
   */
  temp->next = PendingLines;
  PendingLines = temp;

  return (temp);
} /* AddPending() */

/*
DelPending()
 Delete pending line entry - assume calling function handles
linked list manipulation (setting next field etc)
*/

static void
DelPending(aPendingLine *pendptr)

{
  if (!pendptr)
    return;

  if (pendptr->user)
    MyFree(pendptr->user);
  MyFree(pendptr->host);
  MyFree(pendptr->reason);
  MyFree(pendptr);
} /* DelPending() */

/*
LockedFile()
 Determine if 'filename' is currently locked. If it is locked,
there should be a filename.lock file which contains the current
pid of the editing process. Make sure the pid is valid before
giving up.

Return: 1 if locked
        0 if not
*/

static int
LockedFile(const char *filename)

{
  char lockpath[PATH_MAX + 1];
  char buffer[1024];
  FBFILE *fileptr;
  int killret;

  if (!filename)
    return (0);

  ircsnprintf(lockpath, PATH_MAX + 1, "%s.lock", filename);

  if ((fileptr = fbopen(lockpath, "r")) == (FBFILE *) NULL)
  {
    /*
     * lockfile does not exist
     */
    return (0);
  }

  if (fbgets(buffer, sizeof(buffer) - 1, fileptr))
  {
    /*
     * If it is a valid lockfile, 'buffer' should now
     * contain the pid number of the editing process.
     * Send the pid a SIGCHLD to see if it is a valid
     * pid - it could be a remnant left over from a
     * crashed editor or system reboot etc.
     */
    killret = kill(atoi(buffer), SIGCHLD);
    if (killret == 0)
    {
      fbclose(fileptr);
      return (1);
    }

    /*
     * killret must be -1, which indicates an error (most
     * likely ESRCH - No such process), so it is ok to
     * proceed writing klines.
     */
  }

  fbclose(fileptr);

  /*
   * Delete the outdated lock file
   */
  unlink(lockpath);

  return (0);
} /* LockedFile() */

static void
WritePendingLines(const char *filename)

{
  aPendingLine *ptmp;

  if (!filename)
    return;

  while (PendingLines)
  {
    if (PendingLines->type == KLINE_TYPE)
    {
      WriteKline(filename,
		 PendingLines->sptr,
		 PendingLines->rcptr,
		 PendingLines->user,
		 PendingLines->host,
		 PendingLines->reason,
		 PendingLines->until,
		 PendingLines->when);
    }
    else
    {
      WriteDline(filename,
        PendingLines->sptr,
        PendingLines->host,
        PendingLines->reason,
        smalldate(PendingLines->when));
    }

    /*
     * Delete the K/D line from the list after we write
     * it out to the conf
     */
    ptmp = PendingLines->next;
    DelPending(PendingLines);
    PendingLines = ptmp;
  } /* while (PendingLines) */
} /* WritePendingLines() */

/*
WriteKline()
 Write out a kline to the kline configuration file
*/

static void
WriteKline(const char *filename, aClient *sptr, aClient *rcptr,
           const char *user, const char *host, const char *reason, 
	   time_t until,
           time_t when)

{
  char buffer[1024];
  int out;

  if (!filename)
    return;

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT, 0644)) == (-1))
    {
      sendto_ops_flag(UMODE_DEBUG, "Error opening %s: %s",
		      filename,
		      strerror(errno));
      return;
    }

  fchmod(out, 0660);

  if (IsServer(sptr))
    {
      if (rcptr)
	{
	  if (IsServer(rcptr))
	    ircsnprintf(buffer, 1024,
			"#%s propagated K%% %s@%s%%%s%%%d\n",
			rcptr->name,
			user,
			host,
			reason,
			when);
	  else
	    ircsnprintf(buffer, 1024,
			"#%s!%s@%s from server %s K'd%% %s@%s%%%s%%%d\n",
			rcptr->name,
			rcptr->username,
			rcptr->host,
			sptr->name,
			user,
			host,
			reason,
			when);
	}
    }
  else
    {
      ircsnprintf(buffer, 1024,
		  "#%s!%s@%s K'd%% %s@%s%%%s%%%d\n",
		  sptr->name,
		  sptr->username,
		  sptr->host,
		  user,
		  host,
		  reason,
		  when);
    }

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  if (until)
    ircsnprintf(buffer, 1024, "K%%%s%%%s%%%s%%%%%%%d%%%d\n",
		host,
		reason,
		user,
		until,
		when);
  else
    ircsnprintf(buffer, 1024, "K%%%s%%%s%%%s%%%%%%%%%d\n",
		host,
		reason,
		user,
		when);

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  (void) close(out);
} /* WriteKline() */

/*
WriteDline()
 Write out a dline to the kline configuration file
*/

static void
WriteDline(const char *filename, aClient *sptr,
           const char *host, const char *reason, const char *when)

{
  char buffer[1024];
  int out;

  if (!filename)
    return;

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT, 0644)) == (-1))
  {
    sendto_ops_flag(UMODE_DEBUG, "Error opening %s: %s",
			   filename,
			   strerror(errno));
    return;
  }

  fchmod(out, 0660);

  ircsnprintf(buffer, 1024, 
	      "#%s!%s@%s D'd%% %s%%%s (%s)\n",
	      sptr->name,
	      sptr->username,
	      sptr->host,
	      host,
	      reason,
	      when);

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  ircsnprintf(buffer, 1024, "D%%%s%%%s (%s)\n",
	      host,
	      reason,
	      when);

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  (void) close(out);
} /* WriteDline() */

/*
 * m_kline()
 *
 * re-worked a tad ... -Dianora
 *
 * -Dianora
 */

int
m_kline(aClient *cptr,
                aClient *sptr,
                int parc,
                char *parv[])
{
  char buffer[1024];
  char *p;
  char cidr_form_host[HOSTLEN + 1];
  char *user, *host;
  char *reason = no_reason;
  const char* current_date;
  int  ip_kline = NO;
  aClient *acptr;
  char tempuser[USERLEN + 2];
  char temphost[HOSTLEN + 1];
  aConfItem *aconf;
  int temporary_kline_time=0;   /* -Dianora */
  time_t temporary_kline_time_seconds=0;
  char *argv;
  unsigned long ip;
  unsigned long ip_mask;
  const char *kconf; /* kline conf file */
  register char tmpch;
  register int nonwild;
  aClient *rcptr=NULL;
  static char star[] = "*";

  char *slave_oper = NULL;


  if(IsServer(sptr))
    {
      if(parc < 2)      /* pick up actual oper who placed kline */
        return 0;

      slave_oper = parv[1];     /* make it look like normal local kline */

      parc--;
      parv++;

      if ( parc < 2 )
        return 0;

      if ((rcptr = hash_find_client(slave_oper,(aClient *)NULL)))
        {
	  /* Servers can send out KLINEs now, when they need to propagate a local
	   * KLINE to a remote server which appears to be missing it
	   *  -- asuffield
	   */
          if(!IsPerson(rcptr) && !IsServer(rcptr))
	    return 0;
        }
      else
        return 0;
      sendto_slaves(sptr, "KLINE", slave_oper, parc, parv);
    }
  else
    {
      if (!MyClient(sptr) || !HasUmode(sptr,UMODE_KILL))
        {
	  if (SeesOperMessages(sptr))
	    sendto_one(sptr,":%s NOTICE %s :You have no K umode",me.name,parv[0]);
	  else
	    sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
          return 0;
        }

      if ( parc < 2 )
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                     me.name, parv[0], "KLINE");
          return 0;
        }

      sendto_slaves(NULL, "KLINE", sptr->name, parc, parv);
    }

  argv = parv[1];

  if( (temporary_kline_time = isnumber(argv)) )
    {
      if(parc < 3)
        {
          if(!IsServer(sptr))
             sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                        me.name, parv[0], "KLINE");
          return 0;
        }
      /*       if(temporary_kline_time > (24*60)) */
      /*         temporary_kline_time = (24*60);*/ /* Max it at 24 hours */ /* No, don't -- asuffield */
      temporary_kline_time_seconds = (time_t)temporary_kline_time * (time_t)60;
      /* turn it into minutes */
      /* turn it into seconds, you mean. it *was* in minutes. -- asuffield */
      argv = parv[2];
      parc--;
    }

  if ( (host = strchr(argv, '@')) || *argv == '*' )
    {
      /* Explicit user@host mask given */

      if(host)                  /* Found user@host */
        {
          user = argv;  /* here is user part */
          *(host++) = '\0';     /* and now here is host */
        }
      else
        {
          user = star;           /* no @ found, assume its *@somehost */
          host = argv;
        }

      if (!*host)               /* duh. no host found, assume its '*' host */
        host = star;
      strncpy_irc(tempuser, user, USERLEN + 1); /* allow for '*' in front */
      tempuser[USERLEN + 1] = '\0';
      strncpy_irc(temphost, host, HOSTLEN + 1);
      temphost[HOSTLEN] = '\0';
      user = tempuser;
      host = temphost;
    }
  else
    {
      /* Try to find user@host mask from nick */

      if (!(acptr = find_chasing(sptr, argv, NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
          if(!IsServer(sptr))
            sendto_one(sptr,
              ":%s NOTICE %s :Can't KLINE a server, use @'s where appropriate",
                       me.name, parv[0]);
          return 0;
        }

      if(IsElined(acptr))
        {
          if(!IsServer(sptr))
            sendto_one(sptr,
                       ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                       acptr->name);
          return 0;
        }

      /* turn the "user" bit into "*user", blow away '~'
         if found in original user name (non-idented) */

      tempuser[0] = '*';
      if (*acptr->username == '~')
        strncpy_irc(tempuser+1, (char *)acptr->username+1, USERLEN + 1 - 1);
      else
        strncpy_irc(tempuser+1, acptr->username, USERLEN + 1 - 1);
      user = tempuser;
      host = cluster(acptr->host);
    }

  if(temporary_kline_time)
    argv = parv[3];
  else
    argv = parv[2];

  if (parc > 2) 
    {
      if(strchr(argv, ':'))
        {
          if(!IsServer(sptr))
            sendto_one(sptr,
                       ":%s NOTICE %s :Invalid character ':' in comment",
                       me.name, parv[0]);
          return 0;
        }

      if (strchr(argv, ';') && (rcptr == NULL || IsClient(rcptr)))
	{
	  sendto_one(sptr, ":%s NOTICE %s :Invalid character ';' in comment",
		     me.name, sptr->name);
	  return 0;
	}

      if(strchr(argv, '#'))
        {
          if(!IsServer(sptr))
            sendto_one(sptr,
                       ":%s NOTICE %s :Invalid character '#' in comment",
                       me.name, parv[0]);
          return 0;
        }

      if(*argv)
        reason = argv;
    }

  /*
   * Check for # in user@host
   */

  if(strchr(host, '#'))
    {
      if(!IsServer(sptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in hostname",
                   me.name, parv[0]);
      return 0;
    }
  if(strchr(user, '#'))
    { 
      if(!IsServer(sptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in username",
                   me.name, parv[0]);
      return 0;
    }   

  /*
   * Also check for ! -- jilles
   */

  if(strchr(user, '!') || strchr(host, '!'))
    { 
      if(!IsServer(sptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '!' -- K:lines do not include nicks",
                   me.name, parv[0]);
      return 0;
    }   

  /*
   * Now we must check the user and host to make sure there
   * are at least NONWILDCHARS non-wildcard characters in
   * them, otherwise assume they are attempting to kline
   * *@* or some variant of that. This code will also catch
   * people attempting to kline *@*.tld, as long as NONWILDCHARS
   * is greater than 3. In that case, there are only 3 non-wild
   * characters (tld), so if NONWILDCHARS is 4, the kline will
   * be disallowed.
   * -wnder
   */

  nonwild = 0;
  p = user;
  while ((tmpch = *p++))
  {
    if (!IsKWildChar(tmpch))
    {
      /*
       * If we find enough non-wild characters, we can
       * break - no point in searching further.
       */
      if (++nonwild >= NONWILDCHARS)
        break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * The user portion did not contain enough non-wild
     * characters, try the host.
     */
    p = host;
    while ((tmpch = *p++))
    {
      if (!IsKWildChar(tmpch))
        if (++nonwild >= NONWILDCHARS)
          break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * Not enough non-wild characters were found, assume
     * they are trying to kline *@*.
     */
    if (!IsServer(sptr))
      sendto_one(sptr,
        ":%s NOTICE %s :Please include at least %d non-wildcard characters with the user@host",
        me.name,
        parv[0],
        NONWILDCHARS);

    return 0;
  }

  /* 
   * At this point, I know the user and the host to place the k-line on
   * I also know whether its supposed to be a temporary kline or not
   * I also know the reason field is clean
   * Now what I want to do, is find out if its a kline of the form
   *
   * /quote kline *@192.168.0.* i.e. it should be turned into a d-line instead
   *
   */


  if((ip_kline = is_address(host, &ip, &ip_mask)))
     {
       /*
        * XXX - ack
        */
       strncpy_irc(cidr_form_host, host, HOSTLEN + 1);
       cidr_form_host[32] = '\0';
       p = strchr(cidr_form_host,'*');
       if (p)
         {
           *p++ = '0';
           *p++ = '/';
           *p++ = '2';
           *p++ = '4';
           *p++ = '\0';
         }
       host = cidr_form_host;
    }
  else
    {
      ip = 0L;
    }

  /* Clear out the UNKLINES cache of any matching entries */
  {
    char mask[HOSTLEN+USERLEN+2];
    struct unkline_record **ukr, *ukr2;

    ircsnprintf(mask, HOSTLEN + USERLEN + 2, "%s@%s", user, host);
    for (ukr = &recorded_unklines; (ukr2 = *ukr); ukr = &ukr2->next)
      {
	if (match(ukr2->mask, mask))
	  {
	    *ukr = ukr2->next;
	    MyFree(ukr2->mask);
	    MyFree(ukr2);
	    /* And put stuff back, safety in case we can't loop again */
	    if (!(ukr2 = *ukr))
	      break;
	  }
      }
  }

  if( (aconf = is_klined(host,user,(unsigned long)ip)) )
     {
       char *nrkreason;

       if( aconf->status & CONF_KILL )
         {
           nrkreason = aconf->passwd ? aconf->passwd : no_reason;
           if(!IsServer(sptr))
             sendto_one(sptr,
                        ":%s NOTICE %s :[%s@%s] already K-lined by [%s@%s] - %s",
                        me.name,
                        parv[0],
                        user,host,
                        aconf->user,aconf->host,nrkreason);
           return 0;
         }
     }

  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host, host);

  DupString(aconf->user, user);
  aconf->port = 0;

  aconf->hold = temporary_kline_time_seconds ? (CurrentTime + temporary_kline_time_seconds) : 0;
  if(ip_kline)
    {
      aconf->ip = ip;
      aconf->ip_mask = ip_mask;
    }

  /* This is a K:line being propagated, retain the old reason */
  if (strchr(reason, ';') && IsServer(sptr))
    {
      DupString(aconf->passwd, reason);
    }
  else
    {
      char *p;
      p = strchr(reason, '|');
      if (p != NULL)
        {
	  /* the date and time the kline was set does not belong
	   * in the oper reason -- jilles */
	  *p = '\0';
          ircsnprintf(buffer, 512, "%s; %s (%s)|%s",
                      rcptr ? rcptr->name : sptr->name,
                      reason, current_date, p + 1);
	  *p = '|';
	}
      else
        {
          ircsnprintf(buffer, 512, "%s; %s (%s)",
                      rcptr ? rcptr->name : sptr->name,
                      reason, current_date);
	}
      DupString(aconf->passwd, buffer);
    }
  ClassPtr(aconf) = find_class(0);

  if(ip_kline)
    {
      aconf->ip = ip;
      aconf->ip_mask = ip_mask;
      add_ip_Kline(aconf);
    }
  else
    {
      int found = 0;
      struct ConfItem *aconf2;
      add_mtrie_conf_entry(aconf,CONF_KILL);
      for (aconf2 = kline_list; aconf2; aconf2 = aconf2->kline_next)
	if (aconf2 == aconf)
	  {
	    found = 1;
	    break;
	  }
      if (!found)
	{
	  aconf->kline_next = kline_list;
	  kline_list = aconf;
	  kline_count++;
	}
      else
	{
	  sendto_ops_flag(UMODE_DEBUG, "WTF: Tried to add aconf for %s@%s to kline_list twice",
			  aconf->user, aconf->host);
	  logprintf(L_WARN, "WTF: Tried to add aconf for %s@%s to kline_list twice",
	      aconf->user, aconf->host);
	}
    }

  if(temporary_kline_time)
    sendto_local_ops_flag(UMODE_SERVNOTICE, "%s added K-Line for [%s@%s] [%s], expiring at %.1ld (%d minutes)",
			  rcptr ? rcptr->name : sptr->name,
			  user,
			  host,
			  reason,
			  aconf->hold, temporary_kline_time);
  else
    sendto_local_ops_flag(UMODE_SERVNOTICE, "%s added K-Line for [%s@%s] [%s]",
			  rcptr ? rcptr->name : sptr->name,
			  user,
			  host,
			  reason);

  logprintf(L_TRACE, "%s added K-Line for [%s@%s] [%s]",
      rcptr ? rcptr->name : sptr->name, user, host, reason);

  kconf = get_conf_name(KLINE_TYPE);

  /*
   * Check if the conf file is locked - if so, add the kline
   * to our pending kline list, to be written later, if not,
   * allow this kline to be written, and write out all other
   * pending klines as well
   */
  if (LockedFile(kconf))
  {
    aPendingLine *pptr;

    pptr = AddPending();

    /*
     * Now fill in the fields
     */
    pptr->type = KLINE_TYPE;
    pptr->sptr = sptr;
    DupString(pptr->user, user);
    DupString(pptr->host, host);
    DupString(pptr->reason, aconf->passwd);
    pptr->when = CurrentTime;

    pptr->rcptr = rcptr;

    pptr->until = aconf->hold;

    if (IsClient(sptr))
      sendto_one(sptr,
		 ":%s NOTICE %s :Added K-Line [%s@%s] (config file write delayed)",
		 me.name,
		 sptr->name,
		 user,
		 host);

    return 0;
  }
  else if (PendingLines)
    WritePendingLines(kconf);

  if (IsClient(sptr))
    sendto_one(sptr,
	       ":%s NOTICE %s :Added K-Line [%s@%s] to %s",
	       me.name,
	       sptr->name,
	       user,
	       host,
	       kconf ? kconf : "configuration file");

  WriteKline(kconf,
	     sptr,
	     rcptr,
	     user,
	     host,
	     aconf->passwd,
	     aconf->hold,
	     CurrentTime);

  rehashed = YES;
  dline_in_progress = NO;
  nextping = CurrentTime;
  return 0;
} /* m_kline() */

/*
 * isnumber()
 * 
 * inputs       - pointer to ascii string in
 * output       - 0 if not an integer number, else the number
 * side effects - none
*/

static int isnumber(char *p)
{
  int result = 0;

  while(*p)
    {
      if(IsDigit(*p))
        {
          result *= 10;
          result += ((*p) & 0xF);
          p++;
        }
      else
        return(0);
    }
  /* in the degenerate case where oper does a /quote kline 0 user@host :reason 
     i.e. they specifically use 0, I am going to return 1 instead
     as a return value of non-zero is used to flag it as a temporary kline
  */

  /* pest. removed.
   *  -- asuffield
   */

/*   if(result == 0) */
/*     result = 1; */
  return(result);
}

/*
 * cluster()
 *
 * input        - pointer to a hostname
 * output       - pointer to a static of the hostname masked
 *                for use in a kline.
 * side effects - NONE
 *
 * reworked a tad -Dianora
 */

static char *cluster(char *hostname)
{
  static char result[HOSTLEN + 1];      /* result to return */
  char        temphost[HOSTLEN + 1];    /* work place */
  char        *ipp;             /* used to find if host is ip # only */
  char        *host_mask;       /* used to find host mask portion to '*' */
  char        *zap_point = NULL; /* used to zap last nnn portion of an ip # */
  char        *tld;             /* Top Level Domain */
  int         is_ip_number;     /* flag if its an ip # */             
  int         number_of_dots;   /* count number of dots for both ip# and
                                   domain klines */
  if (!hostname)
    return (char *) NULL;       /* EEK! */

  /* If a '@' is found in the hostname, this is bogus
   * and must have been introduced by server that doesn't
   * check for bogus domains (dns spoof) very well. *sigh* just return it...
   * I could also legitimately return (char *)NULL as above.
   *
   * -Dianora
   */

  if(strchr(hostname,'@'))      
    {
      strncpy_irc(result, hostname, HOSTLEN + 1);
      result[HOSTLEN] = '\0';
      return(result);
    }

  strncpy_irc(temphost, hostname, HOSTLEN + 1);
  temphost[HOSTLEN] = '\0';

  is_ip_number = YES;   /* assume its an IP# */
  ipp = temphost;
  number_of_dots = 0;

  while (*ipp)
    {
      if( *ipp == '.' )
        {
          number_of_dots++;

          if(number_of_dots == 3)
            zap_point = ipp;
          ipp++;
        }
      else if(!IsDigit(*ipp))
        {
          is_ip_number = NO;
          break;
        }
      ipp++;
    }

  if(is_ip_number && (number_of_dots == 3))
    {
      zap_point++;
      *zap_point++ = '*';               /* turn 111.222.333.444 into */
      *zap_point = '\0';                /*      111.222.333.*        */
      strncpy_irc(result, temphost, HOSTLEN + 1);
      result[HOSTLEN] = '\0';
      return(result);
    }
  else
    {
      tld = strrchr(temphost, '.');
      if(tld)
        {
          number_of_dots = 2;
          if(tld[3])                     /* its at least a 3 letter tld
                                            i.e. ".com" tld[3] = 'm' not 
                                            '\0' */
                                         /* 4 letter tld's are coming */
            number_of_dots = 1;

          if(tld != temphost)           /* in these days of dns spoofers ...*/
            host_mask = tld - 1;        /* Look for host portion to '*' */
          else
            host_mask = tld;            /* degenerate case hostname is
                                           '.com' etc. */

          while (host_mask != temphost)
            {
              if(*host_mask == '.')
                number_of_dots--;
              if(number_of_dots == 0)
                {
                  result[0] = '*';
                  strncpy_irc(result + 1, host_mask, HOSTLEN + 1 - 1);
                  result[HOSTLEN] = '\0';
                  return(result);
                }
              host_mask--;
            }
          result[0] = '*';                      /* foo.com => *foo.com */
          strncpy_irc(result + 1, temphost, HOSTLEN + 1 - 1);
          result[HOSTLEN] = '\0';
        }
      else      /* no tld found oops. just return it as is */
        {
          strncpy_irc(result, temphost, HOSTLEN + 1);
          result[HOSTLEN] = '\0';
          return(result);
        }
    }

  return (result);
}

/*
 * re-worked a tad
 * added Rodders dated KLINE code
 * -Dianora
 *
 */

int
m_dline(aClient *cptr, aClient *sptr, int parc, char *parv[])

{
  char *host, *reason;
  char *p;
  aClient *acptr;
  char cidr_form_host[HOSTLEN + 1];
  unsigned long ip_host;
  unsigned long ip_mask;
  aConfItem *aconf;
  char buffer[1024];
  const char* current_date;
  const char *dconf;

  if (!MyClient(sptr) || !HasUmode(sptr,UMODE_KILL))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no K umode",me.name,parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "DLINE");
      return 0;
    }

  host = parv[1];
  strncpy_irc(cidr_form_host, host, HOSTLEN + 1);
  cidr_form_host[32] = '\0';

  if((p = strchr(cidr_form_host,'*')))
    {
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      host = cidr_form_host;
    }

  if(!is_address(host,&ip_host,&ip_mask))
    {
      if (!(acptr = find_chasing(sptr, parv[1], NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Can't DLINE a server silly",
                     me.name, parv[0]);
          return 0;
        }

      if (strchr(acptr->sockhost, ':') != NULL)
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :IPv6 DLINE is not supported at this time",
                     me.name, parv[0]);
          return 0;
        }
              
      if(!MyConnect(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE :%s :Can't DLINE nick on another server",
                     me.name, parv[0]);
          return 0;
        }

      if(IsElined(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                     acptr->name);
          return 0;
        }

      /*
       * XXX - this is always a fixed length output, we can get away
       * with strcpy here
       *
       * strncpy_irc(cidr_form_host, inetntoa((char *)&acptr->ip), 32);
       * cidr_form_host[32] = '\0';
       */
      strcpy(cidr_form_host, acptr->sockhost);
      
      host = cidr_form_host;

      ip_mask = 0xFFFFFFFFL;
      ip_host = get_ipv4_ip(&acptr->ip);
      ip_host &= ip_mask;
    }

  if (parc > 2) /* host :reason */
    {
      if(strchr(parv[2], ':'))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Invalid character ':' in comment",
                     me.name, parv[0]);
          return 0;
        }

      if(strchr(parv[2], '#'))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Invalid character '#' in comment",
                     me.name, parv[0]);
          return 0;
        }

      if(*parv[2])
        reason = parv[2];
      else
        reason = no_reason;
    }
  else
    reason = no_reason;


  if((ip_mask & 0xFFFFFF00) ^ 0xFFFFFF00)
    {
      if(ip_mask != 0xFFFFFFFF)
        {
          sendto_one(sptr, ":%s NOTICE %s :Can't use a mask less than 24 with dline",
                     me.name,
                     parv[0]);
          return 0;
        }
    }

  if(((aconf = match_Dline(ip_host)) || (aconf = find_Dline_exception(ip_host)))
		  && (aconf->ip_mask & ~ip_mask) == 0)
     {
       char *nrkreason;
       nrkreason = aconf->passwd ? aconf->passwd : no_reason;
       if(IsConfElined(aconf))
         sendto_one(sptr, ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
                    me.name,
                    parv[0],
                    host,
                    aconf->host,nrkreason);
         else
           sendto_one(sptr, ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
                      me.name,
                      parv[0],
                      host,
                      aconf->host,nrkreason);
      return 0;
       
     }

  current_date = smalldate((time_t) 0);

  ircsnprintf(buffer, 512, "%s (%s)",reason,current_date);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,host);
  DupString(aconf->passwd,buffer);

  aconf->ip = ip_host;
  aconf->ip_mask = ip_mask;

  add_Dline(aconf);

  sendto_ops_flag(UMODE_SERVNOTICE, "%s added D-Line for [%s] [%s]",
			 sptr->name,
			 host,
			 reason);

        logprintf(L_TRACE, "%s added D-Line for [%s] [%s]", 
            sptr->name, host, reason);

        dconf = get_conf_name(DLINE_TYPE);

        /*
         * Check if the conf file is locked - if so, add the dline
         * to our pending dline list, to be written later, if not,
         * allow this dline to be written, and write out all other
         * pending lines as well
         */
        if (LockedFile(dconf))
        {
                aPendingLine *pptr;

                pptr = AddPending();

                /*
                 * Now fill in the fields
                 */
                pptr->type = DLINE_TYPE;
                pptr->sptr = sptr;
                pptr->rcptr = (aClient *) NULL;
                pptr->user = (char *) NULL;
                pptr->host = strdup(host);
                pptr->reason = strdup(reason);
                pptr->when = CurrentTime;

                sendto_one(sptr,
                        ":%s NOTICE %s :Added D-Line [%s] (config file write delayed)",
                        me.name,
                        sptr->name,
                        host);

                return 0;
        }
        else if (PendingLines)
                WritePendingLines(dconf);

        sendto_one(sptr,
                ":%s NOTICE %s :Added D-Line [%s] to %s",
                me.name,
                sptr->name,
                host,
                dconf ? dconf : "configuration file");

        /*
         * Write dline to configuration file
         */
        WriteDline(dconf,
                sptr,
                host,
                reason,
                current_date);

  /*
  ** I moved the following 2 lines up here
  ** because we still want the server to
  ** hunt for 'targetted' clients even if
  ** there are problems adding the D-line
  ** to the appropriate file. -ThemBones
  */
  rehashed = YES;
  dline_in_progress = YES;
  nextping = CurrentTime;
  return 0;
} /* m_dline() */

/* This is a hacked up version of m_dline, for automated dlines */
int
dline_client(struct Client *acptr, const char *reason)
{
  unsigned long ip_host;
  unsigned long ip_mask;
  char host[HOSTLEN + 1];
  const char *dconf;
  struct ConfItem *aconf;
  const char *current_date;

  /* IPv6? throw it away for now instead of setting bad dlines
   * yes I know this function is only called for local clients
   * so this can be under #define IPV6 but this doesn't use any
   * special functions so it is not necessary
   * -- jilles */
  if (strchr(acptr->sockhost, ':') != NULL)
    return 0;

  if (IsElined(acptr))
    return 0;

  ip_mask = 0xFFFFFFFFL;
#ifdef IPV6
  ip_host = get_ipv4_ip(&acptr->ip);
#else
  ip_host = ntohl(acptr->ip.s_addr);
#endif

  if(((aconf = match_Dline(ip_host)) || (aconf = find_Dline_exception(ip_host)))
		  && (aconf->ip_mask & ~ip_mask) == 0)
    return 0; /* d:lined or already D:lined */

  strcpy(host, acptr->sockhost);

  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,host);
  DupString(aconf->passwd,reason);

  aconf->ip = ip_host;
  aconf->ip_mask = ip_mask;

  add_Dline(aconf);

  sendto_ops_flag(UMODE_SERVNOTICE, "added D-Line for [%s] [%s]",
                  host, reason);

  logprintf(L_TRACE, "added D-Line for [%s] [%s]", 
            host, reason);

  dconf = get_conf_name(DLINE_TYPE);

  /*
   * Check if the conf file is locked - if so, add the dline
   * to our pending dline list, to be written later, if not,
   * allow this dline to be written, and write out all other
   * pending lines as well
   */
  if (LockedFile(dconf))
    {
      aPendingLine *pptr;

      pptr = AddPending();

      /*
       * Now fill in the fields
       */
      pptr->type = DLINE_TYPE;
      pptr->sptr = &me;
      pptr->rcptr = (aClient *) NULL;
      pptr->user = (char *) NULL;
      pptr->host = strdup(host);
      pptr->reason = strdup(reason);
      pptr->when = CurrentTime;

      return 0;
    }
  else if (PendingLines)
    WritePendingLines(dconf);

  /*
   * Write dline to configuration file
   */
  WriteDline(dconf,
             &me,
             host,
             reason,
             current_date);

  rehashed = YES;
  dline_in_progress = YES;
  nextping = CurrentTime;

  return 0;
}
