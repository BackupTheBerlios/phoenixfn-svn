/************************************************************************
 *   IRC - Internet Relay Chat, admin/varparse.c
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

#include "varparse.h"
#include "irc_string.h"
#include "send.h"
#include "ircd.h"
#include "numeric.h"

#include <string.h>

/* This is the full name of the current tier (for use in help messages) */
static char full_name[BUFSIZE];

/* 16 should be way more than ever gets used */
static char *variable_parameters[16];

/* Recurse along the variable name, matching where possible */

static int
variable_parse_recurse(struct Client *cptr, struct Client *sptr, int parc, char *parv[], 
		       struct variable_tier *parse_tree, const char *command)
{
  char *tier_name = NULL;
  struct variable_tier *tier = NULL;
  int oldlen = strlen(full_name);

  if (parc > 1)
    {
      char *p;
      tier_name = parv[1];
      if ((p = strchr(parv[1], '.')))
	{
	  *p = '\0';
	  parv[1] = p + 1;
	}
      else
	parv[1] += strlen(parv[1]);
      strcat(full_name, tier_name);
      strcat(full_name, ".");
    }

  if (tier_name)
    for (tier = parse_tree; tier->name; tier++)
      if (!irccmp(tier->name, tier_name))
	break;

  if (tier)
    {
      if (tier->next_tier)
	return variable_parse_recurse(cptr, sptr, parc, parv, tier->next_tier, command);
      if (tier->handler)
	return tier->handler(cptr, sptr, parc, parv, variable_parameters);
    }

  /* If we got this far, this variable is not valid. Show the help at this tier */

  /* Strip any new junk from full_name */
  full_name[oldlen] = '\0';

  if (MyClient(sptr))
    {
      for (tier = parse_tree; tier->name; tier++)  
	sendto_one(sptr, form_str(RPL_OPTION), me.name, sptr->name,
		   command, full_name, tier->name);
      sendto_one(sptr, form_str(RPL_ENDOPTIONS), me.name, sptr->name,
		 command, full_name);
    }

  return -1;
}

int
variable_parse(struct Client *cptr, struct Client *sptr, int parc, char *parv[], 
	       struct variable_tier *parse_tree, const char *command)
{
  full_name[0] = '.';
  full_name[1] = '\0';
  return variable_parse_recurse(cptr, sptr, parc, parv, parse_tree, command);
}
