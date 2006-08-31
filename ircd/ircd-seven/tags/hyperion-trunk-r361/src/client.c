/************************************************************************
 *   IRC - Internet Relay Chat, src/client.c
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

#include "client.h"
#include "class.h"
#include "blalloc.h"
#include "channel.h"
#include "common.h"
#include "dline_conf.h"
#include "fdlist.h"
#include "flud.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "numeric.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "s_serv.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"
#include "s_debug.h"
#include "s_user.h"
#include "umodes.h"
#include "paths.h"

#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

struct  bitfield_lookup_t bitfield_lookup[BITFIELD_SIZE];

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
 * Number of struct Client structures to preallocate at a time
 * for Efnet 1024 is reasonable 
 * for smaller nets who knows? -Dianora
 *
 * This means you call MyMalloc 30 some odd times,
 * rather than 30k times -Dianora
 */
#define CLIENTS_PREALLOCATE 256
#define LOCAL_CLIENTS_PREALLOCATE 256

/* 
 * for Wohali's block allocator 
 */
static BlockHeap*        localClientFreeList;
static BlockHeap*        remoteClientFreeList;
static const char* const BH_FREE_ERROR_MESSAGE = \
        "client.c BlockHeapFree failed for cptr = %p";

struct Client* dying_clients[MAXCONNECTIONS]; /* list of dying clients */
const char*          dying_clients_reason[MAXCONNECTIONS];

/*
 * init_client_heap - initialize client free memory
 */
void init_client_heap(void)
{
  /* 
   * start off with CLIENTS_PREALLOCATE for now... on typical
   * efnet these days, it can get up to 35k allocated 
   */
  remoteClientFreeList =
    BlockHeapCreate((size_t) CLIENT_REMOTE_SIZE, CLIENTS_PREALLOCATE);
  /* 
   * Can't EVER have more than MAXCONNECTIONS number of local Clients 
   */
  /* Why preallocate so many? Making it smaller...
   *  -- asuffield
   */
  localClientFreeList = 
    BlockHeapCreate((size_t) CLIENT_LOCAL_SIZE, LOCAL_CLIENTS_PREALLOCATE);
}

void clean_client_heap(void)
{
  BlockHeapGarbageCollect(localClientFreeList);
  BlockHeapGarbageCollect(remoteClientFreeList);
}

/*
 * make_client - create a new Client struct and set it to initial state.
 *
 *      from == NULL,   create local client (a client connected
 *                      to a socket).
 *
 *      from,   create remote client (behind a socket
 *                      associated with the client defined by
 *                      'from'). ('from' is a local client!!).
 */
struct Client* make_client(struct Client* from)
{
  struct Client* cptr = NULL;

  if (!from)
    {
      cptr = BlockHeapALLOC(localClientFreeList, struct Client);
      if (cptr == NULL)
        outofmemory();
      assert(0 != cptr);

      memset(cptr, 0, CLIENT_LOCAL_SIZE);
      cptr->local_flag = 1;

      cptr->from  = cptr; /* 'from' of local client is self! */

#ifdef NULL_POINTER_NOT_ZERO
#ifdef FLUD
      cptr->fluders   = NULL;
#endif
#ifdef ZIP_LINKS
      cptr->zip       = NULL;
#endif
      cptr->listener  = NULL;
      cptr->confs     = NULL;

      cptr->dns_reply = NULL;
      cptr->allownicks = NULL;
#endif /* NULL_POINTER_NOT_ZERO */
    }
  else
    { /* from is not NULL */
      cptr = BlockHeapALLOC(remoteClientFreeList, struct Client);
      if(cptr == NULL)
        outofmemory();
      assert(0 != cptr);

      memset(cptr, 0, CLIENT_REMOTE_SIZE);
      /* cptr->local_flag = 0; */

      cptr->from = from; /* 'from' of local client is self! */
    }
  cptr->since = cptr->lasttime = cptr->firsttime = CurrentTime;
  SetUnknown(cptr);
  cptr->fd = -1;
  strncpy_irc(cptr->username, "unknown", USERLEN + 1);

#ifdef NULL_POINTER_NOT_ZERO
  /* commenting out unnecessary assigns, but leaving them
   * for documentation. REMEMBER the fripping struct is already
   * zeroed up above =DUH= 
   * -Dianora 
   */
  cptr->listprogress=0;
  cptr->listprogress2=0;
  cptr->next    = NULL;
  cptr->prev    = NULL;
  cptr->hnext   = NULL;
/*   cptr->idhnext = NULL; */
  cptr->lnext   = NULL;
  cptr->lprev   = NULL;
  cptr->next_local_client     = NULL;
  cptr->previous_local_client = NULL;
  cptr->next_server_client    = NULL;
  cptr->next_oper_client      = NULL;
  cptr->user    = NULL;
  cptr->serv    = NULL;
  cptr->servptr = NULL;
  cptr->whowas  = NULL;
#ifdef FLUD
  cptr->fludees = NULL;
#endif
#endif /* NULL_POINTER_NOT_ZERO */

  return cptr;
}

void _free_client(struct Client* cptr)
{
  assert(0 != cptr);
  assert(&me != cptr);
  assert(0 == cptr->prev);
  assert(0 == cptr->next);

  if (cptr->local_flag) {
    if (-1 < cptr->fd)
      close(cptr->fd);

    if (cptr->dns_reply)
      --cptr->dns_reply->ref_count;

    BlockHeapFree(localClientFreeList, cptr);
  }
  else
    BlockHeapFree(remoteClientFreeList, cptr);
}

/*
 * I re-wrote check_pings a tad
 *
 * check_pings - go through the local client list and check activity
 * kill off stuff that should die
 *
 * inputs       - current time
 * output       - next time_t when check_pings() should be called again
 *
 * side effects - 
 *
 * Clients can be k-lined/d-lined/g-lined/r-lined and exit_client
 * called for each of these.
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 *
 * -Dianora
 */

/* Note, that dying_clients and dying_clients_reason
 * really don't need to be any where near as long as MAXCONNECTIONS
 * but I made it this long for now. If its made shorter,
 * then a limit check is going to have to be added as well
 * -Dianora
 */
