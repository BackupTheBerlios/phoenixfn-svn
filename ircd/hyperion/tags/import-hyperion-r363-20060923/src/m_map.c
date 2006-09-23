/************************************************************************
 *   IRC - Internet Relay Chat, src/m_map.c
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
 */

#include "client.h"
#include "ircd.h"
#include "common.h"
#include "numeric.h"
#include "irc_string.h"
#include "send.h"
#include "s_conf.h"
#include "umodes.h"
#include "m_commands.h"
#include "s_serv.h"

#include <string.h>

static char header[(MAX_MAP_DEPTH*2) + 4 + 1];
static int header_length;

static void map_send_server(struct Client* sptr, char* sender_prefix, struct Client* current)
{
  struct Client* acptr;

  if (!current->serv)
    return; /* this should never happen, but let's be safe... */

  /* header should be formatted for us already */
  sendto_one(sptr, form_str(RPL_MAP), me.name, sender_prefix, header, current->name);

  /* Now, iterate over it's children */
  if (current->serv->servers)
    {
      /* Is this a child? */
      if (header_length > 0)
	{
	  /* OK, was our parent a tail? */
	  if (header[header_length - 2] == '`')
	    /* Yes, remove that from the string, it's no longer needed */
	    header[header_length - 2] = ' ';
	  /* Regardless, remove the - from it's current position */
	  header[header_length - 1] = ' ';
	}
      /* Add the marker for "more children" */
      strcat(header, "|-");
      header_length += 2;
      for (acptr = current->serv->servers; acptr->lnext; acptr = acptr->lnext) /* Break one hop early to get tail right */
	{
	  /* Some of these calls might eat this, add it back */
	  header[header_length - 1] = '-';
	  map_send_server(sptr, sender_prefix, acptr);
	}
      /* Convert | to `- in this one, it's my last child */
      header[header_length - 2] = '`';
      header[header_length - 1] = '-';
      map_send_server(sptr, sender_prefix, acptr);
      /* Remove the trailing "  " from the string now */
      header[header_length - 2] = '\0';
      header_length -= 2;
    }
}

int m_map(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem* conf;

  if (!HasUmode(sptr,UMODE_SEEROUTING))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no V umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if (parc > 1)
    if (hunt_server(cptr, sptr, ":%s MAP %s", 1, parc, parv) != HUNTED_ISME)
      return 0;

  header_length = 0;
  header[0] = '\0';
  map_send_server(sptr, parv[0], &me);
  for (conf = ConfigItemList; conf; conf = conf->next)
    {
      if (conf->status != CONF_NOCONNECT_SERVER)
	continue;
      if (strcmp(conf->name, me.name) == 0)
	continue;
      if (!find_server(conf->name))
	sendto_one(sptr, form_str(RPL_MAP), me.name, parv[0], "** ", conf->name, -1);
    }
  sendto_one(sptr,form_str(RPL_ENDOFMAP),me.name,parv[0]);

  return 0;
}
