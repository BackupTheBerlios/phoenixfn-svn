/************************************************************************
 *   IRC - Internet Relay Chat, src/flud.c
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
#include "flud.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "send.h"
#include "channel.h"
#include "struct.h"
#include "blalloc.h"
#include "s_stats.h"

#include <time.h>
#include <string.h>

/* Shadowfax's FLUD code */

#ifdef FLUD

void announce_fluder(struct Client *fluder,
		     struct Client *cptr, /* client being fludded */
		     struct Channel *chptr,    /* channel being fludded */
		     int type)           /* for future use */
{
  char *fludee;

  if (!MyConnect(fluder))
    {
      sendto_ops_flag(UMODE_DEBUG, "announce_fluder() was passed non-local client fluder %s",
		      fluder->name);
      return -1;
    }

  if(cptr) 
    fludee = cptr->name; 
  else
    fludee = chptr->chname;

#ifdef DEATHFLUD
  switch(type)
    {
    case 1: /* classic CTCP */
      sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s (ctcp %i)",
		      fluder->name, fluder->username, fluder->host,
		      fluder->user->server, fludee, fluder->repeatcount);
      break;
    case 2: /* repeating channel traffic */
      sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s (echo %i)",
		      fluder->name, fluder->username, fluder->host,
		      fluder->user->server, fludee, fluder->repeatcount);
      break;
    case 3: /* repeating oneself */
      sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s (spam %i)",
		      fluder->name, fluder->username, fluder->host,
		      fluder->user->server, fludee, fluder->repeatcount);
      break;
    case 4: /* insane message lengths */
      sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s (len %i)",
		      fluder->name, fluder->username, fluder->host,
		      fluder->user->server, fludee, fluder->lastlen);
      break;
    default:
      sendto_ops_flag(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s",
		      fluder->name, fluder->username, fluder->host,
		      fluder->user->server, fludee);
      break;
    }
#else /* DEATHFLUD */
  sendto_ops_flags(UMODE_BOTS, "Flooder %s [%s@%s] on %s target: %s",
                 fluder->name, fluder->username, fluder->host,
                 fluder->user->server, fludee);
#endif
}


/* This is really just a "convenience" function.  I can only keep three or
** four levels of pointer dereferencing straight in my head.  This removes
** an entry in a fluders list.  Use this when working on a fludees list :) */

struct fludbot *remove_fluder_reference(struct fludbot **fluders,
                                        struct Client *fluder)
{
  struct fludbot *current;
  struct fludbot *prev;
  struct fludbot *next;

  prev = NULL;
  current = *fluders;
  while(current)
    { 
      next = current->next;
      if(current->fluder == fluder)
        {
          if(prev)
            prev->next = next; 
          else
            *fluders = next;
          
          free_fludbot(current);
        }
      else
        prev = current;
      current = next; 
    }

  return(*fluders);       
}
 
 
/* Another function to unravel my mind. */
struct SLink *
remove_fludee_reference(struct SLink **fludees,void *fludee)
{       
  struct SLink *current;
  struct SLink *prev;
  struct SLink *next; 

  prev = NULL;
  current = *fludees;
  while(current)
    {
      next = current->next;
      if(current->value.cptr == (struct Client *)fludee)
        {
          if(prev)
            prev->next = next; 
          else
            *fludees = next;
 
          free_link(current);
        }
      else
        prev = current;
      current = next; 
    }

  return(*fludees);       
}


/* This function checks to see if the given message is being repeated,
 * etc.  Returns type of flood (2 if repeating channel traffic, 3 if
 * repeating himself), or 0 if none. Message is truncated to TOPICLEN
 * if too long.  Unfortunately, this makes for a LOT of extra
 * processing in the server.
 */