time_t check_pings(time_t currenttime)
{               
  struct Client *cptr;          /* current local cptr being examined */
  struct ConfItem     *aconf = (struct ConfItem *)NULL;
  int           ping = 0;               /* ping time value from client */
  int           i;                      /* used to index through fd/cptr's */
  time_t        oldest = 0;             /* next ping time */
  time_t        timeout;                /* found necessary ping time */
  const char    *reason;                /* pointer to reason string */
  int           die_index=0;            /* index into list */
  char          ping_time_out_buffer[64];   /* blech that should be a define */

                                        /* of dying clients */
  dying_clients[0] = (struct Client *)NULL;   /* mark first one empty */

  /*
   * I re-wrote the way klines are handled. Instead of rescanning
   * the local[] array and calling exit_client() right away, I
   * mark the client thats dying by placing a pointer to its struct Client
   * into dying_clients[]. When I have examined all in local[],
   * I then examine the dying_clients[] for struct Client's to exit.
   * This saves the rescan on k-lines, also greatly simplifies the code,
   *
   * Jan 28, 1998
   * -Dianora
   */

   for (i = 0; i <= highest_fd; i++)
    {
      if (!(cptr = local[i]) || IsMe(cptr))
        continue;               /* and go examine next fd/cptr */
      /*
      ** Note: No need to notify opers here. It's
      ** already done when "FLAGS_DEADSOCKET" is set.
      */
      if (cptr->flags & FLAGS_DEADSOCKET)
        {
          /* N.B. EVERY single time dying_clients[] is set
           * it must be followed by an immediate continue,
           * to prevent this cptr from being marked again for exit.
           * If you don't, you could cause exit_client() to be called twice
           * for the same cptr. i.e. bad news
           * -Dianora
           */

          dying_clients[die_index] = cptr;
          dying_clients_reason[die_index++] =
            ((cptr->flags & FLAGS_SENDQEX) ?
             "SendQ exceeded" : "Dead socket");
          dying_clients[die_index] = (struct Client *)NULL;
          continue;             /* and go examine next fd/cptr */
        }

      if (rehashed)
        {
          if(dline_in_progress)
            {
              if(!IsServer(cptr) &&  
#ifdef IPV6
                 (aconf = match_Dline(get_ipv4_ip(&cptr->ip)))
#else
                 (aconf = match_Dline(ntohl(cptr->ip.s_addr)))
#endif
		)

                  /* if there is a returned 
                   * struct ConfItem then kill it
                   */
                {
                  if(IsConfElined(aconf) || IsElined(cptr))
                    {
                      sendto_ops_flag(UMODE_SERVNOTICE, "D-line over-ruled for %s, client is E-lined",
				      get_client_name(cptr, HIDE_IP));
                      continue;
                    }

		  sendto_ops_flag(UMODE_SKILL, "%s confirms D-line of %s", me.name,
				  get_client_name(cptr, HIDE_IP));

                  dying_clients[die_index] = cptr;
/* Wintrhawk */
#ifdef KLINE_WITH_CONNECTION_CLOSED
                  /*
                   * We use a generic non-descript message here on 
                   * purpose so as to prevent other users seeing the
                   * client disconnect from harassing the IRCops
                   */
                  reason = "Connection closed";
#else
                  reason = "D-lined";
#endif /* KLINE_WITH_CONNECTION_CLOSED */

                  dying_clients_reason[die_index++] = reason;
                  dying_clients[die_index] = (struct Client *)NULL;
                  if(IsPerson(cptr))
                    {
                      char *tmp, *p = NULL;
                      if (aconf->passwd)
                        {
                          /* If there is a | in aconf->passwd, chop off it and everything after it */
                          if ((p = strchr(aconf->passwd, '|')))
                            *p = '\0';
                          /* Remove the name of the person that placed the kline, if any */
                          if ((tmp = strchr(aconf->passwd, ';')))
                            reason = tmp + 1;
                          /* Otherwise just use the string */
                          else
                            reason = aconf->passwd;
                        }
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
		      if (p != NULL)
			*p = '|';
                    }
#ifdef REPORT_DLINE_TO_USER
                  else
                    {
                      sendto_one(cptr, "NOTICE DLINE :*** You have been D-lined");
                    }
#endif
                  continue;         /* and go examine next fd/cptr */
                }
            }
          else
            {
              if(IsPerson(cptr))
                {
                  if((aconf = find_kill(cptr))) /* if there is a returned
                                                   struct ConfItem.. then kill it */
                    {
		      /* Don't send a notice about kline exempt clients
		       * for now -- jilles */
		      if (IsElined(cptr))
			continue;
                      if(aconf->status & CONF_ELINE)
                        {
                          sendto_ops_flag(UMODE_SERVNOTICE, "K-line over-ruled for %s, client is E-lined",
					  get_client_name(cptr, HIDE_IP));
			  continue;
                        }

		      sendto_ops_flag(UMODE_SKILL, "%s confirms kill of %s", me.name,
				      get_client_name(cptr, HIDE_IP));
                      dying_clients[die_index] = cptr;

/* Wintrhawk */
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose so as to prevent other users seeing the
                       * client disconnect from harassing the IRCops
                       */
                      reason = "Connection closed";
#else
                      reason = "K-lined";
#endif /* KLINE_WITH_CONNECTION_CLOSED */

                      dying_clients_reason[die_index++] = reason;
                      dying_clients[die_index] = (struct Client *)NULL;
                      {
                        char *tmp, *p = NULL;
                        if (aconf->passwd)
                          {
                            /* If there is a | in aconf->passwd, chop off it and everything after it */
                            if ((p = strchr(aconf->passwd, '|')))
                              *p = '\0';
                            /* Remove the name of the person that placed the kline, if any */
                            if ((tmp = strchr(aconf->passwd, ';')))
                              reason = tmp + 1;
                            /* Otherwise just use the string */
                            else
                              reason = aconf->passwd;
                          }
                        sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                   me.name, cptr->name, reason);
			if (p != NULL)
			  *p = '|';
                      }
                      continue;         /* and go examine next fd/cptr */
                    }
                }
            }
        }

#ifdef REJECT_HOLD
      if (IsRejectHeld(cptr))
        {
          if( CurrentTime > (cptr->firsttime + REJECT_HOLD_TIME) )
            {
              if( reject_held_fds )
                reject_held_fds--;

              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "reject held client";
              dying_clients[die_index] = (struct Client *)NULL;
              continue;         /* and go examine next fd/cptr */
            }
        }
#endif

      if (!IsUnknown(cptr))
        {
	  if (!IsRegistered(cptr))
	    ping = CONNECTTIMEOUT;
	  else
	    ping = get_client_ping(cptr);

	  /*
	   * Ok, so goto's are ugly and can be avoided here but this code
	   * is already indented enough so I think its justified. -avalon
	   */
	   /*  if (!rflag &&
		   (ping >= currenttime - cptr->lasttime))
		  goto ping_timeout; */

	  /*
	   * *sigh* I think not -Dianora
	   */

	  if (ping < (currenttime - cptr->lasttime))
	    {

	      /*
	       * If the server hasnt talked to us in 2*ping seconds
	       * and it has a ping time, then close its connection.
	       * If the client is a user and a KILL line was found
	       * to be active, close this connection too.
	       */
	      if (currenttime - cptr->lasttime >= (GlobalSetOptions.dopingout || !IsClient(cptr) ? 2 : 11) * ping &&
		   (cptr->flags & FLAGS_PINGSENT))
		{
		  if (cptr->flags2 & FLAGS2_PING_TIMEOUT)
		    {
		      /* Something is *FUCKED*. Bail. -- asuffield */
		      ts_warn("MAJOR WTF: Client %s has already timed out and has not been deleted! " \
			      "IsClient == %d, IsServer == %d, cptr == %p, cptr->flags == %lu, cptr->flags2 == %lu, i == %d",
			      cptr ? cptr->name : "(null)",
			      IsClient(cptr), IsServer(cptr), (void *)cptr, cptr ? (long int)cptr->flags : 0, 
			      cptr ? (long int)cptr->flags2 : 0, i);
		      
		      continue;
		    }
		  if (IsServer(cptr) || IsConnecting(cptr) ||
		      IsHandshake(cptr))
		    {
		      sendto_ops_flag(UMODE_SERVCONNECT, "No response from %s (ping timeout), closing link",
				      get_client_name(cptr, HIDE_IP));
		    }
		  /*
		   * this is used for KILL lines with time restrictions
		   * on them - send a messgae to the user being killed
		   * first.
		   * *** Moved up above  -taner ***
		   */
		  cptr->flags2 |= FLAGS2_PING_TIMEOUT;
		  dying_clients[die_index++] = cptr;
		  /* the reason is taken care of at exit time */
	  /*      dying_clients_reason[die_index++] = "Ping timeout"; */
		  dying_clients[die_index] = (struct Client *)NULL;
		  
		  /*
		   * need to start loop over because the close can
		   * affect the ordering of the local[] array.- avalon
		   *
		   ** Not if you do it right - Dianora
		   */

		  continue;
		}
	      /* Don't waste time pinging a server that is in the
	       * CONNECTING state, it will just send a 451
	       */
	      else if (!IsConnecting(cptr) && (cptr->flags & FLAGS_PINGSENT) == 0)
		{
		  /*
		   * if we havent PINGed the connection and we havent
		   * heard from it in a while, PING it to make sure
		   * it is still alive.
		   */
		  cptr->flags |= FLAGS_PINGSENT;
		  /* not nice but does the job */
		  cptr->lasttime = currenttime - ping;
		  /* does the job better -- asuffield */
		  gettimeofday(&cptr->ping_send_time, NULL);
		  sendto_one(cptr, "PING :%s", me.name);
		}
	    }
	  /* ping_timeout: */
	  timeout = cptr->lasttime + ping;
	  while (timeout <= currenttime)
	    timeout += ping;
	  if (timeout < oldest || !oldest)
	    oldest = timeout;

	} /* !IsUnknown(cptr) */
      /*
       * Check UNKNOWN connections - if they have been in this state
       * for > UNKNOWN_TIME, close them.
       */
      else if (cptr->firsttime != 0)
        {
	  timeout = cptr->firsttime + UNKNOWN_TIME;
          if (timeout <= CurrentTime)
            {
              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "Connection Timed Out";
              dying_clients[die_index] = (struct Client *)NULL;
              continue;
            }
	  else if (timeout < oldest || !oldest)
	    oldest = timeout;
        }
    }

  /* Now exit clients marked for exit above.
   * it doesn't matter if local[] gets re-arranged now
   *
   * -Dianora
   */

  for(die_index = 0; (cptr = dying_clients[die_index]); die_index++)
    {
      if(cptr->flags2 & FLAGS2_PING_TIMEOUT)
        {
          ircsnprintf(ping_time_out_buffer, 64,
		      "Ping timeout: %ld seconds",
		      currenttime - cptr->lasttime);
	  if (IsServer(cptr))
	    logprintf(L_NOTICE, "Ping timeout: %.1lu seconds for %s",
		currenttime - cptr->lasttime, cptr->name);

          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_ops_flag(UMODE_DEBUG, "Client already exited doing ping timeout (%p)", (void *)cptr);
            }
          else
            exit_client(cptr, cptr, &me, ping_time_out_buffer );
        }
      else
        {
          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_ops_flag(UMODE_DEBUG, "Client already exited (%p)", (void *)cptr);
            }
          else
            exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
        }
      cptr->flags2 |= FLAGS2_ALREADY_EXITED;
    }

  rehashed = 0;
  dline_in_progress = 0;

  if (!oldest || oldest < currenttime)
    oldest = currenttime + PINGFREQUENCY;
  Debug((DEBUG_NOTICE, "Next check_ping() call at: %s, %d %d %d",
         myctime(oldest), ping, oldest, currenttime));
  
  return (oldest);
}


