/************************************************************************
 *   IRC - Internet Relay Chat, src/hash.c
 *   Copyright (C) 1991 Darren Reed
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

#include "s_conf.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "paths.h"
#include "send.h"
#include "struct.h"
#include "s_debug.h"
#include "m_commands.h"
#include "s_log.h"

#include <assert.h>
#include <fcntl.h>     /* O_RDWR ... */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

/* New hash code */
/*
 * Contributed by James L. Davis
 */

#ifdef  DEBUGMODE
static struct HashEntry* clientTable = NULL;
static struct HashEntry* channelTable = NULL;
static int clhits;
static int clmiss;
static int chhits;
static int chmiss;
#else

static struct HashEntry clientTable[U_MAX];
static struct HashEntry channelTable[CH_MAX];

#endif

struct HashEntry hash_get_channel_block(int i)
{
  return channelTable[i];
}

size_t hash_get_channel_table_size(void)
{
  return sizeof(struct HashEntry) * CH_MAX;
}

size_t hash_get_client_table_size(void)
{
  return sizeof(struct HashEntry) * U_MAX;
}

/*
 *
 * look in whowas.c for the missing ...[WW_MAX]; entry
 *   - Dianora
 */

/*
 * Hashing.
 *
 *   The server uses a chained hash table to provide quick and efficient
 * hash table mantainence (providing the hash function works evenly over
 * the input range).  The hash table is thus not susceptible to problems
 * of filling all the buckets or the need to rehash.
 *    It is expected that the hash table would look somehting like this
 * during use:
 *                   +-----+    +-----+    +-----+   +-----+
 *                ---| 224 |----| 225 |----| 226 |---| 227 |---
 *                   +-----+    +-----+    +-----+   +-----+
 *                      |          |          |
 *                   +-----+    +-----+    +-----+
 *                   |  A  |    |  C  |    |  D  |
 *                   +-----+    +-----+    +-----+
 *                      |
 *                   +-----+
 *                   |  B  |
 *                   +-----+
 *
 * A - GOPbot, B - chang, C - hanuaway, D - *.mu.OZ.AU
 *
 * The order shown above is just one instant of the server.  Each time a
 * lookup is made on an entry in the hash table and it is found, the entry
 * is moved to the top of the chain.
 *
 *    ^^^^^^^^^^^^^^^^ **** Not anymore - Dianora
 *
 */

unsigned int hash_nick_name(const char* name)
{
  unsigned int h = 0;

  while (*name)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }

  return(h & (U_MAX - 1));
}

/*
 * hash_channel_name
 *
 * calculate a hash value on at most the first 30 characters of the channel
 * name. Most names are short than this or dissimilar in this range. There
 * is little or no point hashing on a full channel name which maybe 255 chars
 * long.
 */
unsigned int hash_channel_name(const char* name)
{
  register int i = 30;
  unsigned int h = 0;

  while (*name && --i)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }

  return (h & (CH_MAX - 1));
}

/*
 * clear_client_hash_table
 *
 * Nullify the hashtable and its contents so it is completely empty.
 */
static void clear_client_hash_table(void)
{
#ifdef        DEBUGMODE
  clhits = 0;
  clmiss = 0;
  if(!clientTable)
    {
      expect_malloc;
      clientTable = (struct HashEntry*) MyMalloc(U_MAX * 
                                                 sizeof(struct HashEntry));
      malloc_log("clear_client_hash_table() allocating %d struct HashEntrys (%zd bytes) at %p",
                 U_MAX, U_MAX * sizeof(struct HashEntry), clientTable);
    }
#endif
  memset(clientTable, 0, sizeof(struct HashEntry) * U_MAX);
}

static void clear_channel_hash_table(void)
{
#ifdef        DEBUGMODE
  chmiss = 0;
  chhits = 0;
  if (!channelTable)
    {
      expect_malloc;
      channelTable = (struct HashEntry*) MyMalloc(CH_MAX *
                                                  sizeof(struct HashEntry));
      malloc_log("clear_channel_hash_table() allocating %d struct HashEntrys (%zd bytes) at %p",
                 CH_MAX, CH_MAX * sizeof(struct HashEntry), channelTable);
    }
#endif
  memset(channelTable, 0, sizeof(struct HashEntry) * CH_MAX);
}

void init_hash(void)
{
  clear_client_hash_table();
  clear_channel_hash_table();
}

/*
 * add_to_client_hash_table
 */
