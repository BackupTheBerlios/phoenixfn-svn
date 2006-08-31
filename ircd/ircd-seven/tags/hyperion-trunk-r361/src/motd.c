/************************************************************************
 *   IRC - Internet Relay Chat, src/motd.c
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
#include "motd.h"
#include "ircd.h"
#include "fileio.h"
#include "res.h"
#include "s_conf.h"
#include "class.h"
#include "send.h"
#include "s_conf.h"
#include "numeric.h"
#include "client.h"
#include "irc_string.h"
#include "s_serv.h"     /* hunt_server */
#include "umodes.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

/*
** m_motd
**      parv[0] = sender prefix
**      parv[1] = servername
*/
int m_motd(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  static time_t last_used = 0;

  /* uhh.. servers should not send a motd request.. */
  if(IsServer(sptr))
      return 0;

  if(!NoFloodProtection(sptr))
    {
      if(IsHoneypot(sptr) || (last_used + PACE_WAIT) > CurrentTime)
        {
          /* safe enough to give this on a local connect only */
          if(MyClient(sptr))
	    {
              sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,sptr->name,"MOTD");
              sendto_one(sptr,form_str(RPL_ENDOFMOTD),me.name,sptr->name);
	    }
          return 0;
        }
      else
        last_used = CurrentTime;
    }

  if (!IsServer(sptr) && !HasUmode(sptr,UMODE_REMOTEINFO) && parc > 1)
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no S umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (hunt_server(cptr, sptr, ":%s MOTD :%s", 1,parc,parv)!=HUNTED_ISME)
    return 0;

  sendto_ops_flag(UMODE_SPY, "motd requested by %s (%s@%s) [%s]",
		      sptr->name, sptr->username, sptr->host,
		      sptr->user->server);

  return(SendMessageFile(sptr,&ConfigFileEntry.motd));
}

/*
** InitMessageFile
**
*/
void InitMessageFile(MotdType motdType, const char *fileName, MessageFile *motd)
  {
    strncpy_irc(motd->fileName, fileName, PATH_MAX);
    motd->fileName[PATH_MAX] = '\0';
    motd->motdType = motdType;
    motd->contentsOfFile = NULL;
    motd->lastChangedDate[0] = '\0';
  }

/*
** SendMessageFile
**
** This function split off so a server notice could be generated on a
** user requested motd, but not on each connecting client.
** -Dianora
*/

int SendMessageFile(struct Client *sptr, MessageFile *motdToPrint)
{
  MessageFileLine *linePointer;
  MotdType motdType;
  int bodynumeric, endnumeric;

  if(motdToPrint)
    motdType = motdToPrint->motdType;
  else
    return -1;

  switch(motdType)
    {
    case USER_MOTD:

      if (motdToPrint->contentsOfFile == (MessageFileLine *)NULL)
        {
          sendto_one(sptr, form_str(ERR_NOMOTD), me.name, sptr->name);
          return 0;
        }

      sendto_one(sptr, form_str(RPL_MOTDSTART), me.name, sptr->name, me.name);
      bodynumeric = RPL_MOTD;
      endnumeric = RPL_ENDOFMOTD;
      break;

    case OPER_MOTD:
      if (motdToPrint->contentsOfFile == NULL)
        return 0;
      sendto_one(sptr, form_str(RPL_OMOTDSTART), me.name, sptr->name);
      sendto_one(sptr,
                 ":%s %03d %s :OPER MOTD last changed %s",
                 me.name, RPL_OMOTD, sptr->name, motdToPrint->lastChangedDate);
      bodynumeric = RPL_OMOTD;
      endnumeric = RPL_ENDOFOMOTD;
      break;

    case HELP_MOTD:
      sendto_one(sptr, form_str(RPL_HELPSTART), me.name, sptr->name, "Oper help");
      bodynumeric = RPL_HELPTXT;
      endnumeric = RPL_ENDOFHELP;
      break;

    default:
      return 0;
      /* NOT REACHED */
    }

  for(linePointer = motdToPrint->contentsOfFile;linePointer;
      linePointer = linePointer->next)
    {
      sendto_one(sptr,
		 form_str(bodynumeric),
		 me.name, sptr->name, linePointer->line);
    }
  sendto_one(sptr, form_str(endnumeric), me.name, sptr->name);
  return 0;
}

/*
 * ReadMessageFile() - original From CoMSTuD, added Aug 29, 1996
 * modified by -Dianora
 */

int ReadMessageFile(MessageFile *MessageFileptr)
{
  struct stat sb;
  struct tm *local_tm;

  /* used to clear out old MessageFile entries */
  MessageFileLine *mptr = 0;
  MessageFileLine *next_mptr = 0;

  /* used to add new MessageFile entries */
  MessageFileLine *newMessageLine = 0;
  MessageFileLine *currentMessageLine = 0;

  char buffer[MESSAGELINELEN];
  char *p;
  FBFILE* file;

  if( stat(MessageFileptr->fileName, &sb) < 0 )
  /* file doesn't exist oh oh */
  /* might consider printing error message to all opers */
    return -1;

  local_tm = localtime(&sb.st_mtime);

  if (local_tm)
    ircsnprintf(MessageFileptr->lastChangedDate, MAX_DATE_STRING + 1,
		"%d-%d-%d %t:%t",
		1900 + local_tm->tm_year,
		local_tm->tm_mon + 1,
		local_tm->tm_mday,
		local_tm->tm_hour,
		local_tm->tm_min);

  /*
   * Clear out the old MOTD
   */
  for( mptr = MessageFileptr->contentsOfFile; mptr; mptr = next_mptr)
    {
      next_mptr = mptr->next;
      MyFree(mptr);
    }

  MessageFileptr->contentsOfFile = NULL;

  if ((file = fbopen(MessageFileptr->fileName, "r")) == 0)
    return(-1);

  while (fbgets(buffer, MESSAGELINELEN, file))
    {
      if ((p = strchr(buffer, '\n')))
        *p = '\0';
      expect_malloc;
      newMessageLine = (MessageFileLine*) MyMalloc(sizeof(MessageFileLine));
      malloc_log("ReadMessageFile() allocating MessageFileLine (%zd bytes) at %p",
                 sizeof(MessageFileLine), (void *)newMessageLine);

      strncpy_irc(newMessageLine->line, buffer, MESSAGELINELEN);
      newMessageLine->line[MESSAGELINELEN] = '\0';
      newMessageLine->next = (MessageFileLine *)NULL;

      if (MessageFileptr->contentsOfFile)
        {
          if (currentMessageLine)
            currentMessageLine->next = newMessageLine;
          currentMessageLine = newMessageLine;
        }
      else
        {
          MessageFileptr->contentsOfFile = newMessageLine;
          currentMessageLine = newMessageLine;
        }
    }

  fbclose(file);
  return(0);
}

size_t count_message_file(MessageFile *mf)
{
  MessageFileLine *mptr;
  size_t total = 0;
  for (mptr = mf->contentsOfFile; mptr; mptr = mptr->next)
    total += sizeof(MessageFileLine);
  return total;
}