static void update_client_exit_stats(struct Client* cptr)
{
  if (IsServer(cptr))
    {
      --Count.server;

#ifdef NEED_SPLITCODE
      /* Don't bother checking for a split, if split code
       * is deactivated with server_split_recovery_time == 0
       */
      if(SPLITDELAY && (Count.server < SPLITNUM))
        {
          if (!server_was_split)
            {
              sendto_local_ops_flag(UMODE_SERVNOTICE, "Netsplit detected, split-mode activated, hope for the best");
              server_was_split = YES;
            }
          server_split_time = CurrentTime;
        }
#endif
    }

  else if (IsClient(cptr)) {
    if (!IsHoneypot(cptr))
      --Count.total;
    if (HasUmode(cptr,UMODE_OPER))
      --Count.oper;
    if (!IsHoneypot(cptr) && IsInvisible(cptr)) 
      --Count.invisi;
  }
  else if (IsUnknown(cptr))
    --Count.unknown;
}

static void release_client_state(struct Client* cptr)
{
  if (cptr->user) {
    if (IsPerson(cptr)) {
      add_history(cptr,0);
      off_history(cptr);
    }
    free_user(cptr->user, cptr); /* try this here */
  }
  if (cptr->serv)
    {
      if (cptr->serv->user)
        free_user(cptr->serv->user, cptr);
      MyFree(cptr->serv);
    }

#ifdef FLUD
  if (MyConnect(cptr))
    free_fluders(cptr, NULL);
  free_fludees(cptr);
#endif
  if (MyConnect(cptr) && cptr->allownicks)
    MyFree(cptr->allownicks);
}

/*
 * taken the code from ExitOneClient() for this and placed it here.
 * - avalon
 */
void remove_client_from_list(struct Client* cptr)
{
  assert(0 != cptr);

 /* HACK somehow this client has already exited
  * but has come back to haunt us.. looks like a bug
  */
  if(!cptr->prev && !cptr->next)
    {
      logprintf(L_CRIT, "already exited client %p [%s]",
	  (void *)cptr,
	  cptr->name ? cptr->name : "NULL");
      return;
    }

  if (cptr->prev)
    cptr->prev->next = cptr->next;
  else
    {
      GlobalClientList = cptr->next;
      GlobalClientList->prev = NULL;
    }
  if (cptr->next)
    cptr->next->prev = cptr->prev;
  cptr->next = cptr->prev = NULL;

  /*
   * XXX - this code should be elsewhere
   */
  update_client_exit_stats(cptr);
  release_client_state(cptr);
  if (hash_find_client(cptr->name, NULL) == cptr)
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: about to free client %s but they are still in the hash table",
		      cptr->name);
      logprintf(L_WARN, "WTF: about to free client %s but they are still in the hash table",
	  cptr->name);
      abort();
    }
  free_client(cptr);
}

/*
 * although only a small routine, it appears in a number of places
 * as a collection of a few lines...functions like this *should* be
 * in this file, shouldnt they ?  after all, this is list.c, isnt it ?
 * -avalon
 */
void add_client_to_list(struct Client *cptr)
{
  /*
   * since we always insert new clients to the top of the list,
   * this should mean the "me" is the bottom most item in the list.
   */
  cptr->next = GlobalClientList;
  GlobalClientList = cptr;
  if (cptr->next)
    cptr->next->prev = cptr;
  return;
}

/* Functions taken from +CSr31, paranoified to check that the client
** isn't on a llist already when adding, and is there when removing -orabidoo
*/
void add_client_to_llist(struct Client **bucket, struct Client *client)
{
  if (!client->lprev && !client->lnext)
    {
      client->lprev = NULL;
      if ((client->lnext = *bucket) != NULL)
        client->lnext->lprev = client;
      *bucket = client;
    }
}

void del_client_from_llist(struct Client **bucket, struct Client *client)
{
  if (client->lprev)
    {
      client->lprev->lnext = client->lnext;
    }
  else if (*bucket == client)
    {
      *bucket = client->lnext;
    }
  if (client->lnext)
    {
      client->lnext->lprev = client->lprev;
    }
  client->lnext = client->lprev = NULL;
}

/*
 *  find_client - find a client (server or user) by name.
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string and the search is the for server and user.
 */
struct Client* find_client(const char* name, struct Client *cptr)
{
  if (name)
    cptr = hash_find_client(name, cptr);

  return cptr;
}

/*
 *  find_userhost - find a user@host (server or user).
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string and the search is the for server and user.
 */
struct Client *find_userhost(char *user, const char *host, struct Client *cptr, int *count)
{
  struct Client       *c2ptr;
  struct Client       *res = cptr;

  *count = 0;
  if (collapse(user))
    for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next) 
      {
        if (!MyClient(c2ptr)) /* implies mine and a user */
          continue;
        if ((!host || match(host, c2ptr->host)) &&
            irccmp(user, c2ptr->username) == 0)
          {
            (*count)++;
            res = c2ptr;
          }
      }
  return res;
}

/*
 *  find_server - find server by name.
 *
 *      This implementation assumes that server and user names
 *      are unique, no user can have a server name and vice versa.
 *      One should maintain separate lists for users and servers,
 *      if this restriction is removed.
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string.
 */
