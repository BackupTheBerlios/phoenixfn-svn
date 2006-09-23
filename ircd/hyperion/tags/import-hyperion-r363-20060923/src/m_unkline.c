/************************************************************************
 *   IRC - Internet Relay Chat, src/m_unkline.c
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
 *   
 */
#include "m_commands.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "fileio.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "struct.h"
#include "umodes.h"
#include "m_kline.h"

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

struct unkline_record *recorded_unklines = NULL;

/*
 * flush_write()
 *
 * inputs       - out is the file descriptor
 *              - buf is the buffer to write
 *              - ntowrite is the expected number of character to be written
 *              - temppath is the temporary file name to be written
 * output       - true for success
 *              - false for error
 * side effects - if successful, the buf is written to output file
 *                if a write failure happesn, and the file pointed to
 *                by temppath, if its non NULL, is removed.
 */

static int flush_write(FBFILE* out, char *buf, char *temppath)
{
  if (fbputs(buf, out) < 0)
    {
      fbclose(out);
      if(temppath)
        unlink(temppath);
      return 0;
    }
  return 1;
}

static int unkline_in_cache(char *mask)
{
  struct unkline_record *ukr;
  for (ukr = recorded_unklines; ukr; ukr = ukr->next)
    if (ukr->mask && !irccmp(mask, ukr->mask))
      return 1;
  return 0;
}