void add_to_client_hash_table(const char* name, struct Client* cptr)
{
  struct Client *acptr;
  unsigned int hashv;
  assert(0 != name);
  assert(0 != cptr);

  if ((acptr = hash_find_client(name, NULL)))
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: Already got client %s in hash table, but something tried to add it again. Dumping core...",
		      name);
      logprintf(L_WARN, "WTF: Already got %s in hash table but something tried to add it again, dumping core",
	  name);
      flush_connections(0);
      abort();
    }
  hashv = hash_nick_name(name);
  if ((clientTable[hashv].links == 0) && (clientTable[hashv].list != NULL))
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: clientTable[%d].links == 0 but list not NULL. Whacking list...",
		      hashv);
      logprintf(L_WARN, "WTF: clientTable[%d].links == 0 but list not NULL. Whacking list...",
	  hashv);
      clientTable[hashv].list = NULL;
    }
  cptr->hnext = (struct Client*) clientTable[hashv].list;
  clientTable[hashv].list = (void*) cptr;
  ++clientTable[hashv].links;
  ++clientTable[hashv].hits;
}

/*
 * add_to_channel_hash_table
 */
void add_to_channel_hash_table(const char* name, struct Channel* chptr)
{
  struct Channel *achptr;
  unsigned int hashv;
  assert(0 != name);
  assert(0 != chptr);

  if ((achptr = hash_find_channel(name, NULL)))
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: Already got %s in hash table but something tried to add it again, dumping core",
		      name);
      logprintf(L_WARN, "WTF: Already got %s in hash table but something tried to add it again, dumping core",
	  name);
      flush_connections(0);
      abort();
    }
  hashv = hash_channel_name(name);
  if ((channelTable[hashv].links == 0) && (channelTable[hashv].list != NULL))
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: channelTable[%d].links == 0 but list not NULL. Whacking list...",
		      hashv);
      logprintf(L_WARN, "WTF: channelTable[%d].links == 0 but list not NULL. Whacking list...",
	  hashv);
      channelTable[hashv].list = NULL;
    }
  chptr->hnextch = (struct Channel*) channelTable[hashv].list;
  channelTable[hashv].list = (void*) chptr;
  ++channelTable[hashv].links;
  ++channelTable[hashv].hits;
}

/*
 * del_from_client_hash_table - remove a client/server from the client
 * hash table
 */
void del_from_client_hash_table(const char* name, struct Client* cptr)
{
  struct Client* tmp;
  struct Client* prev = NULL;
  int i;
  unsigned int   hashv;
  assert(0 != name);
  assert(0 != cptr);

  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;
  i = clientTable[hashv].links;

  for ( ; tmp; tmp = tmp->hnext)
    {
      if (i == 0)
	{
	  sendto_ops_flag(UMODE_DEBUG, "WTF: Didn't find %s in client hash table while trying to delete",
			  name);
	  logprintf(L_WARN, "WTF: Didn't find %s in client hash table while trying to delete",
	      name);
	  return;
	}
      if (tmp == cptr)
        {
          if (prev)
            prev->hnext = tmp->hnext;
          else
            clientTable[hashv].list = (void*) tmp->hnext;
          tmp->hnext = NULL;

	  /* If this is the last entry in the list, then the list has become empty.
	   * If it is not NULL, we're in trouble
	   */
	  /* Note that we are about to decrement links */
	  if (i == 1)
	    {
	      if (prev && (prev->hnext != NULL))
		{
		  sendto_ops_flag(UMODE_DEBUG, "WTF: prev->hnext != NULL but all elements deleted. NULLing...");
		  logprintf(L_WARN, "WTF: prev->hnext != NULL but all elements deleted. NULLing...");
		  prev->hnext = NULL;
		}
	      if (!prev && (clientTable[hashv].list != NULL))
		{
		  sendto_ops_flag(UMODE_DEBUG, "WTF: clientTable[%d].list != NULL but all elements deleted. NULLing...",
				  hashv);
		  logprintf(L_WARN, "WTF: clientTable[%d].list != NULL but all elements deleted. NULLing...",
		      hashv);
		  clientTable[hashv].list = NULL;
		}
	    }

          assert(clientTable[hashv].links > 0);
          if (clientTable[hashv].links > 0)
            --clientTable[hashv].links;
	  return;
        }
      prev = tmp;
      i--;
    }
  Debug((DEBUG_ERROR, "%#x !in tab %s[%s] %#x %#x %#x %d %d %#x",
         cptr, cptr->name, cptr->from ? cptr->from->host : "??host",
         cptr->from, cptr->next, cptr->prev, cptr->fd, 
         cptr->status, cptr->user));
}

/*
 * del_from_channel_hash_table
 */
