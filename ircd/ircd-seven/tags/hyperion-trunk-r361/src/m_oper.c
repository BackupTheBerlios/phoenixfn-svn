/************************************************************************
 *   IRC - Internet Relay Chat, src/m_oper.c
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

#include "m_commands.h"
#include "client.h"
#include "fdlist.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_user.h"
#include "send.h"
#include "struct.h"
#include "umodes.h"
#include "md5crypt.h"
#include "channel.h"
#include "paths.h"
#include "s_stats.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

/*
 * m_oper
 *      parv[0] = sender prefix
 *      parv[1] = oper name
 *      parv[2] = oper password
 */
int m_oper(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem *aconf;
  char  *name, *password, *encr;
  static char buf[BUFSIZE];

  if (IsServer(sptr) && parc > 2)
    {
      /* OK, here's a kludge for you */
      struct Client *acptr = find_client(parv[1], NULL);
      char *c = parv[2];
      int what = MODE_ADD, mode = 0;
      user_modes old_allowed_umodes;

      if (!acptr)
        {
	  /* Ghost. Kill it. */
	  sendto_ops_flag(UMODE_SERVNOTICE, "Ghosted: %s from %s (OPER)",
			  parv[1], cptr->name);
	  sendto_one(cptr, ":%s KILL %s :%s (%s(unknown) <- %s)",
		     me.name, parv[1], me.name, parv[1], cptr->name);
	  return 0;
	}
      if (cptr != acptr->from)
	{
	  /* Wrong direction. Ignore it to prevent it from being applied
	   * to the wrong user. This code would never have been necessary
	   * if the command had been :<user> OPER in the first place.
	   * -- jilles */
	  ServerStats->is_wrdi++;
	  sendto_ops_flag(UMODE_DEBUG, "%s sent OPER for %s, but %s is at %s",
			  cptr->name, acptr->name, acptr->name, acptr->from->name);
	  return 0;
	}

      CopyUmodes(old_allowed_umodes, acptr->allowed_umodes);

      while(*c)
	{
	  switch(*c)
	    {
	    case '+':
	      what = MODE_ADD;
	      break;
	    case '-':
	      what = MODE_DEL;
	      break;
	    default:
	      if ((mode = user_modes_from_c_to_bitmask[(unsigned char)*c]))
		{
		  if (what == MODE_ADD)
		    SetBit(acptr->allowed_umodes, mode);
		  else
		    ClearBit(acptr->allowed_umodes, mode);
		}
	      break;
	    }
	  c++;
	}

      c = umode_difference(&old_allowed_umodes, &acptr->allowed_umodes);
      if (*c)
	sendto_serv_butone(cptr, ":%s OPER %s %s", sptr->name, acptr->name, c);

      return 0;
    }

  if (!IsClient(sptr))
    return 0;

  password = parc > 2 ? parv[2] : (parc > 1 ? parv[1] : (char *)NULL);
  name = parc > 2 ? parv[1] : (parc > 1 ? sptr->name : (char *)NULL);

  if (!IsServer(cptr) && (EmptyString(name) || EmptyString(password)))
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "OPER");
      return 0;
    }
        
  /* if message arrived from server, trust it, and set to oper */
  /* uhh.... not unless somebody can give me a damn good reason why OPER messages
   * should ever come from a server
   *  -- asuffield
   */
  if ((IsServer(cptr) || IsMe(cptr)))
    {
      sendto_ops_flag(UMODE_DEBUG, "WTF: %s received OPER message for %s from server %s", 
			     me.name, sptr->name, cptr->name);
      /* This doesn't work properly; it doesn't add them to the oper list locally.
	SetBit(sptr->umodes,UMODE_OPER);
	Count.oper++;
	sendto_serv_butone(cptr, ":%s MODE %s :+o", parv[0], parv[0]);
	if (IsMe(cptr))
	sendto_one(sptr, form_str(RPL_YOUREOPER),
	me.name, parv[0]);
      */
      return 0;
    }
  if (HasUmode(sptr,UMODE_OPER))
    {
      if (MyConnect(sptr))
        {
          sendto_one(sptr, form_str(RPL_YOUREOPER),
                     me.name, parv[0]);
          SendMessageFile(sptr, &ConfigFileEntry.opermotd);
        }
      return 0;
    }
  if (!(aconf = find_conf_exact(name, sptr->username, sptr->host,
                                CONF_OPERATOR)) &&
      !(aconf = find_conf_exact(name, sptr->username,
                                inetntoa((char *)&cptr->ip), CONF_OPERATOR)))
    {
      sendto_one(sptr, form_str(ERR_NOOPERHOST), me.name, parv[0]);
#if defined(FAILED_OPER_NOTICE) && defined(SHOW_FAILED_OPER_ID)
#ifdef SHOW_FAILED_OPER_PASSWD
      sendto_ops_flag(UMODE_SERVNOTICE, "Failed OPER attempt [%s(%s)] - identity mismatch: %s (%s@%s)",
			     name, password, sptr->name, sptr->username, sptr->host);
#else
      sendto_ops_flag(UMODE_SERVNOTICE, "Failed OPER attempt - host mismatch by %s (%s@%s)",
			     parv[0], sptr->username, sptr->host);
#endif /* SHOW_FAILED_OPER_PASSWD */
#endif /* FAILED_OPER_NOTICE && SHOW_FAILED_OPER_ID */
      return 0;
    }