static int
process_unkline(const char *user, const char *host, time_t placed, int cache, int *found)
{
  FBFILE* in;
  FBFILE* out;
  char  temppath[BUFSIZE];
  char  buf[BUFSIZE];
  char  buff[BUFSIZE];  /* matches line definition in s_conf.c */
  const char  *filename;                /* filename to use for unkline */
  char  *p;
  mode_t oldumask;

  int store_in_cache = cache;

  ircsnprintf(temppath, BUFSIZE, "%s/%s.tmp", ConfigFileEntry.dpath,
	      ConfigFileEntry.klinefile);

  filename = get_conf_name(KLINE_TYPE);

  if( (in = fbopen(filename, "r")) == 0)
    return 0;

  oldumask = umask(0);                  /* ircd is normally too paranoid */
  if( (out = fbopen(temppath, "w")) == 0)
    {
      fbclose(in);
      umask(oldumask);                  /* Restore the old umask */
      return 0;
    }
  umask(oldumask);                    /* Restore the old umask */

  while(fbgets(buf, sizeof(buf), in)) 
    {
      if((buf[1] == ':') && ((buf[0] == 'k') || (buf[0] == 'K')))
        {
          /* its a K: line */
          char *found_host;
          char *found_user;
          char *found_comment;
	  time_t found_until = 0, found_when = 0;

          strncpy_irc(buff, buf, BUFSIZE);      /* extra paranoia */

          p = strchr(buff,'\n');
          if(p)
            *p = '\0';

          found_host = buff + 2;        /* point past the K: */
	  if (*found_host == '[')
	    {
	      found_host++;
	      p = strchr(found_host, ']');
	      if (p != NULL)
	        *p = '\0', p++;
	    }
	  else
	    p = strchr(found_host, ':');
          if(p == NULL)
            {
              logprintf(L_ERROR, "K-Line file corrupted (couldn't find host)");
              if (!flush_write(out, buf, temppath))
                return 0;
              continue;         /* This K line is corrupted ignore */
            }
          *p = '\0';
          p++;
 
          found_comment = p;
          p = strchr(found_comment,':');
          if(p == (char *)NULL)
            {
              logprintf(L_ERROR, "K-Line file corrupted (couldn't find comment)");
              if (!flush_write(out, buf, temppath))
                return 0;
              continue;         /* This K line is corrupted ignore */
            }
          *p = '\0';
          p++;
          found_user = p;

	  /* If I have more fields, clip here */
          p = strchr(found_user,':');
	  if (p)
	    {
	      int i;
	      *p = '\0';
	      /* And go find the time it was placed */
	      for (i=0; p && (i < 2); i++, p = strchr(p+1, ':'));
	      if (p && p[1])
		{
		  found_until = atoi(p+1);
		  p = strchr(p+1, ':');
		  found_when = atoi(p+1);
		}
	    }

          /*
           * Ok, if its not an exact match on either the user or the host
           * then, write the K: line out, and I add it back to the K line
           * tree
           *
           * Or if it's newer than the UNKLINE. If it matches but is newer, 
           * record the fact that this has happened. We want to remove any 
           * KLINEs which are older, but not store the UNKLINE in the cache.
           *
           * Don't write it out if it expired 
           */

	  {
	    int newer = (found_when > placed);
	    /* permanent K:lines (found_until == 0) are never ancient */
	    int ancient = found_until != 0 &&
		    (found_until + EXPIRED_KLINE_DELAY) < CurrentTime;
	    int match = 0;
            if (user && host)
              {
                if (irccmp(user, found_user) == 0 && irccmp(host, found_host) == 0)
                  match = 1;
              }
	    if(match & newer)
	      store_in_cache = 0;

	    if((!match || newer) && !ancient)
	      {
		/* Keep this one in the file */
                if (!flush_write(out, buf, temppath))
                  return 0;
	      }
	    else if (found)
	      *found = 1;
	  }
        }                               
      else if(buf[0] == '#')
        {
          char *userathost;
          char *found_user;
          char *found_host;
	  time_t found_when;

          strncpy_irc(buff, buf, BUFSIZE);

          p = strchr(buff,':');
          if(p == (char *)NULL)
            {
              if (!flush_write(out, buf, temppath))
                return 0;
              continue;
            }
          *p = '\0';
          p++;

	  userathost = p;
	  p = strchr(userathost, '@');
	  if (p == NULL)
	    p = userathost;
	  else
	    p++;
	  if (*p == '[')
	    {
	      p = strchr(p, ']');
	      if (p != NULL)
	        *p = '\0', p++;
	    }
	  else
	    p = strchr(p, ':');

          if(p == NULL)
            {
              if (!flush_write(out, buf, temppath))
                return 0;
              continue;
            }
          *p = '\0';

	  /* Go find the time it was set */
	  p = strrchr(buff, ':');
	  if (p && p[1])
	    found_when = atoi(p+1);
	  else
	    found_when = 0;

          while(*userathost == ' ')
            userathost++;

          found_user = userathost;
          p = strchr(found_user,'@');
          if(p == (char *)NULL)
            {
              if (!flush_write(out, buf, temppath))
                return 0;
              continue;
            }
          *p = '\0';
          found_host = p;
          found_host++;
	  if (*found_host == '[')
	    {
	      found_host++;
	      p = strchr(found_host, ']');
	      if (p != NULL)
	        *p = '\0';
	    }

          if((found_when > CurrentTime)
             || !user || !host
             || irccmp(found_user,user) != 0
             || irccmp(found_host,host) != 0)
            {
              if (!flush_write(out, buf, temppath))
                return 0;
            }
        }
      else      /* its the ircd.conf file, and not a K line or comment */
        {
          if (!flush_write(out, buf, temppath))
            return 0;
        }
    }

  fbclose(in);
  fbclose(out);

  if (store_in_cache)
    {
      char buf[BUFSIZE];
      ircsnprintf(buf, BUFSIZE, "%s@%s", user, host);
      if (!unkline_in_cache(buf))
	{
	  /* OK, store the unklines to defend against netsplits not
	   * propagating them strongly enough 
	   */
	  struct unkline_record *ukr;
          size_t len;
          expect_malloc;
          ukr = MyMalloc(sizeof(struct unkline_record));
          malloc_log("m_unkline() allocating struct unkline_record for %s@%s (%zd bytes) at %p",
                     user, host, sizeof(struct unkline_record), (void *)ukr);
	  len = strlen(host) + strlen(user) + 2;
          expect_malloc;
	  ukr->mask = MyMalloc(len);
          malloc_log("m_unkline() allocating %d bytes for unkline mask (%zd bytes) at %p",
                     len, len, ukr->mask);
	  ircsnprintf(ukr->mask, len, "%s@%s", user, host);
	  ukr->placed = placed;
	  ukr->next = recorded_unklines;
	  recorded_unklines = ukr;
	}
    }

  if (rename(temppath, filename) != 0)
    {
      unlink(temppath);
      return 0;
    }
  return 1;
}