struct Client* find_server(const char* name)
{
  if (name)
    return hash_find_server(name);
  return 0;
}

/*
 * next_client - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client*
next_client(struct Client *next,     /* First client to check */
            const char* ch)          /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return ((struct Client *) NULL);

  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name)) break;
    }
  return next;
}


/* 
 * this slow version needs to be used for hostmasks *sigh
 *
 * next_client_double - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client* 
next_client_double(struct Client *next, /* First client to check */
                   const char* ch)      /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return NULL;
  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name) || match(next->name,ch))
        break;
    }
  return next;
}

#if 0
/*
 * find_server_by_name - attempt to find server in hash table, otherwise 
 * scan the GlobalClientList
 */
struct Client* find_server_by_name(const char* name)
{
  struct Client* cptr = 0;

  if (EmptyString(name))
    return cptr;

  if ((cptr = hash_find_server(name)))
    return cptr;
  /*
   * XXX - this shouldn't be needed at all hash_find_server should
   * find hostmasked names
   */
  if (!strchr(name, '*'))
    return cptr;

  /* hmmm hot spot for host masked servers (ick)
   * a separate link list for all servers would help here
   * instead of having to scan 50k client structs. (ick)
   * -Dianora
   */
  for (cptr = GlobalClientList; cptr; cptr = cptr->next)
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        continue;
      if (match(name, cptr->name))
        break;
      if (strchr(cptr->name, '*'))
        if (match(cptr->name, name))
          break;
    }
  return cptr;
}
#endif

/*
 *  find_person - find person by (nick)name.
 */
struct Client *find_person(const char *name, struct Client *cptr)
{
  struct Client       *c2ptr = cptr;

  c2ptr = find_client(name, c2ptr);

  if (c2ptr && IsClient(c2ptr) && c2ptr->user)
    return c2ptr;
  else
    return cptr;
}

/*
 * find_chasing - find the client structure for a nick name (user) 
 *      using history mechanism if necessary. If the client is not found, 
 *      an error message (NO SUCH NICK) is generated. If the client was found
 *      through the history, chasing will be 1 and otherwise 0.
 */
struct Client *find_chasing(struct Client *sptr, const char *user, int *chasing)
{
  struct Client *who = find_client(user, (struct Client *)NULL);
  
  if (chasing)
    *chasing = 0;
  if (who)
    return who;
  if (!(who = get_history(user, (long)KILLCHASETIMELIMIT)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                 me.name, sptr->name, user);
      return ((struct Client *)NULL);
    }
  if (chasing)
    *chasing = 1;
  return who;
}



/*
 * check_registered_user - is used to cancel message, if the
 * originator is a server or not registered yet. In other
 * words, passing this test, *MUST* guarantee that the
 * sptr->user exists (not checked after this--let there
 * be coredumps to catch bugs... this is intentional --msa ;)
 *
 * There is this nagging feeling... should this NOT_REGISTERED
 * error really be sent to remote users? This happening means
 * that remote servers have this user registered, although this
 * one has it not... Not really users fault... Perhaps this
 * error message should be restricted to local clients and some
 * other thing generated for remotes...
 */