int check_for_spam(struct Client *fluder, /* fluder */
                   struct Client *cptr,   /* client being fluded, NULL for now */
                   struct Channel *chptr, /* channel being fluded */
                   char *message)
{
  int flud = 0;
  int len;

  if (!MyConnect(fluder) || IsServer(fluder) || NoFloodProtection(fluder))
    return 0;

  if ((len = strlen(message)) > 160)
    {
      if (fluder->repeatcount++) /* ignore it the first time */
        flud = 4;
    }
  else if (!strncmp(fluder->lastmsg, message, 31)
           || ((len > 31) && (len == fluder->lastlen)))
    {
      flud = 3;
      fluder->repeatcount++;
    }
  else
    {
      strncpy(fluder->lastmsg, message, 32);
      fluder->lastmsg[31] = 0;
      fluder->repeatcount = 0;
    }
  fluder->lastlen = len;

  if (chptr)
    {
      if (!strncmp(chptr->lastmsg, message, 31)
          || ((len > 31) && (len == chptr->lastlen)))
        {
          if (!flud)
            flud = 2;
          chptr->repeatcount++;
        }
      else
        {
          strncpy(chptr->lastmsg, message, 32);
          chptr->lastmsg[31] = 0;
          chptr->repeatcount = 0;
        }
      chptr->lastlen = len;
    }

  return flud;
}

int
check_for_flud(struct Client *fluder, /* fluder, client being fluded */
               struct Client *cptr,   
               struct Channel *chptr, /* channel being fluded */
               int type)        /* for future use */
{               
  time_t now;
  struct fludbot *current, *prev, *next;
  int blocking, count, found;
  struct SLink *newfludee;

  /* If it's disabled, we don't need to process all of this */
  if(FLUDBLOCK == 0)
    return 0;

  /* It's either got to be a client or a channel being fluded */
  if((cptr == NULL) && (chptr == NULL))
    return 0;

  if(!MyConnect(fluder))
    {
      sendto_ops_flag(UMODE_DEBUG, "check_for_flud() called for non-local fluder client");
      return 0;
    }
  if (cptr && !MyConnect(cptr))
    {
      sendto_ops_flag(UMODE_DEBUG, "check_for_flud() called for non-local target client");
      return 0;
    }
 
  /* Are we blocking fluds at this moment? */
  time(&now);
  if(cptr)                
    blocking = (cptr->fludblock > (now - FLUDBLOCK));
  else
    blocking = (chptr->fludblock > (now - FLUDBLOCK));
        
  /* Collect the Garbage */
  if(!blocking)
    {
      if(cptr) 
        current = cptr->fluders;
      else
        current = chptr->fluders;
      prev = NULL; 
      while(current)
        {
          next = current->next;
          if(current->last_msg < (now - FLUDTIME))
            {
              if(cptr)
                remove_fludee_reference(&current->fluder->fludees,
                                        (void *)cptr);
              else
                remove_fludee_reference(&current->fluder->fludees,
                                        (void *)chptr);

              if(prev)
                prev->next = current->next;
              else
                if(cptr)
                  cptr->fluders = current->next;
                else
                  chptr->fluders = current->next;
              free_fludbot(current);
            }
          else
            prev = current;
          current = next;
        }
    }

  /* Find or create the structure for the fluder, and update the counter
   * and last_msg members.  Also make a running total count
   */
  if(cptr)
    current = cptr->fluders;
  else
    current = chptr->fluders;
  count = found = 0;
  while(current)
    {
      if(current->fluder == fluder) {
        current->last_msg = now;
        current->count++;
        found = 1;
      }
      if(current->first_msg < (now - FLUDTIME))
        count++;
      else    
        count += current->count;
      current = current->next;
    }
  if(!found)
    {
      if((current = BlockHeapALLOC(free_fludbots, struct fludbot)) != NULL)
        {
          current->fluder = fluder;
          current->count = 1; 
          current->first_msg = now;
          current->last_msg = now;
          if(cptr)
            {
              current->next = cptr->fluders;
              cptr->fluders = current;
            }
          else
            {
              current->next = chptr->fluders;
              chptr->fluders = current;
            }

          count++;  

          if((newfludee = BlockHeapALLOC(free_Links, struct SLink)) != NULL)
            {
              if(cptr)
                {
                  newfludee->flags = 0;
                  newfludee->value.cptr = cptr;
                }
              else
                {
                  newfludee->flags = 1;
                  newfludee->value.chptr = chptr;
                }
              newfludee->next = fluder->fludees;
              fluder->fludees = newfludee;
            }
          else
            outofmemory();

          /* If we are already blocking now, we should go ahead
           * and announce the new arrival
	   */
          if(blocking)
            announce_fluder(fluder, cptr, chptr, type);
        }       
      else
        outofmemory();
    }                       

  /* Okay, if we are not blocking, we need to decide if it's time to
   * begin doing so.  We already have a count of messages received in
   * the last flud_time seconds
   */
  if(!blocking && (count > FLUDNUM))
    {
      blocking = 1;   
      ServerStats->is_flud++;
      
      /* if we are going to say anything to the fludee, now is the
       * time to mention it to them.
       */
      if(cptr)
        sendto_one(cptr,     
                   ":%s NOTICE %s :*** Notice -- incoming %s flood from %s throttled at %s",
                   me.name, cptr->name, cptr->name, fluder->name, me.name);
      else            
        sendto_channel_butone(NULL, &me, chptr,
#ifdef DEATHFLUD
               ":%s NOTICE %s :*** Notice -- %s throttling %s %s flood from %s",
#else
               ":%s NOTICE %s :*** Notice -- %s throttling %s flood from %s",
#endif
                               me.name,
                               chptr->chname,
                               me.name, chptr->chname,
#ifdef DEATHFLUD
                               (type == 1) ? "CTCP" : "repeat",
#endif
                               fluder->name);
            
      /* Here we should go back through the existing list of
       * fluders and announce that they were part of the game as 
       * well.
       */
      if(cptr)        
        current = cptr->fluders;
      else
        current = chptr->fluders;
      while(current)
        {
          announce_fluder(current->fluder, cptr, chptr, type);
          current = current->next;
        }       
    }       
  
  /* update blocking timestamp, since we received a/another CTCP message */
  if(blocking)
    {
      if(cptr)
        cptr->fludblock = now;
      else
        chptr->fludblock = now;
    }               

#ifdef DEATHFLUD
  if (blocking && fluder->repeatcount > KILLNUM)
    {
      sendto_ops_flag(UMODE_SKILL, "Flood countermeasure, dumping user %s",
			     fluder->name);
      sendto_one(fluder, ":%s KILL %s :%s spam",
                 me.name, fluder->name, cptr ? cptr->name : chptr->chname);
      exit_client(fluder, fluder, &me, "Excessive spam");
    }
#endif
  return(blocking);
}               

