/************************************************************************
 *   IRC - Internet Relay Chat, src/m_dump.c
 *   This file is copyright (C) 2002 Andrew Suffield
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
#include "dump.h"
#include "ircd.h"
#include "common.h"
#include "numeric.h"
#include "irc_string.h"
#include "send.h"
#include "s_conf.h"
#include "umodes.h"
#include "m_commands.h"
#include "s_serv.h"
#include "paths.h"
#include "fileio.h"

#include <time.h>

struct dump_entry
{
  const char *name, *not_name;
  dumper *func;
  int want;
};

#define DUMP(A,B) {(A), ("NOT-" A),(B),0},

static struct dump_entry dumpers[] =
  {
    DUMP("CLIENTS", &dump_global_clients)
    DUMP("",NULL)
  };

int m_dump(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int send = 0, file = 0, active = 0;
  const char *host_mask = NULL, *name_mask = NULL;
  struct dump_entry *current = NULL;
  const char *filename = default_dump_file;
  int i = 0;
  FBFILE *fbd = NULL;

  if (!HasUmode(sptr,UMODE_DEBUG))
    {
      if (SeesOperMessages(sptr))
	sendto_one(sptr,":%s NOTICE %s :You have no d umode", me.name, parv[0]);
      else
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc < 2)
    {
      if (!IsServer(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "DUMP");
      return 0;
    }

  for (current = dumpers; current->func; current++)
    current->want = 0;

  for (i = 1; i < parc; i++)
    {
      if (!irccmp(parv[i], "no-send"))
	send = 0;
      else if (!irccmp(parv[i], "send"))
	send = 1;
      else if (!irccmp(parv[i], "no-file"))
	file = 0;
      else if (!irccmp(parv[i], "file"))
	file = 1;
      else if (!irccmp(parv[i], "inactive"))
	active = -1;
      else if (!irccmp(parv[i], "active"))
	active = 1;
      else if (!irccmp(parv[i], "ignore-active"))
	active = 0;
      else if (!irccmp(parv[i], "all"))
        for (current = dumpers; current->func; current++)
          current->want = 1;
      else
        {
          int found = 0;
          for (current = dumpers; current->func; current++)
            {
              if (!irccmp(parv[i], current->name))
                {
                  current->want = 1;
                  found++;
                  break;
                }
              if (!irccmp(parv[i], current->not_name))
                {
                  current->want = 0;
                  found++;
                  break;
                }
            }
          if (!found && (i < (parc - 1)))
            {
              if (!irccmp(parv[i], "host-mask"))
                {
                  host_mask = parv[++i];
                }
              else if (!irccmp(parv[i], "name-mask"))
                {
                  name_mask = parv[++i];
                }
              else if (!irccmp(parv[i], "filename"))
                {
                  filename = parv[++i];
                }
            }
        }
    }

  if (file)
    {
      time_t t;
      if (!(fbd = fbopen(filename, "a")))
	{
          send_markup(sptr, &me, "DUMP", "*!* Failed to open dump file");
	  return 0;
	}
      fbputs("*****", fbd);
      fbputs("Beginning dump now", fbd);
      t = time(NULL);
      fbputs(ctime(&t), fbd);
      fbputs("*****", fbd);
    }

  if (send)
    send_markup(sptr, &me, "DUMP", "*** Beginning dump now");

  for (current = dumpers; current->func; current++)
    if (current->want)
      {
        char buffer[1024 + 1];
	while (current->func(buffer, host_mask, name_mask, active))
	  {
	    if (file)
	      fbputs(buffer, fbd);
	    if (send)
	      send_markup(sptr, &me, "DUMP", buffer);
            if (IsDead(sptr))
              {
                if (file)
                  {
                    fbputs("***** ERROR", fbd);
                    fbputs("Client invoking dump has died", fbd);
                    fbclose(fbd);
                  }
                return CLIENT_EXITED;
              }
	  }
        if (file)
          fbputs("*****", fbd);
        if (send)
          send_markup(sptr, &me, "DUMP", "*** --- End of category");
      }

  if (file)
    {
      fbputs("*****", fbd);
      fbputs("End of dump", fbd);
      fbputs("*****", fbd);
      fbclose(fbd);
    }

  if (send)
    send_markup(sptr, &me, "DUMP", "*** End of dump");

  return 0;
}