int check_registered_user(struct Client* client)
{
  if (!IsRegisteredUser(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * check_registered user cancels message, if 'x' is not
 * registered (e.g. we don't know yet whether a server
 * or user)
 */
int check_registered(struct Client* client)
{
  if (!IsRegistered(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * release_client_dns_reply - remove client dns_reply references
 *
 */
void release_client_dns_reply(struct Client* client)
{
  assert(0 != client);
  if (client->dns_reply) {
    --client->dns_reply->ref_count;
    client->dns_reply = 0;
  }
}

/*
 * get_client_name -  Return the name of the client
 *    for various tracking and
 *      admin purposes. The main purpose of this function is to
 *      return the "socket host" name of the client, if that
 *        differs from the advertised name (other than case).
 *        But, this can be used to any client structure.
 *
 * NOTE 1:
 *        Watch out the allocation of "nbuf", if either sptr->name
 *        or sptr->sockhost gets changed into pointers instead of
 *        directly allocated within the structure...
 *
 * NOTE 2:
 *        Function return either a pointer to the structure (sptr) or
 *        to internal buffer (nbuf). *NEVER* use the returned pointer
 *        to modify what it points!!!
 */
/* Apparently, the use of (+) for idented clients
 * is unstandard. As it is a pain to parse, I'm just as happy
 * to remove it. It also simplifies the code a bit. -Dianora
 */

const char* get_client_name(struct Client* client, int showip)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];

  assert(0 != client);

  if (MyConnect(client))
    {
      if (!irccmp(client->name, client->host))
        return client->name;

#ifdef HIDE_SERVERS_IPS
      if(IsServer(client) || IsHandshake(client) || IsConnecting(client))
      {
        ircsnprintf(nbuf, HOSTLEN * 2 + USERLEN + 5, "%s[%s@255.255.255.255]", 
		    client->name, client->username);
        return nbuf;
      }
#endif

      /* And finally, let's get the host information, ip or name */
      switch (showip)
        {
	case SHOW_IP:
	  ircsnprintf(nbuf, HOSTLEN * 2 + USERLEN + 5, "%s[%s@%s]", client->name, client->username,
		      client->sockhost);
	  break;
	case MASK_IP:
	  ircsnprintf(nbuf, HOSTLEN * 2 + USERLEN + 5, "%s[%s@255.255.255.255]", client->name,
		      client->username);
	  break;
	case HIDE_IP:
	default:
	  ircsnprintf(nbuf, HOSTLEN * 2 + USERLEN + 5, "%s[%s@%s]", client->name, client->username,
		      client->host);
        }
      return nbuf;
    }

  /* As pointed out by Adel Mezibra 
   * Neph|l|m@EFnet. Was missing a return here.
   */
  return client->name;
}

const char* get_client_host(struct Client* client)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];
  
  assert(0 != client);

  if (!MyConnect(client))
    return client->name;
  if (!client->dns_reply)
    return get_client_name(client, HIDE_IP);
  else
    {
      ircsnprintf(nbuf, HOSTLEN * 2 + USERLEN + 5, "%s[%-.*s@%-.*s]",
		  client->name, USERLEN, client->username,
		  HOSTLEN, client->host);
    }
  return nbuf;
}

/*
** Exit one client, local or remote. Assuming all dependents have
** been already removed, and socket closed for local client.
*/
static void exit_one_client(struct Client *cptr, struct Client *sptr, struct Client *from,
                            const char* comment)
{
  struct Client* acptr;
  Link*    lp;

  if (IsServer(sptr))
    {
      if (sptr->servptr && sptr->servptr->serv)
        del_client_from_llist(&(sptr->servptr->serv->servers),
                                    sptr);
      else
        ts_warn("server %s without servptr!", sptr->name);
    }
  else if (sptr->servptr && sptr->servptr->serv)
      del_client_from_llist(&(sptr->servptr->serv->users), sptr);
  /* there are clients w/o a servptr: unregistered ones */

  /* Remove IP hash entry (moved here to allow remote clients
   * to be counted) -- jilles */
#ifdef LIMIT_UH
  if(sptr->flags & FLAGS_IPHASH)
    remove_one_ip(sptr, sptr->servptr && sptr->servptr != &me);
#else
  if(sptr->flags & FLAGS_IPHASH)
    remove_one_ip(&sptr->ip, sptr->servptr && sptr->servptr != &me);
#endif
  sptr->flags &= ~FLAGS_IPHASH;

  /*
  **  For a server or user quitting, propogate the information to
  **  other servers (except to the one where is came from (cptr))
  */
  if (IsMe(sptr))
    {
      sendto_ops_flag(UMODE_DEBUG, "ERROR: tried to exit me! : %s", comment);
      return;        /* ...must *never* exit self!! */
    }
  else if (IsServer(sptr))
    {
      /*
      ** Old sendto_serv_but_one() call removed because we now
      ** need to send different names to different servers
      ** (domain name matching)
      */
      /*
      ** The bulk of this is done in remove_dependents now, all
      ** we have left to do is send the SQUIT upstream.  -orabidoo
      */
      acptr = sptr->from;
      if (acptr && IsServer(acptr) && acptr != cptr && !IsMe(acptr) &&
          (sptr->flags & FLAGS_KILLED) == 0)
        sendto_one(acptr, ":%s SQUIT %s :%s", from->name, sptr->name, comment);
    }
  else if (!(IsPerson(sptr)))
      /* ...this test is *dubious*, would need
      ** some thought.. but for now it plugs a
      ** nasty hole in the server... --msa
      */
      ; /* Nothing */
  else if (sptr->name[0]) /* ...just clean all others with QUIT... */
    {
      /*
      ** If this exit is generated from "m_kill", then there
      ** is no sense in sending the QUIT--KILL's have been
      ** sent instead.
      */
      if (!IsHoneypot(sptr) && (sptr->flags & FLAGS_KILLED) == 0)
        {
          sendto_serv_butone(cptr,":%s QUIT :%s",
                             sptr->name, comment);
        }
      /*
      ** If a person is on a channel, send a QUIT notice
      ** to every client (person) on the same channel (so
      ** that the client can show the "**signoff" message).
      ** (Note: The notice is to the local clients *only*)
      */
      if (!IsHoneypot(sptr) && sptr->user)
        {
          sendto_common_channels(sptr, ":%s QUIT :%s",
                                   sptr->name, comment);

          while ((lp = sptr->user->channel))
            remove_user_from_channel(sptr,lp->value.chptr,0);

#if 0
          while ((lp = sptr->user->logging))
            {
              Link **curr, *tmp;
              for (curr = &lp->value.chptr->loggers; (tmp = *curr); curr = &tmp->next)
                {
                  if (tmp->value.cptr == sptr)
                    {
                      *curr = tmp->next;
                      free_link(tmp);
                      lp->value.chptr->logcount--;
                      sptr->user->logcount--;
                      if (!lp->value.chptr->logcount)
                        {
                          lp->value.chptr->mode.mode &= ~MODE_LOGGING;
                          sendto_channel_butserv(lp->value.chptr, sptr, ":%s MODE %s -L",
                                                 sptr->name, lp->value.chptr->chname);
                        }
                      break;
                    }
                }
              sptr->user->logging = lp->next;
              free_link(lp);
            }
#endif
          
          /* Clean up invitefield */
          while ((lp = sptr->user->invited))
            del_invite(sptr, lp->value.chptr);
          /* again, this is all that is needed */

          /* Clean up silencefield */
          while ((lp = sptr->user->silence))
            del_silence(sptr, lp->value.cp);
        }
    }
  
  /* 
   * Remove sptr from the client lists
   */
  if (!IsHoneypot(sptr))
    del_from_client_hash_table(sptr->name, sptr);
  remove_client_from_list(sptr);
}

/*
** Recursively send QUITs and SQUITs for sptr and all its dependent clients
** and servers to those servers that need them.  A server needs the client
** QUITs if it can't figure them out from the SQUIT (ie pre-TS4) or if it
** isn't getting the SQUIT because of @#(*&@)# hostmasking.  With TS4, once
** a link gets a SQUIT, it doesn't need any QUIT/SQUITs for clients depending
** on that one -orabidoo
*/
static void recurse_send_quits(struct Client *cptr, struct Client *sptr, struct Client *to,
                                const char* comment,  /* for servers */
                                const char* myname)
{
  struct Client *acptr;

  /* If this server can handle quit storm (QS) removal
   * of dependents, just send the SQUIT -Dianora
   */

  if (IsCapable(to,CAP_QS))
    {
      if (match(myname, sptr->name))
        {
          for (acptr = sptr->serv->users; acptr; acptr = acptr->lnext)
            if (!IsHoneypot(acptr))
              sendto_one(to, ":%s QUIT :%s", acptr->name, comment);
          for (acptr = sptr->serv->servers; acptr; acptr = acptr->lnext)
            recurse_send_quits(cptr, acptr, to, comment, myname);
        }
      else
        sendto_one(to, "SQUIT %s :%s", sptr->name, me.name);
    }
  else
    {
      for (acptr = sptr->serv->users; acptr; acptr = acptr->lnext)
        if (!IsHoneypot(acptr))
          sendto_one(to, ":%s QUIT :%s", acptr->name, comment);
      for (acptr = sptr->serv->servers; acptr; acptr = acptr->lnext)
        recurse_send_quits(cptr, acptr, to, comment, myname);
      if (!match(myname, sptr->name))
        sendto_one(to, "SQUIT %s :%s", sptr->name, me.name);
    }
}

/* 
** Remove all clients that depend on sptr; assumes all (S)QUITs have
** already been sent.  we make sure to exit a server's dependent clients 
** and servers before the server itself; exit_one_client takes care of 
** actually removing things off llists.   tweaked from +CSr31  -orabidoo
*/
/*
 * added sanity test code.... sptr->serv might be NULL... -Dianora
 */
static void recurse_remove_clients(struct Client* sptr, const char* comment)
{
  struct Client *acptr;

  if (IsMe(sptr))
    return;

  if (!sptr->serv)        /* oooops. uh this is actually a major bug */
    return;

  while ( (acptr = sptr->serv->servers) )
    {
      recurse_remove_clients(acptr, comment);
      /*
      ** a server marked as "KILLED" won't send a SQUIT 
      ** in exit_one_client()   -orabidoo
      */
      acptr->flags |= FLAGS_KILLED;
      exit_one_client(NULL, acptr, &me, me.name);
    }

  while ( (acptr = sptr->serv->users) )
    {
      acptr->flags |= FLAGS_KILLED;
      exit_one_client(NULL, acptr, &me, comment);
    }
}

/*
** Remove *everything* that depends on sptr, from all lists, and sending
** all necessary QUITs and SQUITs.  sptr itself is still on the lists,
** and its SQUITs have been sent except for the upstream one  -orabidoo
*/
static void remove_dependents(struct Client* cptr, 
                               struct Client* sptr,
                               struct Client* from,
                               const char* comment,
                               const char* comment1)
{
  struct Client *to;
  int i;
  struct ConfItem *aconf;
  static char myname[HOSTLEN+1];

  for (i=0; i<=highest_fd; i++)
    {
      if (!(to = local[i]) || !IsServer(to) || IsMe(to) ||
          to == sptr->from || (to == cptr && IsCapable(to,CAP_QS)))
        continue;
      /* MyConnect(sptr) is rotten at this point: if sptr
       * was mine, ->from is NULL.  we need to send a 
       * WALLOPS here only if we're "deflecting" a SQUIT
       * that hasn't hit its target  -orabidoo
       */
      /* The WALLOPS isn't needed here as pointed out by
       * comstud, since m_squit already does the notification.
       */
#if 0
      if (to != cptr &&        /* not to the originator */
          to != sptr->from && /* not to the destination */
          cptr != sptr->from        /* hasn't reached target */
          && sptr->servptr != &me) /* not mine [done in m_squit] */
        sendto_one(to, ":%s WALLOPS :Received SQUIT %s from %s (%s)",
                   me.name, sptr->name, from->name, comment);

#endif
      if ((aconf = to->serv->nline))
        strncpy_irc(myname, my_name_for_link(me.name, aconf), HOSTLEN + 1);
      else
        strncpy_irc(myname, me.name, HOSTLEN + 1);
      recurse_send_quits(cptr, sptr, to, comment1, myname);
    }

  recurse_remove_clients(sptr, comment1);
}


/*
 * exit_client - This is old "m_bye". Name  changed, because this is not a
 *        protocol function, but a general server utility function.
 *
 *        This function exits a client of *any* type (user, server, etc)
 *        from this server. Also, this generates all necessary prototol
 *        messages that this exit may cause.
 *
 *   1) If the client is a local client, then this implicitly
 *        exits all other clients depending on this connection (e.g.
 *        remote clients having 'from'-field that points to this.
 *
 *   2) If the client is a remote client, then only this is exited.
 *
 * For convenience, this function returns a suitable value for
 * m_function return value:
 *
 *        CLIENT_EXITED        if (cptr == sptr)
 *        0                if (cptr != sptr)
 */
int exit_client(struct Client* cptr, /* The local client originating the exit or NULL, if this
				      * exit is generated by this server for internal reasons.
				      * This will not get any of the generated messages.
				      */
		struct Client* sptr, /* Client exiting */
		struct Client* from, /* Client firing off this Exit, never NULL! */
		const char* comment  /* Reason for the exit */
		)
{
  struct Client        *acptr;
  struct Client        *next;
  time_t        on_for;
  char comment1[HOSTLEN + HOSTLEN + 2];
  int i;

  if (sptr == burst_in_progress)
    burst_in_progress = NULL;

  if (IsClient(sptr) && !MyConnect(sptr))
    for (i = 0; user_mode_table[i].letter; i++)
      if (HasUmode(sptr, user_mode_table[i].mode))
	sptr->from->serv->umode_count[user_mode_table[i].mode]--;

  if (IsPerson(sptr))
    {
      /* generate this for all client exits, except netsplit quits
       * (which would flood with useless information)
       * -- jilles */
      sendto_local_ops_flag(UMODE_CCONN,
			    "Client exiting: %s (%s@%s) [%s] [%s] [%s]",
			    sptr->name, sptr->username, sptr->host,
			    comment,
			    sptr->sockhost, sptr->servptr ? sptr->servptr->name : "<null>");
    }

  if (MyConnect(sptr))
    {
      fdlist_delete(sptr->fd, FDL_OPER | FDL_BUSY);
#if 0
      if (HasUmode(sptr,UMODE_OPER))
        {
          /* LINKLIST */
          /* oh for in-line functions... */
          {
            struct Client *prev_cptr=(struct Client *)NULL;
            struct Client *cur_cptr = oper_cptr_list;
            while(cur_cptr) 
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_oper_client = cur_cptr->next_oper_client;
                    else
                      oper_cptr_list = cur_cptr->next_oper_client;
                    cur_cptr->next_oper_client = (struct Client *)NULL;
                    break;
                  }
                else
                  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_oper_client;
              }
          }
        }
#endif
      if (IsClient(sptr))
        {
          if (!IsHoneypot(sptr))
            Count.local--;

          /* LINKLIST */
          /* oh for in-line functions... */
          if(IsPerson(sptr))        /* a little extra paranoia */
            {
              if(sptr->previous_local_client)
                sptr->previous_local_client->next_local_client =
                  sptr->next_local_client;
              else
                {
                  if(local_cptr_list == sptr)
                    {
                      local_cptr_list = sptr->next_local_client;
                    }
                }

              if(sptr->next_local_client)
                sptr->next_local_client->previous_local_client =
                  sptr->previous_local_client;

              sptr->previous_local_client = sptr->next_local_client = 
                (struct Client *)NULL;
            }
        }
      if (IsServer(sptr))
        {
          Count.myserver--;
          fdlist_delete(sptr->fd, FDL_SERVER | FDL_BUSY);

	  if (sptr->serv->nline)
	    {
	      if (ClassPtr(sptr->serv->nline))
		servers_connected_in_class[ConfClassType(sptr->serv->nline)]--;
	    }
          /* LINKLIST */
          /* oh for in-line functions... */
          {
            struct Client *prev_cptr = NULL;
            struct Client *cur_cptr = serv_cptr_list;
            while(cur_cptr)
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_server_client =
                        cur_cptr->next_server_client;
                    else
                      serv_cptr_list = cur_cptr->next_server_client;
                    cur_cptr->next_server_client = NULL;
                    break;
                  }
                else
                  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_server_client;
              }
          }
        }
      sptr->flags |= FLAGS_CLOSING;
          on_for = CurrentTime - sptr->firsttime;