void free_fluders(struct Client *cptr, struct Channel *chptr)
{      
  struct fludbot *fluders;
  struct fludbot *next;

  if((cptr == NULL) && (chptr == NULL))
    {
      sendto_ops_flag(UMODE_DEBUG, "free_fluders(NULL, NULL)");
      return;
    }    
 
  if(cptr && !MyConnect(cptr))  
    return;
                
  if(cptr)        
    fluders = cptr->fluders;
  else    
    fluders = chptr->fluders;

  while(fluders)
    {
      next = fluders->next;

      if(cptr)
        remove_fludee_reference(&fluders->fluder->fludees, (void *)cptr); 
      else
        remove_fludee_reference(&fluders->fluder->fludees, (void *)chptr);
      
      free_fludbot(fluders);
      fluders = next;
    }    
}


void free_fludees(struct Client *badguy)
{
  struct SLink *fludees;
  struct SLink *next;
                
  if(badguy == NULL)
    {
      sendto_ops_flag(UMODE_DEBUG, "free_fludees(NULL)");
      return;
    }
  fludees = badguy->fludees;
  while(fludees)
    {
      next = fludees->next;  
      
      if(fludees->flags)
        remove_fluder_reference(&fludees->value.chptr->fluders, badguy);
      else
        {
          if(!MyConnect(fludees->value.cptr))
            sendto_ops_flag(UMODE_DEBUG, "free_fludees() encountered non-local client");
          else
            remove_fluder_reference(&fludees->value.cptr->fluders, badguy);
        }

      free_link(fludees);
      fludees = next;
    }       
}       

#endif /* FLUD */

/* This function checks to see if a CTCP message (other than ACTION) is
 * contained in the passed string.  This might seem easier than I am doing it,
 * but a CTCP message can be changed together, even after a normal message.
 * 
 * Unfortunately, this makes for a bit of extra processing in the server.
 */
int check_for_ctcp(const char *str)
{
  const char *p = str;          
  while((p = strchr(p, 1)) != NULL)
    {
      if(ircncmp(++p, "ACTION", 6) != 0)
        return 1;
      if((p = strchr(p, 1)) == NULL)
        return 0;
      p++;    
    }       
  return 0;
}