#ifdef CRYPT_OPER_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL pointer. Head it off at the pass... */
  if (password && *aconf->passwd)
    encr = libshadow_md5_crypt(password, aconf->passwd);
  else
    {
      static char blank[] = "";
      encr = blank;
    }
#else
  encr = password;
#endif  /* CRYPT_OPER_PASSWORD */

  if ((aconf->status & CONF_OPERATOR) &&
      0 == strcmp(encr, aconf->passwd) && !attach_conf(sptr, aconf))
    {
      user_modes old, old_allowed;
      int identified = HasUmode(sptr, UMODE_IDENTIFIED);
      char *mode_string;

      CopyUmodes(old, sptr->umodes);
      CopyUmodes(old_allowed, sptr->allowed_umodes);

      OrUmodes(sptr->allowed_umodes, aconf->allowed_umodes, user_umodes);
      OrUmodes(sptr->umodes, sptr->umodes, aconf->default_umodes);
      AndUmodes(sptr->umodes, sptr->umodes, sptr->allowed_umodes);
      /* Preserve +e state across OPER */
      if (identified)
	SetUmode(sptr, UMODE_IDENTIFIED);
      ClearBit(sptr->allowed_umodes, UMODE_IDENTIFIED);

      SetUmode(sptr,UMODE_OPER);
      SetBit(sptr->allowed_umodes,UMODE_OPER);
      Count.oper++;

      if (IsAlwaysBusy(sptr))
	fdlist_add(sptr->fd, FDL_OPER | FDL_BUSY);
      else
	fdlist_add(sptr->fd, FDL_OPER);
      sendto_ops_flag(UMODE_SEESOPERS, "%s (%s@%s) just activated an O:line, hmmm", parv[0],
		      sptr->username, sptr->host);

      SendMessageFile(sptr, &ConfigFileEntry.opermotd);

      mode_string = umode_difference(&old_allowed, &sptr->allowed_umodes);
      if (*mode_string)
	sendto_serv_butone(cptr, ":%s OPER %s %s", me.name, sptr->name, mode_string);

      send_umode_out(cptr, sptr, sptr, &old);
      sendto_one(sptr, form_str(RPL_YOUREOPER), me.name, parv[0]);
      {
	user_modes privs;
	AndNotUmodes(privs, sptr->allowed_umodes, user_umodes);
	ClearBit(privs, UMODE_OPER);
	mode_string = umodes_as_string(&privs);
	if (*mode_string)
	  sendto_one(sptr, ":%s NOTICE %s :*** Capabilities are: %s", me.name, parv[0],
		     mode_string);
      }

      logprintf(L_TRACE, "OPER %s by %s!%s@%s",
	  name, parv[0], sptr->username, sptr->host);
      {
	int     logfile;
	
	/*
	 * This conditional makes the logfile active only after
	 * it's been created - thus logging can be turned off by
	 * removing the file.
	 *
	 */

	if (IsPerson(sptr) &&
	    (logfile = open(oper_log_file, O_WRONLY|O_APPEND)) != -1)
	  {
	    ircsnprintf(buf, BUFSIZE, "%s OPER (%s) by (%s!%s@%s)\n",
			myctime(CurrentTime), name, 
			parv[0], sptr->username,
			sptr->host);
	    write(logfile, buf, strlen(buf));
	    close(logfile);
	  }
      }
    }
  else
    {
      detach_conf(sptr, aconf);
      sendto_one(sptr,form_str(ERR_PASSWDMISMATCH),me.name, parv[0]);
#ifdef FAILED_OPER_NOTICE
#ifdef SHOW_FAILED_OPER_PASSWD
      sendto_ops_flag(UMODE_SERVNOTICE, "Failed OPER attempt [%s(%s)] - md5 passwd mismatch: %s [%s@%s]",
			     name, password, sptr->name, sptr->username, sptr->host);
#else
      sendto_ops_flag(UMODE_SERVNOTICE, "Failed OPER attempt by %s (%s@%s)",
			     parv[0], sptr->username, sptr->host);
#endif /* SHOW_FAILED_OPER_PASSWD */
#endif
    }
  return 0;
}