#if defined(SYSLOG_USERS)
          if (IsPerson(sptr))
            logprintf(L_INFO, "%s (%3ld:%02ld:%02ld): %s!%s@%s %ld/%ld\n",
                myctime(sptr->firsttime),
                on_for / 3600, (on_for % 3600)/60,
                on_for % 60, sptr->name,
                sptr->username, sptr->host,
                sptr->sendK, sptr->receiveK);
#else
          {
            char        linebuf[300];
            static int        logfile = -1;
            static long        lasttime;

            /*
             * This conditional makes the logfile active only after
             * it's been created - thus logging can be turned off by
             * removing the file.
             *
             * stop NFS hangs...most systems should be able to open a
             * file in 3 seconds. -avalon (curtesy of wumpus)
             *
             * Keep the logfile open, syncing it every 10 seconds
             * -Taner
             */
            if (IsPerson(sptr))
              {
                if (logfile == -1)
                  {
                    logfile = open(user_log_file, O_WRONLY|O_APPEND);
                  }
                ircsnprintf(linebuf, 300,
			    "%s (%3d:%02d:%02d): %s!%s@%s %d/%d\n",
                            myctime(sptr->firsttime), on_for / 3600,
                            (on_for % 3600)/60, on_for % 60,
                            sptr->name,
                            sptr->username,
                            sptr->host,
                            sptr->sendK,
                            sptr->receiveK);
                write(logfile, linebuf, strlen(linebuf));
                /*
                 * Resync the file evey 10 seconds
                 */
                if (CurrentTime - lasttime > 10)
                  {
                    close(logfile);
                    logfile = -1;
                    lasttime = CurrentTime;
                  }
              }
          }
