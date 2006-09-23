/************************************************************************
 *   IRC - Internet Relay Chat, admin/varparse.h
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

#include "config.h"
#include "client.h"

/* Variable parsing engine
 *  Variables are named with heirarchial structure
 *  FOO.BAR.BAZ is a three-tier name
 *  FOO.BAR=fred.BAZ is a three-tier with a parameter in the middle,
 *   for identifying specific records
 */

/* Extra parameter contains the inline parameters (fred in the above example ) */
typedef int var_handler_func(struct Client *, struct Client *, int, char **, char **);

struct variable_tier
{
  const char *name;
  int parameter; /* Do we have one at this level? */
  struct variable_tier *next_tier;
  var_handler_func *handler;
};

extern int variable_parse(struct Client *, struct Client *, int, char **, struct variable_tier *, const char *);