void del_from_channel_hash_table(const char* name, struct Channel* chptr)
{
  struct Channel* tmp;
  struct Channel* prev = NULL;
  int i;
  unsigned int    hashv;
  assert(0 != name);
  assert(0 != chptr);

  hashv = hash_channel_name(name);
  tmp = (struct Channel*) channelTable[hashv].list;
  i = channelTable[hashv].links;

  for ( ; tmp; tmp = tmp->hnextch)
    {
      if (i == 0)
	{
	  sendto_ops_flag(UMODE_DEBUG, "WTF: Didn't find %s in channel hash table while trying to delete",
			  name);
	  logprintf(L_WARN, "WTF: Didn't find %s in channel hash table while trying to delete",
	      name);
	  return;
	}
      if (tmp == chptr)
        {
          if (prev)
            prev->hnextch = tmp->hnextch;
          else
            channelTable[hashv].list = (void*) tmp->hnextch;
          tmp->hnextch = NULL;

	  /* If this is the last entry in the list, then the list has become empty.
	   * If it is not NULL, we're in trouble
	   */
	  /* Note that we are about to decrement links */
	  if (i == 1)
	    {
	      if (prev && (prev->hnextch != NULL))
		{
		  sendto_ops_flag(UMODE_DEBUG, "WTF: prev->hnextch != NULL but all elements deleted. NULLing...");
		  logprintf(L_WARN, "WTF: prev->hnextch != NULL but all elements deleted. NULLing...");
		  prev->hnextch = NULL;
		}
	      if (!prev && (channelTable[hashv].list != NULL))
		{
		  sendto_ops_flag(UMODE_DEBUG, "WTF: channelTable[%d].list != NULL but all elements deleted. NULLing...",
				  hashv);
		  logprintf(L_WARN, "WTF: channelTable[%d].list != NULL but all elements deleted. NULLing...",
		      hashv);
		  channelTable[hashv].list = NULL;
		}
	    }

          assert(channelTable[hashv].links > 0);
          if (channelTable[hashv].links > 0)
            --channelTable[hashv].links;
          return;
        }
      prev = tmp;
      i--;
    }
}


/*
 * hash_find_client
 */
struct Client* hash_find_client(const char* name, struct Client* cptr)
{
  struct Client* tmp;
  unsigned int   hashv;
  assert(0 != name);

  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;
  /*
   * Got the bucket, now search the chain.
   */
  for ( ; tmp; tmp = tmp->hnext)
    {
      if (irccmp(name, tmp->name) == 0)
	{
#ifdef        DEBUGMODE
	  ++clhits;
#endif
	  return tmp;
	}
#ifdef        DEBUGMODE
      ++clmiss;
#endif
    }
  return cptr;

  /*
   * If the member of the hashtable we found isnt at the top of its
   * chain, put it there.  This builds a most-frequently used order into
   * the chains of the hash table, giving speedier lookups on those nicks
   * which are being used currently.  This same block of code is also
   * used for channels and servers for the same performance reasons.
   *
   * I don't believe it does.. it only wastes CPU, lets try it and
   * see....
   *
   * - Dianora
   */
}

/*
 * Whats happening in this next loop ? Well, it takes a name like
 * foo.bar.edu and proceeds to earch for *.edu and then *.bar.edu.
 * This is for checking full server names against masks although
 * it isnt often done this way in lieu of using matches().
 *
 * Rewrote to do *.bar.edu first, which is the most likely case,
 * also made const correct
 * --Bleep
 */
static struct Client* hash_find_masked_server(const char* name)
{
  char           buf[HOSTLEN + 1];
  char*          p = buf;
  char*          s;
  struct Client* server;

  if ('*' == *name || '.' == *name)
    return 0;

  /*
   * copy the damn thing and be done with it
   */
  strncpy_irc(buf, name, HOSTLEN + 1);
  buf[HOSTLEN] = '\0';

  while ((s = strchr(p, '.')) != 0)
    {
       *--s = '*';
      /*
       * Dont need to check IsServer() here since nicknames cant
       * have *'s in them anyway.
       */
      if ((server = hash_find_client(s, NULL)))
        return server;
      p = s + 2;
    }
  return 0;
}

/*
 * hash_find_server
 */
struct Client* hash_find_server(const char* name)
{
  struct Client* tmp;
  unsigned int   hashv;

  assert(0 != name);
  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnext)
    {
      if (!IsServer(tmp) && !IsMe(tmp))
        continue;
      if (irccmp(name, tmp->name) == 0)
        {
#ifdef        DEBUGMODE
          ++clhits;
#endif
          return tmp;
        }
    }
  
#ifndef DEBUGMODE
  return hash_find_masked_server(name);