#endif
          if (sptr->fd >= 0)
            {
	      /* This uses that sptr->user is only set upon receiving USER.
	       * A silly server that sends USER for itself could disclose
	       * its IP this way. But it's useful to show banned users
	       * their IP. -- jilles */
	      sendto_one(sptr, "ERROR :Closing Link: %s (%s)",
			 sptr->user ? sptr->host : "0.0.0.0", comment);
            }
          /*
          ** Currently only server connections can have
          ** depending remote clients here, but it does no
          ** harm to check for all local clients. In
          ** future some other clients than servers might
          ** have remotes too...
          **
          ** Close the Client connection first and mark it
          ** so that no messages are attempted to send to it.
          ** (The following *must* make MyConnect(sptr) == FALSE!).
          ** It also makes sptr->from == NULL, thus it's unnecessary
          ** to test whether "sptr != acptr" in the following loops.
          */

          close_connection(sptr);
    }

  if(IsServer(sptr))
    {        
#ifdef SERVERHIDE
      strncpy_irc(comment1, me.name, HOSTLEN + 1);
      strcat(comment1, " ");
      strcat(comment1, NETWORK_NAME);
#else 
      /* I'm paranoid -Dianora */
      if((sptr->serv) && (sptr->serv->up))
        strncpy_irc(comment1, sptr->serv->up, HOSTLEN + 1);
      else
        strncpy_irc(comment1, "<Unknown>", HOSTLEN + 1);

      strcat(comment1," ");
      strcat(comment1, sptr->name);
#endif

      remove_dependents(cptr, sptr, from, comment, comment1);

      if (sptr->servptr == &me)
        {
          sendto_ops_flag(UMODE_SEEROUTING, "%s was connected to %s for %.1ld seconds.  %d/%d sendK/recvK. (%s)",
			  sptr->name, (char*) &me.name, CurrentTime - sptr->firsttime,
			  sptr->sendK, sptr->receiveK, comment);
          logprintf(L_NOTICE, "%s was connected for %.1ld seconds.  %d/%d sendK/recvK. (%s)",
              sptr->name, CurrentTime - sptr->firsttime, 
              sptr->sendK, sptr->receiveK, comment);

              /* Just for paranoia... this shouldn't be necessary if the
              ** remove_dependents() stuff works, but it's still good
              ** to do it.    MyConnect(sptr) has been set to false,
              ** so we look at servptr, which should be ok  -orabidoo
              */
              for (acptr = GlobalClientList; acptr; acptr = next)
                {
                  next = acptr->next;
                  if (!IsServer(acptr) && acptr->from == sptr)
                    {
                      ts_warn("Dependent client %s not on llist!?",
                              acptr->name);
                      exit_one_client(NULL, acptr, &me, comment1);
                    }
                }
              /*
              ** Second SQUIT all servers behind this link
              */
              for (acptr = GlobalClientList; acptr; acptr = next)
                {
                  next = acptr->next;
                  if (IsServer(acptr) && acptr->from == sptr)
                    {
                      ts_warn("Dependent server %s not on llist!?", 
                                     acptr->name);
                      exit_one_client(NULL, acptr, &me, me.name);
                    }
                }
            }
        }

  exit_one_client(cptr, sptr, from, comment);
  return cptr == sptr ? CLIENT_EXITED : 0;
}

/*
 * Count up local client memory
 */
void count_local_client_memory(size_t *local_client_memory_used,
                               size_t *local_client_memory_allocated,
			       size_t *local_client_memory_overheads)
{
  BlockHeapCountMemory(localClientFreeList,
		       local_client_memory_used,
		       local_client_memory_allocated,
		       local_client_memory_overheads);
}

/*
 * Count up remote client memory
 */
void count_remote_client_memory(size_t *remote_client_memory_used,
				size_t *remote_client_memory_allocated,
				size_t *remote_client_memory_overheads)
{
  BlockHeapCountMemory(remoteClientFreeList,
		       remote_client_memory_used,
		       remote_client_memory_allocated,
		       remote_client_memory_overheads);
}