int
unkline_conf(struct ConfItem *aconf)
{
  /* And it's expired. Remove it and return the I:line instead */

  if ((strlen(aconf->user) == 0) || (strlen(aconf->host) == 0))
    return 0;

  sendto_local_ops_flag(UMODE_SERVNOTICE, "Expiring K:line on %s@%s", aconf->user, aconf->host);

  if (!process_unkline(aconf->user, aconf->host, CurrentTime, 0, NULL))
    {
      sendto_ops_flag(UMODE_SERVNOTICE, "Cannot remove timed K:line for %s@%s. Rejecting connection instead", aconf->user, aconf->host);
      return 0;
    }
  return 1;
}

int
expire_ancient_klines(void)
{
  if (!process_unkline(NULL, NULL, CurrentTime, 0, NULL))
    {
      sendto_ops_flag(UMODE_SERVNOTICE, "Failed to expire ancient klines");
      return 0;
    }
  return 1;
}

/*
 * m_unkline
 *      parv[1] = address to remove
 */
int
m_unkline (aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  int found = 0;
  const char *user;
  char *host;
  time_t placed = 0;

  if (!IsMe(sptr) && check_registered(sptr))
    {
      return -1;
    }

  if (!(IsMe(sptr) || IsServer(sptr) || HasUmode(sptr,UMODE_UNKLINE)))  
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no U umode",me.name,parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, 
		   parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                   me.name, parv[0], "UNKLINE");
      return -1;
    }

  if ( (host = strchr(parv[1], '@')) || *parv[1] == '*' )
    {
      /* Explicit user@host mask given */

      if(host)                  /* Found user@host */
        {
          user = parv[1];       /* here is user part */
          *(host++) = '\0';     /* and now here is host */
        }
      else
        {
          user = "*";           /* no @ found, assume its *@somehost */
          host = parv[1];
        }
    }
  else
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid parameters",
                   me.name, parv[0]);
      return -1;
    }

  if( (user[0] == '*') && (user[1] == '\0')
      && (host[0] == '*') && (host[1] == '\0') )
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        sendto_one(sptr, ":%s NOTICE %s :Cannot UNK-Line everyone",
                   me.name, parv[0]);
      return -1;
    }

  if (IsServer(sptr) && (parc > 2) && !EmptyString(parv[2]))
    placed = atoi(parv[2]);
  else
    placed = CurrentTime;

  sendto_serv_butone(cptr, ":%s UNKLINE %s@%s %ld", parv[0], user, host,
		  (unsigned long)placed);

  if (!process_unkline(user, host, placed, 1, &found))
    {
      if (!IsServer(sptr) && !IsMe(cptr))
        sendto_one(sptr,":%s NOTICE %s :Failed to update kline file, aborted",
                   me.name,parv[0]);
      return -1;
    }

  if(!found)
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        sendto_one(sptr, ":%s NOTICE %s :No K-Line for %s@%s",
                   me.name, parv[0],user,host);
      /* This is not an error. The invariant "user@host is not K:lined" is true at this point */
      return 0;
    }

  if (!IsServer(cptr) && !IsMe(cptr))
    sendto_one(sptr, ":%s NOTICE %s :K-Line for [%s@%s] is removed", 
               me.name, parv[0], user,host);
  /* If it's a user, don't broadcast, everybody got one */
  /* If it's a server and I had a "placed" argument, this is the netburst 
   *  UNKLINE propagation, better warn
   * If placed == CurrentTime, this almost certainly is not propagated
   *  -- asuffield
   */
  if (IsServer(sptr) && (placed != CurrentTime))
    sendto_ops_flag(UMODE_SERVNOTICE, "%s has removed the K-Line for: [%s@%s]",
                    parv[0], user, host);
  else
    if (!IsMe(sptr)) /* Don't show _myself_ expiring a K:line */
      sendto_local_ops_flag(UMODE_SERVNOTICE, "%s has removed the K-Line for: [%s@%s]",
			    parv[0], user, host);

  logprintf(L_NOTICE, "%s removed K-Line for [%s@%s]", parv[0], user, host);
  return 0; 
}