#else /* DEBUGMODE */
  if (!(tmp = hash_find_masked_server(name)))
    ++clmiss;
  return tmp;
#endif
}

/*
 * find_channel
 */
struct Channel* hash_find_channel(const char* name, struct Channel* chptr)
{
  struct Channel*    tmp;
  unsigned int hashv;
  
  assert(0 != name);
  hashv = hash_channel_name(name);
  tmp = (struct Channel*) channelTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnextch)
    {
      if (irccmp(name, tmp->chname) == 0)
	{
#ifdef        DEBUGMODE
	  ++chhits;
#endif
	  return tmp;
	}
    }
#ifdef        DEBUGMODE
  ++chmiss;
#endif
  return chptr;
}

/*
 * NOTE: this command is not supposed to be an offical part of the ircd
 *       protocol.  It is simply here to help debug and to monitor the
 *       performance of the hash functions and table, enabling a better
 *       algorithm to be sought if this one becomes troublesome.
 *       -avalon
 *
 * partially rewritten (finally) -Dianora
 *
 * XXX - spaghetti still, sigh
 */

int m_hash(struct Client *cptr, struct Client *sptr,int parc,char *parv[])
{
  register int l;
  register int i;
  register struct HashEntry* tab;
  struct HashEntry* table;
  struct tm*        tmptr;
  int        deepest = 0;
  int   deeplink = 0;
  int   showlist = 0;
  int   tothits = 0;
  int        mosthit = 0;
  int   mosthits = 0;
  int   used = 0;
  int   used_now = 0;
  int   totlink = 0;
  int   size = U_MAX;
  char        ch;
  int   out = 0;
  int        link_pop[10];
  char  result_buf[256];
  char  hash_log_file[256];
  char  timebuffer[MAX_DATE_STRING];

  if (!MyClient(sptr) || !HasUmode(sptr,UMODE_DEBUG))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if(parc > 1)
    {
      if(!irccmp(parv[1],"iphash"))
        {
          iphash_stats(cptr,sptr,parc,parv,-1);
          return 0;
        }
      else if(!irccmp(parv[1],"Diphash"))
        {
          tmptr = localtime(&CurrentTime);
          strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d%H%M", tmptr);
          sprintf(hash_log_file, "%s.%s",
		  hash_log_file_base, timebuffer);

          if ((out = open(hash_log_file, O_RDWR | O_APPEND | O_CREAT,0664))==-1)
              sendto_one(sptr, ":%s NOTICE %s :Problem opening %s",
                         me.name, parv[0], hash_log_file);
          else
            sendto_one(sptr, ":%s NOTICE %s :Writing hash log to %s",
                       me.name, parv[0], hash_log_file);

          iphash_stats(cptr,sptr,parc,parv,out);
          return 0;
        }

      ch = *parv[1];
      if (IsLower(ch))
        {
          table = clientTable;
          
        }
      else
        {
          table = channelTable;
          size = CH_MAX;
        }
      if (ch == 'L' || ch == 'l')
        {
          tmptr = localtime(&CurrentTime);
          strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d%H%M", tmptr);
          sprintf(hash_log_file, "%s-%cdump.%s",
		  hash_log_file_base, ch, timebuffer);
          showlist = 1;
          if ((out = open(hash_log_file, O_RDWR|O_APPEND|O_CREAT,0664))==-1)
              sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
                         me.name, parv[0], hash_log_file);
          else
            sendto_one(sptr, ":%s NOTICE %s :Writing hash log to %s ",
                       me.name, parv[0], hash_log_file);
        }
    }
  else
    {
      ch = '\0';
      table = clientTable;
    }

  for (i = 0; i < 10; i++)
    link_pop[i] = 0;

  for (i = 0; i < size; i++)
    {
      tab = &table[i];
      l = tab->links;
      if (showlist)
        {
        /*
          sendto_one(sptr,
          "NOTICE %s :Hash Entry:%6d Hits:%7d Links:%6d",
          parv[0], i, tab->hits, l); */
          if(out >= 0)
            {
              sprintf(result_buf,"Hash Entry:%6d Hits;%7d Links:%6d\n",
                            i, tab->hits, l);
              write(out,result_buf,strlen(result_buf));
            }
        }

      if (l > 0)
        {
          if (l < 10)
            link_pop[l]++;
          else
            link_pop[9]++;
          used_now++;
          totlink += l;
          if (l > deepest)
            {
              deepest = l;
              deeplink = i;
            }
        }
      else
        link_pop[0]++;
      l = tab->hits;
      if (l)
        {
          used++;
          tothits += l;
          if (l > mosthits)
            {
              mosthits = l;
              mosthit = i;
            }
        }
    }
  if(showlist && (out >= 0))
     (void)close(out);

  switch((int)ch)
    {
    case 'V' : case 'v' :
      {
        register struct Client* acptr;
        int        bad = 0, listlength = 0;
        
        for (acptr = GlobalClientList; acptr; acptr = acptr->next) {
          if (hash_find_client(acptr->name,acptr) != acptr)
            {
              if (ch == 'V')
                sendto_one(sptr, "NOTICE %s :Bad hash for %s",
                           parv[0], acptr->name);
              bad++;
            }
          listlength++;
        }
        sendto_one(sptr,"NOTICE %s :List Length: %d Bad Hashes: %d",
                   parv[0], listlength, bad);
      }
    case 'P' : case 'p' :
      for (i = 0; i < 10; i++)
        sendto_one(sptr,"NOTICE %s :Entires with %d links : %d",
                   parv[0], i, link_pop[i]);
      return (0);
    case 'r' :
      {
        register        struct Client        *acptr;

        sendto_one(sptr,"NOTICE %s :Rehashing Client List.", parv[0]);
        clear_client_hash_table();
        for (acptr = GlobalClientList; acptr; acptr = acptr->next)
          add_to_client_hash_table(acptr->name, acptr);
        break;
      }
    case 'R' :
      {
        register        struct Channel        *acptr;

        sendto_one(sptr,"NOTICE %s :Rehashing Channel List.", parv[0]);
        clear_channel_hash_table();
        for (acptr = channel; acptr; acptr = acptr->nextch)
          (void)add_to_channel_hash_table(acptr->chname, acptr);
        break;
      }
    case 'H' :
      if (parc > 2)
        sendto_one(sptr,"NOTICE %s :%s hash to entry %d",
                   parv[0], parv[2],
                   hash_channel_name(parv[2]));
      return (0);
    case 'h' :
      if (parc > 2)
        sendto_one(sptr,"NOTICE %s :%s hash to entry %d",
                   parv[0], parv[2],
                   hash_nick_name(parv[2]));
      return (0);
    case 'n' :
      {
        struct Client        *tmp;
        int        max;
        
        if (parc <= 2)
          return (0);
        l = atoi(parv[2]) % U_MAX;
        if (parc > 3)
          max = atoi(parv[3]) % U_MAX;
        else
          max = l;
        for (;l <= max; l++)
          for (i = 0, tmp = (struct Client *)clientTable[l].list; tmp;
               i++, tmp = tmp->hnext)
            {
              if (parv[1][2] == '1' && tmp != tmp->from)
                continue;
              sendto_one(sptr,"NOTICE %s :Node: %d #%d %s",
                         parv[0], l, i, tmp->name);
            }
        return (0);
      }
    case 'N' :
      {
        struct Channel *tmp;
        int        max;

        if (parc <= 2)
          return (0);
        l = atoi(parv[2]) % CH_MAX;
        if (parc > 3)
          max = atoi(parv[3]) % CH_MAX;
        else
          max = l;
        for (;l <= max; l++)
          for (i = 0, tmp = (struct Channel*) channelTable[l].list; tmp;
               i++, tmp = tmp->hnextch)
            sendto_one(sptr,"NOTICE %s :Node: %d #%d %s",
                       parv[0], l, i, tmp->chname);
        return (0);
      }
#ifdef DEBUGMODE
    case 'S' :

      sendto_one(sptr,"NOTICE %s :Entries Hashed: %d NonEmpty: %d of %d",
                 parv[0], totlink, used_now, size);
      if (!used_now)
        used_now = 1;
      sendto_one(sptr,"NOTICE %s :Hash Ratio (av. depth): %f %%Full: %f",
                 parv[0], (float)((1.0 * totlink) / (1.0 * used_now)),
                 (float)((1.0 * used_now) / (1.0 * size)));
      sendto_one(sptr,"NOTICE %s :Deepest Link: %d Links: %d",
                 parv[0], deeplink, deepest);
      if (!used)
        used = 1;
      sendto_one(sptr,"NOTICE %s :Total Hits: %d Unhit: %d Av Hits: %f",
                 parv[0], tothits, size-used,
                 (float)((1.0 * tothits) / (1.0 * used)));
      sendto_one(sptr,"NOTICE %s :Entry Most Hit: %d Hits: %d",
                 parv[0], mosthit, mosthits);
      sendto_one(sptr,"NOTICE %s :Client hits %d miss %d",
                 parv[0], clhits, clmiss);
      sendto_one(sptr,"NOTICE %s :Channel hits %d miss %d",
                 parv[0], chhits, chmiss);
      return 0;
#endif
    }
  return 0;
}