int user_dumper(char *output, struct User *new_user)
{
  static int line = 0;
  static struct User *user;

  if (new_user)
    {
      user = new_user;
      line = 0;
    }

  if (!user)
    {
      line = -1;
      return 0;
    }

  switch(line++)
    {
    case  0: ircsnprintf(output, 1024, "%p: struct User", user); break;
    case  1: ircsnprintf(output, 1024, "away %p, '%s'", user->away, user->away); break;
#ifdef HAVE_LONG_LONG
    case  2: ircsnprintf(output, 1024, "last %lld, last_sent %lld, refcnt %d, joined %d, logcount %d",
                         (long long)user->last, (long long)user->last_sent, user->refcnt, user->joined, /*user->logcount*/ 0); break;
#else
    case  2: ircsnprintf(output, 1024, "last %ld, last_sent %ld, refcnt %d, joined %d, logcount %d",
                         (long)user->last, (long)user->last_sent, user->refcnt, user->joined, /*user->logcount*/ 0); break;
#endif
    case  3: ircsnprintf(output, 1024, "*v* Entering SLink chain channel"); break;
    case  4: if (!slink_channel_dumper(output, user->channel)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
    case  5: if (slink_channel_dumper(output, NULL)) {line--; break;} else line++;
    case  6: ircsnprintf(output, 1024, "*v* Entering SLink chain logging"); break;
/*     case  7: if (!slink_channel_dumper(output, user->logging)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break; */
    case  7: line++;
    case  8: if (slink_channel_dumper(output, NULL)) {line--; break;} else line++;
    case  9: ircsnprintf(output, 1024, "*v* Entering SLink chain invites"); break;
    case 10: if (!slink_channel_dumper(output, user->invited)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
    case 11: if (slink_channel_dumper(output, NULL)) {line--; break;} else line++;
    case 12: ircsnprintf(output, 1024, "*v* Entering SLink chain silence"); break;
    case 13: if (!slink_string_dumper(output, user->silence)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
    case 14: if (slink_string_dumper(output, NULL)) {line--; break;} else line++;
    case 15: ircsnprintf(output, 1024, "server '%s'", user->server); break;
    default:
      user = NULL;
      line = -1;
      return 0;
    }

  return 1;
}

int server_dumper(char *output, struct Server *new_server)
{
  static int line = 0;
  static struct Server *server;

  if (new_server)
    {
      server = new_server;
      line = 0;
    }

  if (!server)
    {
      line = -1;
      return 0;
    }

  switch(line++)
    {
    case 0: ircsnprintf(output, 1024, "%p: struct Server", server); break;
    case 1: ircsnprintf(output, 1024, "*v* Entering struct User user"); break;
    case 2: if (!user_dumper(output, server->user)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
    case 3: if (user_dumper(output, NULL)) {line--; break;} else line++;
    case 4: ircsnprintf(output, 1024, "up '%s', by '%s', tsversion %d", server->up, server->by, server->tsversion); break;
    case 5: ircsnprintf(output, 1024, "nline %p (%s)", server->nline, server->nline ? server->nline->name : ""); break;
    case 6: ircsnprintf(output, 1024, "servers %p (%s)", server->servers, server->servers ? server->servers->name : ""); break;
    case 7: ircsnprintf(output, 1024, "users %p (%s)", server->users, server->users ? server->users->name : ""); break;
    case 8: ircsnprintf(output, 1024, "umode_count {");
      {
        int i, comma = 0;
        for (i = 0; user_mode_table[i].letter; i++)
          {
            if (comma)
              strcat(output, ",");
            comma = 1;
            ircsnprintf(output + strlen(output), 1024 - strlen(output), "(%c,%d)", user_mode_table[i].letter, server->umode_count[i]);
          }
        strcat(output, "}");
      }
    default:
      server = NULL;
      line = -1;
      return 0;
    }

  return 1;
}

int client_dumper(char *output, struct Client *new_cptr)
{
  static int line = 0;
  static struct Client *cptr;
  struct {const char *name; int stat;} *current_status, statii[] =
    {
      {"connecting", 0x01},
      {"handshake", 0x02},
      {"me", 0x04},
      {"unknown", 0x08},
      {"server", 0x10},
      {"client", 0x20},
      {NULL, 0}
    };

  if (new_cptr)
    {
      cptr = new_cptr;
      line = 0;
    }

  if (!cptr)
    {
      line = -1;
      return 0;
    }

  switch (line++)
    {
    case 0: ircsnprintf(output, 1024, "%p: struct Client", cptr); break;
    case 1:
      {
	int status, comma = 0;
	ircsnprintf(output, 1024, "%s!%s@%s, status %#x (", cptr->name, cptr->username, cptr->host, cptr->status);
	status = cptr->status;
	for (current_status = statii; current_status->stat; current_status++)
	  {
	    if (status & current_status->stat)
	      {
		if (comma)
		  strcat(output, ",");
		strcat(output, current_status->name);
		comma = 1;
		status &= ~current_status->stat;
	      }
	  }
	strcat(output, ")");
	if (status)
	  ircsnprintf(output + strlen(output), 1024 - strlen(output), " unknown components %#x", status);
	break;
      }
    case  2: ircsnprintf(output, 1024, "next %p (%s)", cptr->next, cptr->next ? cptr->next->name : ""); break;
    case  3: ircsnprintf(output, 1024, "prev %p (%s)", cptr->prev, cptr->prev ? cptr->prev->name : ""); break;
    case  4: ircsnprintf(output, 1024, "hnext %p (%s)", cptr->hnext, cptr->hnext ? cptr->hnext->name : ""); break;
    case  5: ircsnprintf(output, 1024, "lnext %p (%s)", cptr->lnext, cptr->lnext ? cptr->lnext->name : ""); break;
    case  6: ircsnprintf(output, 1024, "lprev %p (%s)", cptr->lprev, cptr->lprev ? cptr->lprev->name : ""); break;
    case  7: ircsnprintf(output, 1024, "next_local_client %p (%s)", 
			 cptr->next_local_client, cptr->next_local_client ? cptr->next_local_client->name : ""); break;
    case  8: ircsnprintf(output, 1024, "previous_local_client %p (%s)",
			 cptr->previous_local_client,
			 cptr->previous_local_client ? cptr->previous_local_client->name : ""); break;
    case  9: ircsnprintf(output, 1024, "next_server_client %p (%s)",
			 cptr->next_server_client, cptr->next_server_client ? cptr->next_server_client->name : ""); break;
    case 10: ircsnprintf(output, 1024, "servptr %p (%s)", cptr->servptr, cptr->servptr ? cptr->servptr->name : ""); break;
    case 11: ircsnprintf(output, 1024, "from %p (%s)", cptr->from, cptr->from ? cptr->from->name : ""); break;
#ifdef HAVE_LONG_LONG
    case 12: ircsnprintf(output, 1024, "lasttime %.1lld, firsttime %.1lld, since %.1lld, tsinfo %.1lld",
			 (long long)cptr->lasttime, (long long)cptr->firsttime, (long long)cptr->since, (long long)cptr->tsinfo); break;
#else
    case 12: ircsnprintf(output, 1024, "lasttime %.1ld, firsttime %.1ld, since %.1ld, tsinfo %.1ld",
			 (long)cptr->lasttime, (long)cptr->firsttime, (long)cptr->since, (long)cptr->tsinfo); break;
#endif
    case 13: ircsnprintf(output, 1024, "umodes %s", umodes_as_string(&cptr->umodes)); break;
    case 14: ircsnprintf(output, 1024, "allowed_umodes %s", umodes_as_string(&cptr->allowed_umodes)); break;
    case 15: ircsnprintf(output, 1024, "flags %lx, flags2 %lx", cptr->flags, cptr->flags2); break;
    case 16: ircsnprintf(output, 1024, "fd %d, hopcount %d, nicksent %d, listprogress %d, listprogress2 %d",
			 cptr->fd, cptr->hopcount, cptr->nicksent, cptr->listprogress, cptr->listprogress2); break;
    case 17: ircsnprintf(output, 1024, "origname '%s', info '%s'", cptr->origname, cptr->info); break;
    case 18: ircsnprintf(output, 1024, "dnshost '%s', spoofhost '%s', sockhost '%s'",
			 cptr->dnshost, cptr->spoofhost, cptr->sockhost); break;
    case 19: ircsnprintf(output, 1024, "ping_send_time %.1lu.%.1lu, ping_time %.1lu.%.1lu",
			 cptr->ping_send_time.tv_sec, cptr->ping_send_time.tv_usec,
			 cptr->ping_time.tv_sec, cptr->ping_time.tv_usec); break;
/*     case 20: ircsnprintf(output, 1024, "iline %p (%s@%s)", */
/*                          cptr->iline, cptr->iline ? cptr->iline->user : "", */
/*                          cptr->iline, cptr->iline ? cptr->iline->host : ""); break; */
    case 20: line++;
    case 21: ircsnprintf(output, 1024, "local_flag %d", cptr->local_flag); if (!cptr->local_flag) line = -1; break;
    case 22: ircsnprintf(output, 1024, "*v* Entering struct User user"); break;
      /* I like this. First time around, skip the next line if there is no more data (ie, no data at all) */
    case 23: if (!user_dumper(output, cptr->user)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
      /* Second time around, repeat this step if there is more data */
    case 24: if (user_dumper(output, NULL)) {line--; break;} else line++;
    case 25: ircsnprintf(output, 1024, "*v* Entering struct Server serv"); break;
    case 26: if (!server_dumper(output, cptr->serv)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
    case 27: if (server_dumper(output, NULL)) {line--; break;} else line++;
      /* Below this point is local clients only */
    case 28: ircsnprintf(output, 1024, "sendM %ud, sendK %ud, receiveM %ud, receiveK %ud, sendB %ud, receiveB %ud",
			 cptr->sendM, cptr->sendK, cptr->receiveM, cptr->receiveK, cptr->sendB, cptr->receiveB); break;
#ifdef HAVE_LONG_LONG
    case 29: ircsnprintf(output, 1024, "lastrecvM %ud, priority %d, port %d, caps %d, last_knock %.1lld",
			 cptr->lastrecvM, cptr->priority, cptr->port, cptr->caps, (long long)cptr->last_knock); break;
#else
    case 29: ircsnprintf(output, 1024, "lastrecvM %ud, priority %d, port %d, caps %d, last_knock %.1ld",
			 cptr->lastrecvM, cptr->priority, cptr->port, cptr->caps, (long)cptr->last_knock); break;
#endif
    case 30: ircsnprintf(output, 1024, "listener %p (%s)",
			 cptr->listener, cptr->listener ? cptr->listener->name : ""); break;
    case 31: ircsnprintf(output, 1024, "confs %p", cptr->confs); break;
    case 32: ircsnprintf(output, 1024, "*v* Entering SLink chain confs"); break;
    case 33: if (!slink_conf_dumper(output, cptr->confs)) {ircsnprintf(output, 1024, "*!* No content"); line++;} break;
      /* If we don't have any text, execute the next step instead (and increment line so we don't get it twice) */
    case 34: if (slink_conf_dumper(output, NULL)) {line--; break;} else line++;
    case 35: ircsnprintf(output, 1024, "dns_reply %p", cptr->dns_reply); break;
    case 36: ircsnprintf(output, 1024, "passwd '%s', response '%s'", cptr->passwd, cptr->response); break;
    default:
      cptr = NULL;
      line = -1;
      return 0;
    }
  return 1;
}

/* Here's a state machine for ya' */
int dump_global_clients(char *output, const char *host_mask, const char *name_mask, int active)
{
  static struct Client *cptr = NULL;
  static int starting = 1;
  static int first = 0;

  /* starting is true on the first call */
  if (starting && output)
    {
      ircsnprintf(output, 1024, "*-* Starting dump of global clients");
      starting = 0;
      cptr = GlobalClientList;
      first = 1;
      return 1;
    }

  /* !output is the reset call (not currently used), !cptr should never be true */
  if (!output || !cptr)
    {
      cptr = NULL;
      starting = 1;
      return 0;
    }

  /* If this is the first line of this cptr, we might need to skip over some uninteresting ones */
  if (first)
    while (cptr && ((host_mask && !match(host_mask, cptr->host)) ||
                    (name_mask && !match(name_mask, cptr->name)) ||
                    /* True if active == 1 and client is unregistered, or if active == -1 and client is registered */
                    (active && (IsRegistered(cptr) ^ (active == 1)))))
      cptr = cptr->next;

  if (!cptr)
    {
      starting = 1;
      return 0;
    }

  /* call if with NULL on all non-first calls */
  if (!client_dumper(output, first ? cptr : NULL))
    {
      cptr = cptr->next;
      first = 1;
      if (cptr)
        ircsnprintf(output, 1024, "*.* Next global client");
      else
        {
          starting = 1;
          return 0;
        }
    }
  else
    first = 0;

  return 1;
}
