/*
 * HybServ TS Services, Copyright (C) 1998-1999 Patrick Alken
 * This program comes with absolutely NO WARRANTY
 *
 * Should you choose to use and/or modify this source code, please
 * do so under the terms of the GNU General Public License under which
 * this program is distributed.
 *
 * $Id: chanserv.c,v 1.4 2001/11/12 09:50:55 asuffield Exp $
 */

#include "defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#endif

#include "alloc.h"
#include "channel.h"
#include "chanserv.h"
#include "client.h"
#include "conf.h"
#include "config.h"
#include "dcc.h"
#include "err.h"
#include "hash.h"
#include "helpserv.h"
#include "hybdefs.h"
#include "log.h"
#include "match.h"
#include "memoserv.h"
#include "misc.h"
#include "mystring.h"
#include "nickserv.h"
#include "operserv.h"
#include "server.h"
#include "settings.h"
#include "sock.h"
#include "timestr.h"
#include "sprintf_irc.h"

#if defined(NICKSERVICES) && defined(CHANNELSERVICES)

/* hash table of registered channels */
struct ChanInfo *chanlist[CHANLIST_MAX];

static void cs_joinchan(struct ChanInfo *);
static void cs_CheckChan(struct ChanInfo *, struct Channel *);
static void cs_SetTopic(struct Channel *, char *);
static int ChangeChanPass(struct ChanInfo *, char *);
static void AddChan(struct ChanInfo *);
static struct ChanInfo *MakeChan();
static void AddContact(struct Luser *, struct ChanInfo *);
static int AddAccess(struct ChanInfo *, struct Luser *, char *,
                     struct NickInfo *, int, time_t, time_t);
static int DelAccess(struct ChanInfo *, struct Luser *, char *,
                     struct NickInfo *);
static int AddAkick(struct ChanInfo *, struct Luser *, char *, char *);
static int DelAkick(struct ChanInfo *, char *);
static int IsContact(struct Luser *, struct ChanInfo *);
static int GetAccess(struct ChanInfo *, struct Luser *);
static struct ChanAccess *OnAccessList(struct ChanInfo *, char *,
                                       struct NickInfo *);
static struct AutoKick *OnAkickList(struct ChanInfo *, char *);

/* default access levels for new channels */
static int DefaultAccess[] = {
  -1,          /* CA_AUTODEOP */
  51,          /* CA_AUTOVOICE */
  8,           /* CA_CMDVOICE */
  30,          /* CA_ACCESS */
  5,           /* CA_CMDINVITE */
#ifdef HYBRID7
  /* Default access for halfop and cmdhalfop -Janos */
  8,           /* CA_AUTOHALFOP */
  10,           /* CA_CMDHALFOP */
#endif /* HYBRID7 */
  51,          /* CA_AUTOOP */
  10,          /* CA_CMDOP */
#ifndef HYBRID7
  10,          /* CA_CMDUNBAN */
#else
  8,           /* CA_CMDUNBAN */
#endif /* HYBRID7 */
  15,          /* CA_AKICK */
  20,          /* CA_CMDCLEAR */
  25,          /* CA_SET */
  40,          /* CA_SUPEROP */
  50,          /* CA_CONTACT */
  10,          /* CA_TOPIC */
  50,          /* CA_LEVEL */
};

static void c_help(struct Luser *, struct NickInfo *, int, char **);
static void c_register(struct Luser *, struct NickInfo *, int, char **);
static void c_drop(struct Luser *, struct NickInfo *, int, char **);
static void c_identify(struct Luser *, struct NickInfo *, int, char **);

static void c_access(struct Luser *, struct NickInfo *, int, char **);
static void c_access_add(struct Luser *, struct NickInfo *, int, char **);
static void c_access_del(struct Luser *, struct NickInfo *, int, char **);
static void c_access_list(struct Luser *, struct NickInfo *, int, char **);

static void c_akick(struct Luser *, struct NickInfo *, int, char **);
static void c_akick_add(struct Luser *, struct NickInfo *, int, char **);
static void c_akick_del(struct Luser *, struct NickInfo *, int, char **);
static void c_akick_list(struct Luser *, struct NickInfo *, int, char **);

static void c_list(struct Luser *, struct NickInfo *, int, char **);
static void c_level(struct Luser *, struct NickInfo *, int, char **);

static void c_set(struct Luser *, struct NickInfo *, int, char **);
static void c_set_topiclock(struct Luser *, struct NickInfo *, int, char **);
static void c_set_private(struct Luser *, struct NickInfo *, int, char **);
static void c_set_verbose(struct Luser *, struct NickInfo *, int, char **);     
static void c_set_secure(struct Luser *, struct NickInfo *, int, char **);
static void c_set_secureops(struct Luser *, struct NickInfo *, int, char **);
static void c_set_splitops(struct Luser *, struct NickInfo *, int, char **);
/* static void c_set_restricted(struct Luser *, struct NickInfo *, int, char **); */
static void c_set_forget(struct Luser *, struct NickInfo *, int, char **);
static void c_set_guard(struct Luser *, struct NickInfo *, int, char **);
static void c_set_password(struct Luser *, struct NickInfo *, int, char **);
static void c_set_contact(struct Luser *, struct NickInfo *, int, char **);
static void c_set_alternate(struct Luser *, struct NickInfo *, int, char **);
static void c_set_mlock(struct Luser *, struct NickInfo *, int, char **);
static void c_set_topic(struct Luser *, struct NickInfo *, int, char **);
static void c_set_entrymsg(struct Luser *, struct NickInfo *, int, char **);
static void c_set_email(struct Luser *, struct NickInfo *, int, char **);
static void c_set_url(struct Luser *, struct NickInfo *, int, char **);

static void c_getkey(struct Luser *, struct NickInfo *, int, char **);
static void c_invite(struct Luser *, struct NickInfo *, int, char **);
static void c_op(struct Luser *, struct NickInfo *, int, char **);
#ifdef HYBRID7
static void c_hop(struct Luser *, struct NickInfo *, int, char **);
#endif /* HYBRID7 */
static void c_voice(struct Luser *, struct NickInfo *, int, char **);
static void c_unban(struct Luser *, struct NickInfo *, int, char **);
static void c_info(struct Luser *, struct NickInfo *, int, char **);

static void c_clear(struct Luser *, struct NickInfo *, int, char **);
static void c_clear_ops(struct Luser *, struct NickInfo *, int, char **);
#ifdef HYBRID7
static void c_clear_hops(struct Luser *, struct NickInfo *, int, char **);
#endif /* HYBRID7 */
static void c_clear_voices(struct Luser *, struct NickInfo *, int, char **);
static void c_clear_modes(struct Luser *, struct NickInfo *, int, char **);
#ifdef GECOSBANS
static void c_clear_gecos_bans(struct Luser *, struct NickInfo *, int, char **);
#endif /* GECOSBANS */
static void c_clear_bans(struct Luser *, struct NickInfo *, int, char **);
static void c_clear_users(struct Luser *, struct NickInfo *, int, char **);
static void c_clear_all(struct Luser *, struct NickInfo *, int, char **);

#ifdef EMPOWERADMINS
static void c_forbid(struct Luser *, struct NickInfo *, int, char **);
static void c_unforbid(struct Luser *, struct NickInfo *, int, char **);
static void c_setpass(struct Luser *, struct NickInfo *, int, char **);
static void c_status(struct Luser *, struct NickInfo *, int, char **);
static void c_forget(struct Luser *, struct NickInfo *, int, char **);
static void c_noexpire(struct Luser *, struct NickInfo *, int, char **);
static void c_clearnoexpire(struct Luser *, struct NickInfo *, int, char **);   
/* static void c_fixts(struct Luser *, struct NickInfo *, int, char **); */
/* static void c_resetlevels(struct Luser *, struct NickInfo *, int, char **); */
#endif /* EMPOWERADMINS */

/* main ChanServ commands */
static struct Command chancmds[] = {
  { "HELP", c_help, LVL_NONE },
  { "REGISTER", c_register, LVL_IDENT },
  { "DROP", c_drop, LVL_IDENT },
  { "IDENTIFY", c_identify, LVL_IDENT },
  { "ACCESS", c_access, LVL_NONE },
  { "AKICK", c_akick, LVL_IDENT },
  { "AUTOREM", c_akick, LVL_IDENT },
  { "LIST", c_list, LVL_NONE },
  { "LEVEL", c_level, LVL_IDENT },
  { "SET", c_set, LVL_IDENT },
  { "GETKEY", c_getkey, LVL_IDENT },
  { "INVITE", c_invite, LVL_IDENT },
  { "OP", c_op, LVL_IDENT },
#ifdef HYBRID7
  { "HALFOP", c_hop, LVL_IDENT },
#endif /* HYBRID7 */
  { "VOICE", c_voice, LVL_IDENT },
  { "UNBAN", c_unban, LVL_IDENT },
  { "INFO", c_info, LVL_NONE },
  { "CLEAR", c_clear, LVL_IDENT },
/*   { "FIXTS" , c_fixts, LVL_ADMIN }, */
#ifdef EMPOWERADMINS
  { "FORBID", c_forbid, LVL_ADMIN },
  { "UNFORBID", c_unforbid, LVL_ADMIN },
  { "SETPASS", c_setpass, LVL_ADMIN },
  { "STATUS", c_status, LVL_ADMIN },
  { "NOEXPIRE", c_noexpire, LVL_ADMIN },
  { "CLEARNOEXP", c_clearnoexpire, LVL_ADMIN },
/*   { "RESETLEVELS", c_resetlevels, LVL_ADMIN }, */
#endif

  { 0, 0, 0 }
};

/* sub-commands for ChanServ ACCESS */
static struct Command accesscmds[] = {
  { "ADD", c_access_add, LVL_IDENT },
  { "DEL", c_access_del, LVL_IDENT },
  { "LIST", c_access_list, LVL_NONE },

  { 0, 0, 0 }
};

/* sub-commands for ChanServ AKICK / AUTOREM */
static struct Command akickcmds[] = {
  { "ADD", c_akick_add, LVL_NONE },
  { "DEL", c_akick_del, LVL_NONE },
  { "LIST", c_akick_list, LVL_NONE },

  { 0, 0, 0 }
};

/* sub-commands for ChanServ SET */
static struct Command setcmds[] = {
  { "TOPICLOCK", c_set_topiclock, LVL_NONE },
  { "TLOCK", c_set_topiclock, LVL_NONE },
  { "PRIVATE", c_set_private, LVL_NONE },
  { "VERBOSE", c_set_verbose, LVL_NONE },
  { "SECURE", c_set_secure, LVL_NONE },
  { "SECUREOPS", c_set_secureops, LVL_NONE },
  { "RESTRICTOPS", c_set_secureops, LVL_NONE },
  { "SPLITOPS", c_set_splitops, LVL_NONE },
/*   { "RESTRICTED", c_set_restricted, LVL_NONE }, */
  { "GUARD", c_set_guard, LVL_NONE },
  { "CHANGUARD", c_set_guard, LVL_NONE },
  { "AUTOJOIN", c_set_guard, LVL_NONE },
  { "PASSWORD", c_set_password, LVL_NONE },
  { "NEWPASS", c_set_password, LVL_NONE },
  { "CONTACT", c_set_contact, LVL_NONE },
  { "ALTERNATE", c_set_alternate, LVL_NONE },
  { "SUCCESSOR", c_set_alternate, LVL_NONE },
  { "MLOCK", c_set_mlock, LVL_NONE },
  { "MODELOCK", c_set_mlock, LVL_NONE },
/*   { "TOPIC", c_set_topic, LVL_NONE }, */
  { "ENTRYMSG", c_set_entrymsg, LVL_NONE },
  { "ONJOIN", c_set_entrymsg, LVL_NONE },
  { "EMAIL", c_set_email, LVL_NONE },
  { "MAIL", c_set_email, LVL_NONE },
  { "URI", c_set_url, LVL_NONE },
  { "URL", c_set_url, LVL_NONE },
  { "WEBSITE", c_set_url, LVL_NONE },
  { 0, 0, 0 }
};

/* sub-commands for ChanServ CLEAR */
static struct Command clearcmds[] = {
  { "OPS", c_clear_ops, LVL_NONE },
#ifdef HYBRID7
  /* Allow clear halfops for hybrid7, too -Janos */
  { "HALFOPS", c_clear_hops, LVL_NONE },
#endif /* HYBRID7 */
  { "VOICES", c_clear_voices, LVL_NONE },
  { "MODES", c_clear_modes, LVL_NONE },
  { "BANS", c_clear_bans, LVL_NONE },
#ifdef GECOSBANS
  { "GECOSBANS", c_clear_gecos_bans, LVL_NONE },
#endif /* GECOSBANS */
  { "USERS", c_clear_users, LVL_NONE },
  { "ALL", c_clear_all, LVL_NONE },

  { 0, 0, 0 }
};

typedef struct
{
  int level;
  char *cmd;
  char *desc;
} AccessInfo;

static AccessInfo accessinfo[] = {
  { CA_AUTODEOP, "AUTODEOP", "Automatic deop/devoice" },
  { CA_AUTOVOICE, "AUTOVOICE", "Automatic voice" },
  { CA_CMDVOICE, "CMDVOICE", "Use of command VOICE" },
  { CA_ACCESS, "ACCESS", "Allow ACCESS modification" },
  { CA_CMDINVITE, "CMDINVITE", "Use of command INVITE" },
#ifdef HYBRID7
  /* Halfop help indices -Janos */
  { CA_CMDHALFOP, "CMDHALFOP", "Use of command HALFOP"},
  { CA_AUTOHALFOP, "AUTOHALFOP", "Automatic halfop"},
#endif /* HYBRID7 */
  { CA_AUTOOP, "AUTOOP", "Automatic op" },
  { CA_CMDOP, "CMDOP", "Use of command OP" },
  { CA_CMDUNBAN, "CMDUNBAN", "Use of command UNBAN" },
  { CA_AKICK, "AUTOKICK", "Allow AKICK modification" },
  { CA_CMDCLEAR, "CMDCLEAR", "Use of command CLEAR" },
  { CA_SET, "SET", "Modify channel SETs" },
  { CA_SUPEROP, NULL, "All of the above" },
  { CA_CONTACT, NULL, "Full access to the channel" },
  { CA_TOPIC, "TOPIC", "Change the channel topic" },
  { CA_LEVEL, "LEVEL", "Use of command LEVEL" },
  { 0, 0, 0 }
};

/*
cs_process()
  Process command coming from 'nick' directed towards n_ChanServ
*/

void
cs_process(char *nick, char *command)

{
  int acnt;
  char **arv;
  struct Command *cptr;
  struct Luser *lptr;
  struct NickInfo *nptr;
  struct ChanInfo *chptr;

  if (!command || !(lptr = FindClient(nick)))
    return;

  if (Network->flags & NET_OFF)
  {
    notice(n_ChanServ, lptr->nick,
      "Services are currently \002disabled\002");
    return;
  }

  acnt = SplitBuf(command, &arv);
  if (acnt == 0)
  {
    MyFree(arv);
    return;
  }

  cptr = GetCommand(chancmds, arv[0]);

  if (!cptr || (cptr == (struct Command *) -1))
  {
    notice(n_ChanServ, lptr->nick,
      "%s command [%s]",
      (cptr == (struct Command *) -1) ? "Ambiguous" : "Unknown",
      arv[0]);
    MyFree(arv);
    return;
  }

  if ((!(nptr = FindNick(lptr->nick))) &&
      (cptr->level != LVL_NONE))
  {
    /* the command requires a registered nickname */

    notice(n_ChanServ, lptr->nick,
      "Your nickname is not registered");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_NickServ,
      "REGISTER");
    MyFree(arv);
    return;
  }

  /*
   * Check if the command is for admins only - if so,
   * check if they match an admin O: line.  If they do,
   * check if they are EITHER oper'd, or registered with OperServ,
   * if either of these is true, allow the command
   */
  if ((cptr->level == LVL_ADMIN) && !(IsValidAdmin(lptr)))
  {
    notice(n_ChanServ, lptr->nick, "Unknown command [%s]",
      arv[0]);
    MyFree(arv);
    return;
  }

  if (nptr)
  {
    if (nptr->flags & NS_FORBID)
    {
      notice(n_ChanServ, lptr->nick,
        "Cannot execute commands for forbidden nicknames");
      MyFree(arv);
      return;
    }

    if (cptr->level != LVL_NONE)
    {
      if (!(nptr->flags & NS_IDENTIFIED))
      {
        notice(n_ChanServ, lptr->nick,
          "Password identification is required for [\002%s\002]",
          cptr->cmd);
        notice(n_ChanServ, lptr->nick, 
          "Type \002/msg %s IDENTIFY <password>\002 and retry",
          n_NickServ);
        MyFree(arv);
        return;
      }
    }
  } /* if (nptr) */

  if ((chptr = FindChan(acnt >= 2 ? arv[1] : NULL)))
  {
    /* Complain only if it not admin-level command -kre */
    if (!IsValidAdmin(lptr))
    {
      if (chptr->flags & CS_FORBID)
      {
        notice(n_ChanServ, lptr->nick,
          "Cannot execute commands for forbidden channels");
        MyFree(arv);
        return;
      }
    }
  }

  /* call cptr->func to execute command */
  (*cptr->func)(lptr, nptr, acnt, arv);

  MyFree(arv);

  return;
} /* cs_process() */

/*
cs_loaddata()
  Load ChanServ database - return 1 if successful, -1 if not, and -2
if the errors are unrecoverable
*/

int
cs_loaddata()

{
  FILE *fp;
  char line[MAXLINE], **av;
  char *keyword;
  int ac, ret = 1, cnt;
  struct ChanInfo *cptr = NULL;

  if (!(fp = fopen(ChanServDB, "r")))
  {
    /* ChanServ data file doesn't exist */
    return -1;
  }

  cnt = 0;
  /* load data into list */
  while (fgets(line, MAXLINE - 1, fp))
  {
    ++cnt;

    ac = SplitBuf(line, &av);
    if (!ac)
    {
      /* probably a blank line */
      MyFree(av);
      continue;
    }

    if (av[0][0] == ';')
    {
      MyFree(av);
      continue;
    }

    if (!ircncmp("->", av[0], 2))
    {
      /* 
       * check if there are enough args
       */
      if (ac < 2)
      {
        fatal(1, "%s:%d Invalid database format (FATAL)",
          ChanServDB,
          cnt);
        ret = -2;
        MyFree(av);
        continue;
      }

      /* check if there is no channel associated with data */
      if (!cptr)
      {
        fatal(1, "%s:%d No channel associated with data",
          ChanServDB,
          cnt);
        if (ret > 0)
          ret = -1;
        MyFree(av);
        continue;
      }

      keyword = av[0] + 2;
      if (!ircncmp("PASS", keyword, 4))
      {
        if (!cptr->password)
          cptr->password = MyStrdup(av[1]);
        else
        {
          fatal(1,
            "%s:%d ChanServ entry for [%s] has multiple PASS lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("FNDR", keyword, 4))
      {
        if (!cptr->contact)
        {
          struct NickInfo *ni = FindNick(av[1]);
          struct NickInfo *master;

          if (!ni)
          {
            fatal(1, "%s:%d Contact nickname [%s] for channel [%s] is not registered (FATAL)",
              ChanServDB,
              cnt,
              av[1],
              cptr->name);
            ret = -2;
          }
          else if ((master = GetMaster(ni)))
          {
            cptr->contact = MyStrdup(master->nick);
            AddContactChannelToNick(&master, cptr);
          }
          else
          {
            fatal(1, "%s:%d Unable to find master nickname for contact [%s] on channel [%s] (FATAL)",
              ChanServDB,
              cnt,
              ni->nick,
              cptr->name);
            ret = -2;
          }
	  if (ac > 2)
	    cptr->last_contact_active = atol(av[2]);
        }
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple FNDR (contact) lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp(keyword, "SUCCESSOR", 9))
      {
        if (!cptr->alternate)
        {
          struct NickInfo  *ni = FindNick(av[1]);

          if (!ni)
          {
            fatal(1, "%s:%d Alternate contact nickname [%s] for channel [%s] is not registered (ignoring)",
              ChanServDB,
              cnt,
              av[1],
              cptr->name);
            if (ret > 0)
              ret = -1;
          }
          else
            cptr->alternate = MyStrdup(ni->nick);
        }
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple SUCCESSOR (alternate contact) lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
	if (ac > 2)
	  cptr->last_alternate_active = atol(av[2]);
      }
      else if (!ircncmp("ACCESS", keyword, 6))
      {
        if (ac < 3)
        {
          fatal(1, "%s:%d Invalid database format (FATAL)",
            ChanServDB,
            cnt);
          ret = -2;
        }
        else
        {
          struct NickInfo *nptr;
	  time_t created = 0, last_used = 0;
	  if (ac > 4)
	    {
	      created = atol(av[3]);
	      last_used = atol(av[4]);
	    }

          if ((nptr = FindNick(av[1])))
            AddAccess(cptr, NULL, NULL, nptr, atol(av[2]), created, last_used);
          else
            AddAccess(cptr, NULL, av[1], NULL, atol(av[2]), created, last_used);
        }
      }
      else if (!ircncmp("AKICK", keyword, 5))
      {
        if (ac < 3)
        {
          fatal(1, "%s:%d Invalid database format (FATAL)",
            ChanServDB,
            cnt);
          ret = -2;
        }
        else
          AddAkick(cptr, (struct Luser *) NULL, av[1], av[2] + 1);
      }
      else if (!ircncmp("ALVL", keyword, 4))
      {
	if (!cptr->access_lvl)
        {
          int ii;

          cptr->access_lvl = (int *) MyMalloc(sizeof(int) * CA_SIZE);
          for (ii = 0; ii < CA_SIZE; ii++)
	    {
	      if ((ii + 1) < ac)
		cptr->access_lvl[ii] = atoi(av[ii + 1]);
	      else
		cptr->access_lvl[ii] = DefaultAccess[ii];
	    }
        }
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple ALVL lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("TOPIC", keyword, 5))
      {
        if (!cptr->topic)
          cptr->topic = MyStrdup(av[1] + 1);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple TOPIC lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("LIMIT", keyword, 5))
      {
        if (!cptr->limit)
          cptr->limit = atoi(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple LIMIT lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("KEY", keyword, 3))
      {
        if (!cptr->key)
          cptr->key = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple KEY lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!strncasecmp("FORWARD", keyword, 3))
      {
        if (!cptr->forward)
          cptr->forward = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple FORWARD lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!strncasecmp("THROTTLE", keyword, 3))
      {
        if (!cptr->throttle)
          cptr->throttle = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple THROTTLE lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!strncasecmp("DLINE", keyword, 3))
      {
        if (!cptr->dline)
          cptr->dline = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple DLINE lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("MON", keyword, 3))
      {
        if (!cptr->modes_on)
          cptr->modes_on = atoi(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple MON lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("MOFF", keyword, 5))
      {
        if (!cptr->modes_off)
          cptr->modes_off = atoi(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple MOFF lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("ENTRYMSG", keyword, 8))
      {
        if (!cptr->entrymsg)
          cptr->entrymsg = MyStrdup(av[1] + 1);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple ENTRYMSG lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("EMAIL", keyword, 8))
      {
        if (!cptr->email)
          cptr->email = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple EMAIL lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
      else if (!ircncmp("URL", keyword, 8))
      {
        if (!cptr->url)
          cptr->url = MyStrdup(av[1]);
        else
        {
          fatal(1, "%s:%d ChanServ entry for [%s] has multiple URL lines (using first)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }
      }
    }
    else
    {
      if (cptr)
      {
	if (cptr->flags & CS_FORGET)
	{
          fatal(1,
            "%s:%d Forgotten channel [%s] (deleting)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
	  MyFree(cptr);
	  cptr = NULL;
	  continue;
	}
        if (!cptr->access_lvl && !(cptr->flags & CS_FORBID))
        {
          SetDefaultALVL(cptr);

          fatal(1,
            "%s:%d No access level list for registered channel [%s] (using default)",
            ChanServDB,
            cnt,
            cptr->name);
          if (ret > 0)
            ret = -1;
        }

        if (!cptr->password && !(cptr->flags & CS_FORBID))
        {
          /* the previous channel didn't have a PASS line */
          fatal(1, "%s:%d No contact password entry for registered channel [%s] (FATAL)",
            ChanServDB,
            cnt,
            cptr->name);
          ret = -2;
        }

        if (!cptr->contact && !(cptr->flags & CS_FORBID))
        {
          /* the previous channel didn't have a FNDR line */
          fatal(1, "%s:%d No contact nickname entry for registered channel [%s] (FATAL)",
            ChanServDB,
            cnt,
            cptr->name);
          ret = -2;
        }

        /*
         * we've come to a new channel entry, so add the last chan
         * to the list before proceeding
         */
        AddChan(cptr);
      } /* if (cptr) */

      /* 
       * make sure there are enough args on the line
       * <channel> <flags> <time created> <last used>
       */
      if (ac < 4)
      {
        fatal(1, "%s:%d Invalid database format (FATAL)",
          ChanServDB,
          cnt);
        ret = -2;
        cptr = NULL;
        MyFree(av);
        continue;
      }

      if (av[0][0] != '#')
      {
        fatal(1, "%s:%d Invalid channel name [%s] (skipping)",
          ChanServDB,
          cnt,
          av[0]);
        if (ret > 0)
          ret = -1;
        cptr = NULL;
        MyFree(av);
        continue;
      }

      cptr = MakeChan();
      cptr->name = MyStrdup(av[0]);
      cptr->flags = atoi(av[1]) & ~CS_RESTRICTED;
      cptr->created = atoi(av[2]);
      cptr->lastused = atoi(av[3]);
    }

    MyFree(av);
  } /* while */

  if (cptr)
  {
    if (!cptr->access_lvl && !(cptr->flags & CS_FORBID))
    {
      SetDefaultALVL(cptr);

      fatal(1,
        "%s:%d No access level list for registered channel [%s] (using default)",
        ChanServDB,
        cnt,
        cptr->name);
      if (ret > 0)
        ret = -1;
    }

    if (cptr->flags & CS_FORGET)
    {
      fatal(1,
        "%s:%d Forgotten channel [%s] (deleting)",
        ChanServDB,
        cnt,
        cptr->name);
      if (ret > 0)
        ret = -1;
      MyFree(cptr);
      cptr = NULL;
    }
    else if ((cptr->password && cptr->contact) ||
        (cptr->flags & CS_FORBID))
    {
      /* 
       * cptr has a contact and password, so it can be added to
       * the table
       */
      AddChan(cptr);
    }
    else
    {
      if (!cptr->password && !(cptr->flags & CS_FORBID))
      {
        fatal(1, "%s:%d No contact password entry for registered channel [%s] (FATAL)",
          ChanServDB,
          cnt,
          cptr->name);
        ret = -2;
      }

      if (!cptr->contact && !(cptr->flags & CS_FORBID))
      {
        fatal(1, "%s:%d No contact nickname entry for registered channel [%s] (FATAL)",
          ChanServDB,
          cnt,
          cptr->name);
        ret = -2;
      }
    }
  } /* if (cptr) */

  fclose(fp);

  return (ret);
} /* cs_loaddata */

/*
cs_join()
  Have ChanServ join a channel
*/

void
cs_join(struct ChanInfo *chanptr)

{
  int goodjoin;
  char sendstr[MAXLINE], **av;
  struct Channel *chptr;
  time_t chants;

  if (!chanptr)
    return;

  if (cs_ShouldBeOnChan(chanptr))
    goodjoin = 1;
  else
    goodjoin = 0;

  chptr = FindChannel(chanptr->name);

  if (goodjoin)
  {
    /*
     * chptr might be null, so make sure we check before
     * using it
     */
    if (chptr)
      chants = chptr->since;
    else
      chants = current_ts;

    ircsprintf(sendstr, ":%s SJOIN %ld %s + :@%s\n", Me.name,
      (long) chants, chanptr->name, n_ChanServ);
    toserv(sendstr);

    SplitBuf(sendstr, &av);

    /*
     * The previous call to FindChannel() may have
     * been null, so reassign chptr to the channel
     * structure if the channel was just created.
     * This way, we will always pass a valid pointer
     * to cs_CheckChan()
     */
    chptr = AddChannel(av, 0, (char **) NULL);

    MyFree(av);
  }

  /* Check that the right ops are opped etc */
  cs_CheckChan(chanptr, chptr);
} /* cs_join() */

/*
cs_joinchan()
 Similar to cs_join, except don't call cs_CheckChan() on the
channel.
*/

static void
cs_joinchan(struct ChanInfo *chanptr)

{
  char sendstr[MAXLINE], **av;
  struct Channel *chptr;

  chptr = FindChannel(chanptr->name);

  ircsprintf(sendstr, ":%s SJOIN %ld %s + :@%s\n",
    Me.name, chptr ? (long) chptr->since : (long) current_ts,
    chanptr->name, n_ChanServ);
  toserv(sendstr);

  SplitBuf(sendstr, &av);
  AddChannel(av, 0, (char **) NULL);
  MyFree(av);
} /* cs_joinchan() */

/*
cs_join_ts_minus_1()
  Have ChanServ join a channel with TS - 1
*/

void
cs_join_ts_minus_1(struct ChanInfo *chanptr)

{
  char sendstr[MAXLINE], **av;
  struct Channel *cptr = FindChannel(chanptr->name);
  int ac;

  ircsprintf(sendstr,
    ":%s SJOIN %ld %s + :@%s\n",
    Me.name, cptr ? (long) (cptr->since - 1) : (long) current_ts,
    cptr->name, n_ChanServ);
  toserv(sendstr);

  ac = SplitBuf(sendstr, &av);
  s_sjoin(ac, av);

  MyFree(av);

  /* Check that the right ops are opped etc */
  cs_CheckChan(chanptr, cptr);
} /* cs_join_ts_minus_1() */

/*
cs_part()
  Have ChanServ part 'chptr'
*/

void
cs_part(struct Channel *chptr)

{
  if (!chptr)
    return;

  toserv(":%s PART %s\n",
    n_ChanServ,
    chptr->name);
  RemoveFromChannel(chptr, Me.csptr);
} /* cs_part() */

/*
cs_CheckChan()
  Called right after ChanServ joins a channel, to check that the right
modes get set, the right people are opped, etc
*/

static void
cs_CheckChan(struct ChanInfo *cptr, struct Channel *chptr)

{
  struct ChannelUser *tempu;
  char *dopnicks; /* nicks to deop */
  char modes[MAXLINE]; /* mlock modes to set */

  if (!cptr || !chptr)
    return;

  if (cptr->flags & CS_FORBID)
  {
    char  *knicks; /* nicks to kick */

    knicks = (char *)MyMalloc(sizeof(char));
    knicks[0] = '\0';
    for (tempu = chptr->firstuser; tempu; tempu = tempu->next)
    {
      if (FindService(tempu->lptr))
        continue;
      knicks = (char *) MyRealloc(knicks, strlen(knicks) + strlen(tempu->lptr->nick) + (2 * sizeof(char)));
      strcat(knicks, tempu->lptr->nick);
      strcat(knicks, " ");
    }
    SetModes(n_ChanServ, 0, 'o', chptr, knicks);

    /*
     * Have ChanServ join the channel to enforce the +i
     */
    if (!IsChannelMember(chptr, Me.csptr))
      cs_joinchan(cptr);

    toserv(":%s MODE %s +ij\n",
      n_ChanServ,
      cptr->name);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, "+ij");
    KickBan(0, n_ChanServ, chptr, knicks, "Forbidden Channel");
    MyFree(knicks);

    return;
  }

  if ((cptr->flags & CS_SECUREOPS) || (cptr->flags & CS_RESTRICTED))
  {
    /* SECUREOPS is set - deop all non autoops */
    dopnicks = (char *) MyMalloc(sizeof(char));
    dopnicks[0] = '\0';
    for (tempu = chptr->firstuser; tempu; tempu = tempu->next)
    {
      if (FindService(tempu->lptr))
        continue;
      if ((tempu->flags & CH_OPPED) && 
          !HasAccess(cptr, tempu->lptr, CA_AUTOOP))
      {
        dopnicks = (char *) MyRealloc(dopnicks, strlen(dopnicks)
            + strlen(tempu->lptr->nick) + (2 * sizeof(char)));
        strcat(dopnicks, tempu->lptr->nick);
        strcat(dopnicks, " ");
      }
    }
    SetModes(n_ChanServ, 0, 'o', chptr, dopnicks);
    MyFree(dopnicks);
  }

  if ((cptr->flags & CS_RESTRICTED))
  {
    char *kbnicks; /* nicks to kickban */

    /* channel is restricted - kickban all non-autoops */
    kbnicks = (char *) MyMalloc(sizeof(char));
    kbnicks[0] = '\0';
    for (tempu = chptr->firstuser; tempu; tempu = tempu->next)
    {
      if (FindService(tempu->lptr))
        continue;
      if (!HasAccess(cptr, tempu->lptr, CA_AUTOOP))
      {
        kbnicks = (char *) MyRealloc(kbnicks, strlen(kbnicks) + strlen(tempu->lptr->nick) + (2 * sizeof(char)));
        strcat(kbnicks, tempu->lptr->nick);
        strcat(kbnicks, " ");
      }
    }

    /*
     * Have ChanServ join the channel to enforce the bans
     */
    if (!IsChannelMember(chptr, Me.csptr))
      cs_joinchan(cptr);

    KickBan(1, n_ChanServ, chptr, kbnicks, "Restricted Channel");
    MyFree(kbnicks);
  }

  strcpy(modes, "+");
  if (cptr->modes_on || cptr->key || cptr->limit || cptr->forward || cptr->throttle || cptr->dline)
  {
    if ((cptr->modes_on & MODE_S) &&
        !(chptr->modes & MODE_S))
      strcat(modes, "s");
#ifdef HYBRID7
    /* Add parse for mode_a -Janos*/
    if ((cptr->modes_on & MODE_A) &&
        !(chptr->modes & MODE_A))
      strcat(modes, "a");
#endif /* HYBRID7 */
    if ((cptr->modes_on & MODE_P) &&
        !(chptr->modes & MODE_P))
      strcat(modes, "p");
    if ((cptr->modes_on & MODE_N) &&
        !(chptr->modes & MODE_N))
      strcat(modes, "n");
    if ((cptr->modes_on & MODE_T) &&
        !(chptr->modes & MODE_T))
      strcat(modes, "t");
    if ((cptr->modes_on & MODE_M) &&
        !(chptr->modes & MODE_M))
      strcat(modes, "m");
    if ((cptr->modes_on & MODE_C) &&
        !(chptr->modes & MODE_C))
      strcat(modes, "c");
    if ((cptr->modes_on & MODE_G) &&
        !(chptr->modes & MODE_G))
      strcat(modes, "g");
    if ((cptr->modes_on & MODE_I) &&
        !(chptr->modes & MODE_I))
      strcat(modes, "i");
    if ((cptr->modes_on & MODE_R) &&
        !(chptr->modes & MODE_R))
      strcat(modes, "r");
    if ((cptr->modes_on & MODE_Z) &&
        !(chptr->modes & MODE_Z))
      strcat(modes, "z");
#if 0
    /* XXX - doesn't exist */
    if ((cptr->modes_on & MODE_CAPF) &&
        !(chptr->modes & MODE_CAPF))
      strcat(modes, "F");
#endif
    if ((cptr->modes_on & MODE_CAPP) &&
        !(chptr->modes & MODE_CAPP))
      strcat(modes, "P");
    if ((cptr->modes_on & MODE_CAPL) &&
        !(chptr->modes & MODE_CAPL))
      strcat(modes, "L");
    if ((cptr->modes_on & MODE_CAPR) &&
        !(chptr->modes & MODE_CAPR))
      strcat(modes, "R");
    if (cptr->limit)
      strcat(modes, "l");
    if (cptr->key)
      strcat(modes, "k");
    if (cptr->forward)
      strcat(modes, "f");
    if (cptr->throttle)
      strcat(modes, "J");
    if (cptr->dline)
      strcat(modes, "D");

    if (cptr->limit)
    {
      char temp[MAXLINE];

      ircsprintf(temp, "%s %ld", modes, cptr->limit);
      strcpy(modes, temp);
    }
    if (cptr->key)
    {
      char temp[MAXLINE];

      if (chptr->key)
      {
        ircsprintf(temp, "-k %s", chptr->key);
        toserv(":%s MODE %s %s\n", n_ChanServ, chptr->name, temp);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, temp);
      }
      ircsprintf(temp, "%s %s", modes, cptr->key);
      strcpy(modes, temp);
    }
    if (cptr->forward)
    {
      char temp[MAXLINE];

      sprintf(temp, "%s %s", modes, cptr->forward);
      strcpy(modes, temp);
    }
    if (cptr->throttle)
    {
      char temp[MAXLINE];

      sprintf(temp, "%s %s", modes, cptr->throttle);
      strcpy(modes, temp);
    }
    if (cptr->dline)
    {
      char temp[MAXLINE];

      sprintf(temp, "%s %s", modes, cptr->dline);
      strcpy(modes, temp);
    }
  }
  if (modes[1])
  {
    toserv(":%s MODE %s %s\n",
      n_ChanServ,
      cptr->name,
      modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }

  strcpy(modes, "-");
  if (cptr->modes_off)
  {
    if ((cptr->modes_off & MODE_S) &&
        (chptr->modes & MODE_S))
      strcat(modes, "s");
#ifdef HYBRID7
    /* Add parse for mode_a -Janos*/
    if ((cptr->modes_off & MODE_A) &&
        (chptr->modes & MODE_A))
      strcat(modes, "a");
#endif /* HYBRID7 */
    if ((cptr->modes_off & MODE_P) &&
        (chptr->modes & MODE_P))
      strcat(modes, "p");
    if ((cptr->modes_off & MODE_N) &&
        (chptr->modes & MODE_N))
      strcat(modes, "n");
    if ((cptr->modes_off & MODE_T) &&
        (chptr->modes & MODE_T))
      strcat(modes, "t");
    if ((cptr->modes_off & MODE_M) &&
        (chptr->modes & MODE_M))
      strcat(modes, "m");
    if ((cptr->modes_off & MODE_C) &&
        (chptr->modes & MODE_C))
      strcat(modes, "c");
    if ((cptr->modes_off & MODE_G) &&
        (chptr->modes & MODE_G))
      strcat(modes, "g");
    if ((cptr->modes_off & MODE_I) &&
        (chptr->modes & MODE_I))
      strcat(modes, "i");
    if ((cptr->modes_off & MODE_R) &&
        (chptr->modes & MODE_R))
      strcat(modes, "r");
    if ((cptr->modes_off & MODE_Z) &&
        (chptr->modes & MODE_Z))
      strcat(modes, "z");
#if 0
    if ((cptr->modes_off & MODE_CAPF) &&
        (chptr->modes & MODE_CAPF))
      strcat(modes, "F");
#endif
    if ((cptr->modes_off & MODE_CAPP) &&
        (chptr->modes & MODE_CAPP))
      strcat(modes, "P");
    if ((cptr->modes_off & MODE_CAPQ) &&
        (chptr->modes & MODE_CAPQ))
      strcat(modes, "Q");
    if ((cptr->modes_off & MODE_CAPL) &&
        (chptr->modes & MODE_CAPL))
      strcat(modes, "L");
    if ((cptr->modes_off & MODE_CAPR) &&
        (chptr->modes & MODE_CAPR))
      strcat(modes, "R");
    if ((cptr->modes_off & MODE_L) &&
        (chptr->limit))
      strcat(modes, "l");
    if ((cptr->modes_off & MODE_F) &&
        (chptr->forward))
      strcat(modes, "f");
    if ((cptr->modes_off & MODE_CAPJ) &&
        (chptr->throttle))
      strcat(modes, "J");
    if ((cptr->modes_off & MODE_CAPD) &&
        (chptr->dline))
      strcat(modes, "D");
    if ((cptr->modes_off & MODE_K) &&
        (chptr->key))
      {
	strcat(modes, "k ");
	strcat(modes, chptr->key);
      }
  }
  if (modes[1])
  {
    toserv(":%s MODE %s %s\n",
      n_ChanServ,
      cptr->name,
      modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }

  if (cptr->flags & CS_TOPICLOCK)
    cs_SetTopic(FindChannel(cptr->name), cptr->topic);
} /* cs_CheckChan() */

/*
cs_SetTopic()
 Set the topic on the given channel to 'topic'
*/

static void
cs_SetTopic(struct Channel *chanptr, char *topic)

{
  if (!chanptr || !topic)
    return;

#ifdef JOIN_CHANNELS
  if (cs_ShouldBeOnChan(FindChan(chanptr->name)))
  {
#endif
    /*
     * ChanServ should already be on the channel - just set
     * topic normally
     */
    toserv(":%s STOPIC %s ChanServ %ld 0 :%s\n",
	   n_ChanServ,
	   chanptr->name,
	   current_ts,
	   topic);
#ifdef JOIN_CHANNELS
  }
  else
  {
    /*
     * Hybrid won't accept a TOPIC from a user unless they are
     * on the channel - have ChanServ join and leave.
     *
     * Modifications to be sure all fits in linebuf of ircd. -kre
     * However +ins supports topic burst -Janos
     * It won't help if topiclen > ircdbuflen, that was original bug.
     * Anyway, it is dealt with c_topic() code, too. :-) -kre
     */
    /* dancer-hybrid is fine as long as chanserv is a godoper,
     * so all this cute stuff is irrelevant.
     *  -- asuffield
     */
    toserv(":%s SJOIN %ld %s + :@%s\n",
      Me.name,
      chanptr->since,
      chanptr->name,
      n_ChanServ);
    toserv(":%s TOPIC %s :%s\n",
      n_ChanServ,
      chanptr->name,
      topic);
    toserv(":%s PART %s\n",
      n_ChanServ,
      chanptr->name);
  }
#endif
} /* cs_SetTopic() */

/*
cs_CheckModes()
  Called after a mode change, to check if the mode is in conflict
with the mlock modes etc
*/

void
cs_CheckModes(struct Luser *source, struct ChanInfo *cptr,
              int isminus, int mode, struct Luser *lptr)

{
  int slev; /* user level for source */
  char modes[MAXLINE];
  struct Channel *chptr;

  if (!cptr || !source)
    return;

  if ((cptr->flags & CS_FORBID))
    return;

  /*
   * Allow OperServ and ChanServ to set any mode
   */
  if ((source == Me.csptr) || (source == Me.osptr))
    return;

#if 0
  /* contacts can set any mode */
  if (IsContact(source, cptr))
    return;
#endif

  if (!(chptr = FindChannel(cptr->name)))
    return;

  slev = GetAccess(cptr, source);

  if (mode == MODE_O)
  {
    int    ulev; /* user level for (de)opped nick */

    if (!lptr)
      return;

    ulev = GetAccess(cptr, lptr);
    if (isminus)
    {
      /*
       * don't let someone deop a user with a greater user level,
       * but allow a user to deop him/herself
       */
      /* disable this, it's annoying and may lead to floods -- jilles */
#if 0
      if ((lptr != source) &&
          HasAccess(cptr, lptr, CA_CMDOP) &&
          (ulev >= slev))
      {
        if (HasFlag(lptr->nick, NS_NOCHANOPS))
          return;

        ircsprintf(modes, "+o %s", lptr->nick);
        toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
      }
#endif
    }
    else /* mode +o */
    {
      if (((cptr->flags & CS_SECUREOPS) &&
          (!HasAccess(cptr, lptr, CA_AUTOOP) || !HasAccess(cptr, lptr, CA_CMDOP))) ||
	   (GetAccess(cptr, lptr) == cptr->access_lvl[CA_AUTODEOP]))
      {
        ircsprintf(modes, "-o %s", lptr->nick);
        toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
      }
      else if (HasFlag(lptr->nick, NS_NOCHANOPS))
      {
        ircsprintf(modes, "-o %s", lptr->nick);
        toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);

        notice(n_ChanServ, lptr->nick,
          "You are not permitted to have channel op privileges");
        SendUmode(OPERUMODE_Y,
          "Flagged user %s!%s@%s was opped on channel [%s] by %s",
          lptr->nick,
          lptr->username,
          lptr->hostname,
          cptr->name,
          source->nick);
      }
    }
    return;
  } /* if (mode == MODE_O) */

  if (mode == MODE_V)
  {
    if (!isminus)
    {
      /* autodeop people aren't allowed voice status either */
      if (GetAccess(cptr, lptr) == cptr->access_lvl[CA_AUTODEOP])
      {
        ircsprintf(modes, "-v %s", lptr->nick);
        toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
      }
    }

    return;
  } /* if (mode == MODE_V) */

#ifdef HYBRID7
  /* Properly handle autodeop and halfop -Janos
   * XXX: Merge this into upper statement! -kre */
  if (mode == MODE_H)
  {
    if (!isminus)
    {
      /* Autodeop people aren't allowed halfop status either */
      if (GetAccess(cptr, lptr) == cptr->access_lvl[CA_AUTODEOP])
      {
        ircsprintf(modes, "-h %s", lptr->nick);
        toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
        UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
      }
    }
  } /* if (mode == MODE_H) */
#endif /* HYBRID7 */

  /*
   * Check if the mode conflicts with any enforced modes for the
   * channel
   */
  if (cptr->modes_on && isminus)
  {
    modes[0] = '\0';
    if ((mode == MODE_S) &&
        (cptr->modes_on & MODE_S))
      ircsprintf(modes, "+s");
    /* cmode +p has been disabled in dancer for now */
/*     if ((mode == MODE_P) && */
/*         (cptr->modes_on & MODE_P)) */
/*       sprintf(modes, "+p-s"); */
    if ((mode == MODE_N) &&
        (cptr->modes_on & MODE_N))
      ircsprintf(modes, "+n");
#ifdef HYBRID7
    /* -Janos */
    if ((mode == MODE_A) &&
        (cptr->modes_on & MODE_A))
      ircsprintf(modes, "+a");
#endif /* HYBRID7 */
    if ((mode == MODE_T) &&
        (cptr->modes_on & MODE_T))
      ircsprintf(modes, "+t");
    if ((mode == MODE_M) &&
        (cptr->modes_on & MODE_M))
      ircsprintf(modes, "+m");
    if ((mode == MODE_C) &&
        (cptr->modes_on & MODE_C))
      ircsprintf(modes, "+c");
    if ((mode == MODE_G) &&
        (cptr->modes_on & MODE_G))
      ircsprintf(modes, "+g");
    if ((mode == MODE_I) &&
        (cptr->modes_on & MODE_I))
      ircsprintf(modes, "+i");
    if ((mode == MODE_R) &&
        (cptr->modes_on & MODE_R))
      ircsprintf(modes, "+r");
    if ((mode == MODE_Z) &&
        (cptr->modes_on & MODE_Z))
      ircsprintf(modes, "+z");
#if 0
    if ((mode == MODE_CAPF) &&
        (cptr->modes_on & MODE_CAPF))
      ircsprintf(modes, "+F");
#endif
    if ((mode == MODE_CAPP) &&
        (cptr->modes_on & MODE_CAPP))
      ircsprintf(modes, "+P");
    if ((mode == MODE_CAPQ) &&
        (cptr->modes_on & MODE_CAPQ))
      ircsprintf(modes, "+Q");
    if ((mode == MODE_CAPL) &&
        (cptr->modes_on & MODE_CAPL))
      ircsprintf(modes, "+L");
    if ((mode == MODE_CAPR) &&
        (cptr->modes_on & MODE_CAPR))
      ircsprintf(modes, "+R");

    if (modes[0])
    {
      toserv(":%s MODE %s %s\n",
        n_ChanServ,
        cptr->name,
        modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
  }
  if ((mode == MODE_L) &&
      (cptr->limit))
  {
    ircsprintf(modes, "+l %ld", cptr->limit);
    toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }
  if ((mode == MODE_K) &&
      (cptr->key))
  {
    if (!isminus)
    {
      ircsprintf(modes, "-k %s", chptr->key);
      toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
    ircsprintf(modes, "+k %s", cptr->key);
    toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }
  if ((mode == MODE_F) &&
      (cptr->forward))
  {
    if (!isminus)
    {
      sprintf(modes, "-f %s", chptr->forward);
      toserv(":%s MODE %s %s\n",
        n_ChanServ,
        cptr->name,
        modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
    sprintf(modes, "+f %s", cptr->forward);
    toserv(":%s MODE %s %s\n",
      n_ChanServ,
      cptr->name,
      modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }
  if ((mode == MODE_CAPJ) &&
      (cptr->throttle))
  {
    if (!isminus)
    {
      sprintf(modes, "-J %s", chptr->throttle);
      toserv(":%s MODE %s %s\n",
        n_ChanServ,
        cptr->name,
        modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
    sprintf(modes, "+J %s", cptr->throttle);
    toserv(":%s MODE %s %s\n",
      n_ChanServ,
      cptr->name,
      modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }

  if ((mode == MODE_CAPD) &&
      (cptr->dline))
  {
    if (!isminus)
    {
      sprintf(modes, "-D %s", chptr->dline);
      toserv(":%s MODE %s %s\n",
        n_ChanServ,
        cptr->name,
        modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
    sprintf(modes, "+D %s", cptr->dline);
    toserv(":%s MODE %s %s\n",
      n_ChanServ,
      cptr->name,
      modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
  }

  if (cptr->modes_off && !isminus)
  {
    modes[0] = '\0';
    if ((mode == MODE_S) &&
        (cptr->modes_off & MODE_S))
      ircsprintf(modes, "-s");
    if ((mode == MODE_P) &&
        (cptr->modes_off & MODE_P))
      ircsprintf(modes, "-p");
    if ((mode == MODE_N) &&
        (cptr->modes_off & MODE_N))
      ircsprintf(modes, "-n");
#ifdef HYBRID7
    /* -Janos */
    if ((mode == MODE_A) &&
        (cptr->modes_off & MODE_S))
      ircsprintf(modes, "-a");
#endif /* HYBRID7 */
    if ((mode == MODE_T) &&
        (cptr->modes_off & MODE_T))
      ircsprintf(modes, "-t");
    if ((mode == MODE_M) &&
        (cptr->modes_off & MODE_M))
      ircsprintf(modes, "-m");
    if ((mode == MODE_C) &&
        (cptr->modes_off & MODE_C))
      ircsprintf(modes, "-c");
    if ((mode == MODE_G) &&
        (cptr->modes_off & MODE_G))
      ircsprintf(modes, "-g");
    if ((mode == MODE_I) &&
        (cptr->modes_off & MODE_I))
      ircsprintf(modes, "-i");
    if ((mode == MODE_R) &&
        (cptr->modes_off & MODE_R))
      ircsprintf(modes, "-r");
    if ((mode == MODE_Z) &&
        (cptr->modes_off & MODE_Z))
      ircsprintf(modes, "-z");
#if 0
    if ((mode == MODE_CAPF) &&
        (cptr->modes_off & MODE_CAPF))
      ircsprintf(modes, "-F");
#endif
    if ((mode == MODE_CAPJ) &&
        (cptr->modes_off & MODE_CAPJ))
      ircsprintf(modes, "-J");
    if ((mode == MODE_CAPD) &&
        (cptr->modes_off & MODE_CAPD))
      ircsprintf(modes, "-D");
    if ((mode == MODE_CAPP) &&
        (cptr->modes_off & MODE_CAPP))
      ircsprintf(modes, "-P");
    if ((mode == MODE_CAPQ) &&
        (cptr->modes_off & MODE_CAPQ))
      ircsprintf(modes, "-Q");
    if ((mode == MODE_CAPL) &&
        (cptr->modes_off & MODE_CAPL))
      ircsprintf(modes, "-L");
    if ((mode == MODE_CAPR) &&
        (cptr->modes_off & MODE_CAPR))
      ircsprintf(modes, "-R");
    if ((mode == MODE_L) &&
        (cptr->modes_off & MODE_L))
      ircsprintf(modes, "-l");
    if ((mode == MODE_K) &&
        (cptr->modes_off & MODE_K))
    {
      if (chptr->key)
        ircsprintf(modes, "-k %s", chptr->key);
    }
    if ((mode == MODE_F) &&
        (cptr->modes_off & MODE_F))
    {
      if (chptr->forward)
        ircsprintf(modes, "-f %s", chptr->forward);
    }

    if (modes[0])
    {
      toserv(":%s MODE %s %s\n",
        n_ChanServ,
        cptr->name,
        modes);
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }
  }
} /* cs_CheckModes() */

/*
cs_CheckTopic()
 Called when a topic is changed - if topiclock is enabled, change
it back
*/

void
cs_CheckTopic(char *who, char *channel, char *topic, time_t topic_ts)
{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(channel)))
    return;

  if ((cptr->topic_ts < topic_ts) && (!(cptr->flags & CS_TOPICLOCK) ||
			  HasAccess(cptr, FindClient(who), CA_TOPIC)))
    {
      MyFree(cptr->topic);
      cptr->topic = MyStrdup(topic);
      cptr->topic_ts = topic_ts;
    }
  else
    {
      if (cptr->flags & CS_TOPICLOCK)
	{
	  if (cptr->topic)
	    cs_SetTopic(FindChannel(cptr->name), cptr->topic);
	  else
	    cs_SetTopic(FindChannel(cptr->name), "");
	}
    }
} /* cs_CheckTopic() */

/*
cs_CheckOp()
  Check if nick has AutoOp access on 'chanptr', if so
op nick
*/

void
cs_CheckOp(struct Channel *chanptr, struct ChanInfo *cptr, char *nick)

{
  struct ChannelUser *tempuser;
  char modes[MAXLINE];

  if (!chanptr || !cptr || !nick)
    return;

    /*
     * If the user is flagged to be denied channel op
     * privileges, do nothing
     */
  if (HasFlag(nick, NS_NOCHANOPS))
    return;

  /*
   * If ChanServ should be on the channel, but isn't, don't
   * do anything
   */
  if (cs_ShouldBeOnChan(cptr) && !IsChannelOp(chanptr, Me.csptr))
    return;

  if (!(tempuser = FindUserByChannel(chanptr, FindClient(nick))))
    return;

  if (tempuser->flags & CH_OPPED)
  {
    /*
     * They're already opped on the channel - we don't need to do anything
     */
    return;
  }

  if (HasAccess(cptr, tempuser->lptr, CA_AUTOOP))
  {
    ircsprintf(modes, "+o %s", tempuser->lptr->nick);
    toserv(":%s MODE %s %s\n", n_ChanServ, chanptr->name, modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chanptr, modes);
  }
#ifdef HYBRID7
  /* Add autohalfop -Janos */
  else if (!(tempuser->flags & CH_HOPPED) &&
        HasAccess(cptr, tempuser->lptr, CA_AUTOHALFOP))
  {
    ircsprintf(modes, "+h %s", tempuser->lptr->nick);
    toserv(":%s MODE %s %s\n", n_ChanServ, chanptr->name, modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chanptr, modes);
  }
#endif /* HYBRID7 */
  else
    {
      if (!(tempuser->flags & CH_VOICED) &&
          HasAccess(cptr, tempuser->lptr, CA_AUTOVOICE))
        {
          ircsprintf(modes, "+v %s", tempuser->lptr->nick);
          toserv(":%s MODE %s %s\n", n_ChanServ, chanptr->name, modes);
          UpdateChanModes(Me.csptr, n_ChanServ, chanptr, modes);
        }
      else
        {
          struct UserChannel *uc = FindChannelByUser(tempuser->lptr, chanptr);
          /* If the user is not being opped right now, schedule a recheck
           * when they identify
           */
          if (uc)
            uc->redo_cs_checkop = 1;
        }
    }
} /* cs_CheckOp() */

/*
cs_CheckJoin()
  Check if 'nick' is on the AutoRemove list for chanptr; if not, send
the entry message to 'nickname' (if there is one)
*/

void
cs_CheckJoin(struct Channel *chanptr, struct ChanInfo *cptr, char *nickname)

{
  struct Luser *lptr;
  struct NickInfo *nptr;
  struct AutoKick *ak;
  char chkstr[MAXLINE];
  char chkstr2[MAXLINE];
  char modes[MAXLINE];
  char *nick;

  if (!chanptr || !cptr || !nickname)
    return;

  nick = nickname;
  if (IsNickPrefix(*nick))
  {
    if (IsNickPrefix(*(++nick)))
      ++nick;
  }

  if (!(lptr = FindClient(nick)))
    return;

  if (FindService(lptr))
    return;

  /*
   * If the channel is restricted, and lptr just created it,
   * cs_CheckChan(), called from cs_CheckSjoin() would have
   * already banned and removed lptr, in which case they would no
   * longer be on the channel, but s_sjoin() would not know
   * that - catch it here.
   */
  if (!IsChannelMember(chanptr, lptr))
    return;

#ifndef HYBRID_ONLY
  /*
   * Check if ChanServ has joined the channel yet - this might
   * be the initial SJOIN burst in which case we can't do anything
   */
  if (!IsChannelMember(chanptr, Me.csptr))
    return;
#endif /* !HYBRID_ONLY */

  if (cptr->flags & CS_FORBID)
  {
    /*
     * Normally, we would never get here since ChanServ would have
     * removed everyone from the forbidden channel when services
     * was first started.  However, if an administrator uses
     * OMODE -i on the channel, they could conceivably join again,
     * so remove them again
     */

    if (!IsChannelMember(chanptr, Me.csptr))
      cs_joinchan(cptr);

    toserv(":%s MODE %s +is\n",
      n_ChanServ,
      cptr->name);
    UpdateChanModes(Me.csptr, n_ChanServ, chanptr, "+is");
    KickBan(0, n_ChanServ, chanptr, lptr->nick, "Forbidden Channel");
    return;
  }

  ircsprintf(chkstr, "%s!%s@%s", lptr->nick, lptr->username,
      lptr->hostname);
  /* Also check the ip for akicks so they work the same way as
   * channel bans -- jilles */
  ircsprintf(chkstr2, "%s!%s@%s", lptr->nick, lptr->username,
      lptr->realip);

  /*
   * Don't remove contacts who have identified - even if they match
   * an AKICK entry
   */
  /*
   * Also, don't remove users matching ban exceptions as they
   * could simply rejoin, causing a flood -- jilles
   */
  if (((ak = OnAkickList(cptr, chkstr)) || (ak = OnAkickList(cptr, chkstr2)))
		  && !IsContact(lptr, cptr) && !HasException(chanptr, lptr))
  {
    if (chanptr->numusers == 1)
    {
      /*
       * This user is the only one on the channel -
       * if we remove him, he could just rejoin
       * since the channel would have been dropped,
       * have ChanServ join the channel for a few
       * minutes
       */
      cs_joinchan(cptr);
    }

    ircsprintf(modes, "+b %s", ak->hostmask);
    toserv(":%s MODE %s %s\n", n_ChanServ, chanptr->name, modes);
    UpdateChanModes(Me.csptr, n_ChanServ, chanptr, modes);

    toserv(":%s REMOVE %s %s :%s\n", n_ChanServ, chanptr->name,
        lptr->nick, ak->reason ? ak->reason : "");
    RemoveFromChannel(chanptr, lptr);
  }
  else if ((cptr->flags & CS_RESTRICTED) &&
           !HasAccess(cptr, lptr, CA_AUTOOP))
  {
    char  *mask = HostToMask(lptr->username, lptr->hostname);

    if (!IsChannelMember(chanptr, Me.csptr))
      cs_joinchan(cptr);

    ircsprintf(modes, "+b *!%s", mask);

    toserv(":%s MODE %s %s\n", n_ChanServ, chanptr->name, modes);

    UpdateChanModes(Me.csptr, n_ChanServ, chanptr, modes);

    MyFree(mask);

    toserv(":%s REMOVE %s %s :%s\n",
      n_ChanServ,
      chanptr->name,
      lptr->nick,
      "Restricted Channel");

    RemoveFromChannel(chanptr, lptr);
  }
  else if (cptr->entrymsg && burst_complete) /* Don't send entry messages during initial burst */
  {
    /*
     * Send cptr->entrymsg to lptr->nick in the form of a NOTICE
     */
    toserv(":%s NOTICE %s :[%s] %s\n",
      n_ChanServ,
      lptr->nick,
      chanptr->name,
      cptr->entrymsg);
  }

  /* Is this the contact? (not somebody with contact _access_, but the actual contact nick) */
  if ((nptr = FindNick(lptr->nick)))
    {
      if (nptr->flags & NS_IDENTIFIED)
	{
	  if (cptr->contact && irccmp(lptr->nick, cptr->contact) == 0)
	    /* That's the contact joining. Update activity timer */
	    cptr->last_contact_active = current_ts;
	  if (cptr->alternate && irccmp(lptr->nick, cptr->alternate) == 0)
	    cptr->last_alternate_active = current_ts;
	}
    }
} /* cs_CheckJoin() */

/*
cs_CheckSjoin()
 Called from s_sjoin() to check if the SJOINing nicks are
allowed to have operator status on "chptr->name"

nickcnt : number of SJOINing nicks
nicks   : array of SJOINing nicks
newchan : 1 if a newly created channel
*/

void
cs_CheckSjoin(struct Channel *chptr, struct ChanInfo *cptr,
              int nickcnt, char **nicks, int newchan)

{
  struct Luser *nlptr;
  char *dnicks, /* nicks to deop */
       *currnick;
  int ii; /* looping */

  if (!chptr || !cptr || !nicks)
    return;

#ifndef HYBRID_ONLY

  if (IsChannelMember(chptr, Me.csptr))
  {
    /*
     * ChanServ is already on the channel so it is not
     * a brand new channel - we don't have to worry about
     * the new user(s) who joined
     */
    return;
  }

#endif /* !HYBRID_ONLY */

 /*
  * This is where ChanServ will join the channel if its
  * registered.  We just got the TS, so we know how to get
  * ChanServ opped. The thing is, ChanServ won't join any
  * registered channels which are empty on startup, since
  * there won't be an SJOIN associated with it; thus the
  * moment someone joins such a channel, an SJOIN will
  * be sent, causing ChanServ to join (theres little point in
  * ChanServ sitting in an empty channel anyway)
  */

#ifdef HYBRID_ONLY

  /*
   * Only call cs_join() if the channel is brand new,
   * or ChanServ will be changing the topic or setting
   * mlock modes everytime someone joins.
   */
  if (newchan)
    cs_join(cptr);

#else

  cs_join(cptr);

#endif /* HYBRID_ONLY */

  /*
   * If SPLITOPS is on, don't worry about anything else,
   * just let everyone get opped - toot 26/6/2000
   */
  if (cptr->flags & CS_SPLITOPS)
  {
    return;
  }

  /*
   * First, if AllowAccessIfSOp is enabled, check if there
   * is a SuperOp currently opped on the channel. If there
   * is, return because we don't need to deop anyone in
   * that case. If the channel has RESTRICTED or SECUREOPS
   * enabled, we deop non-autops regardless of having
   * a SuperOp on the channel.
   */
  if (AllowAccessIfSOp &&
      !(cptr->flags & CS_RESTRICTED) &&
      !(cptr->flags & CS_SECUREOPS))
  {
    struct ChannelUser *tempu;

    for (tempu = chptr->firstuser; tempu; tempu = tempu->next)
    {
      if (HasAccess(cptr, tempu->lptr, CA_SUPEROP))
      {
        /*
         * There is at least 1 SuperOp opped on the channel
         * and SECUREOPS/RESTRICTED is off, so allow the 
         * other ops to stay opped, regardless of being
         * an Aop or higher
         */
        return;
      }
    }
  } /* if (AllowAccessIfSOp) */

 /*
   * deop the nick(s) who created the channel if they
   * aren't an autoop or higher
   */
  dnicks = (char *) MyMalloc(sizeof(char));
  dnicks[0] = '\0';

  for (ii = 0; ii < nickcnt; ii++)
  {
    currnick = GetNick(nicks[ii]);
    nlptr = FindClient(currnick);

    if (!nlptr)
      continue;

    if (FindService(nlptr))
      continue;

    if (!IsChannelOp(chptr, nlptr))
      continue;

    if (!HasFlag(nlptr->nick, NS_NOCHANOPS) &&
        (HasAccess(cptr, nlptr, CA_AUTOOP) ||
	 HasAccess(cptr, nlptr, CA_CMDOP)))
      continue;

    dnicks = (char *) MyRealloc(dnicks, strlen(dnicks) + strlen(nlptr->nick) + (2 * sizeof(char)));
    strcat(dnicks, nlptr->nick);
    strcat(dnicks, " ");

    /*
     * Send them a notice so they know why they're being
     * deoped
     */
    if (GiveNotice)
    {
      toserv(":%s NOTICE %s :You do not have channel operator access to [%s]\n",
        n_ChanServ,
        nlptr->nick,
        cptr->name);
    }
  }

  /*
   * Deop the non-autoops
   */
  SetModes(n_ChanServ, 0, 'o', chptr, dnicks);

  MyFree(dnicks);
} /* cs_CheckSjoin() */

/*
cs_ShouldBeOnChan()
 Determine whether ChanServ should be on the channel 'cptr'.
If so, return 1, otherwise 0
*/

int
cs_ShouldBeOnChan(struct ChanInfo *cptr)

{
  if (!cptr)
    return (0);

#ifndef HYBRID_ONLY

  /*
   * Since HYBRID_ONLY is not defined, ChanServ should be on
   * every single monitored channel
   */
  return (1);

#endif /* !HYBRID_ONLY */

  /*
   * Now, HYBRID_ONLY is defined, so generally ChanServ should NOT
   * be on the channel. However, if the channel has SET GUARD
   * enabled, ChanServ should be monitoring the channel.
   */
  if (AllowGuardChannel)
    if (cptr->flags & CS_GUARD)
      return (1);

  return (0);
} /* cs_ShouldBeOnChan() */

/*
cs_RejoinChannels()
 Have ChanServ rejoin all channel's it should be in
*/

void
cs_RejoinChannels()

{
  int ii;
  struct ChanInfo *cptr;

  for (ii = 0; ii < CHANLIST_MAX; ii++)
  {
    for (cptr = chanlist[ii]; cptr; cptr = cptr->next)
    {
      if (!cs_ShouldBeOnChan(cptr))
        continue;
      if (!FindChannel(cptr->name))
        continue;

      cs_join(cptr);
    }
  }
} /* cs_RejoinChannels() */

/*
MakeContact()
 Change the alternate contact on cptr to contact
*/

void
MakeContact(struct ChanInfo *cptr)

{
  struct NickInfo *nptr;
  assert(cptr && cptr->alternate);

  putlog(LOG2,
    "%s: changing alternate contact [%s] to contact on [%s]",
    n_ChanServ,
    cptr->alternate,
    cptr->name);

  if (cptr->contact)
    MyFree(cptr->contact);

  cptr->contact = cptr->alternate;
  cptr->last_contact_active = cptr->last_alternate_active;
  cptr->alternate = NULL;
  cptr->last_alternate_active = 0;

  nptr = FindNick(cptr->contact);
  if (nptr)
    AddContactChannelToNick(&nptr, cptr);
  else
    {
      putlog(LOG2,
             "%s: dropping channel [%s] (alternate contact is not registered)",
             n_ChanServ,
             cptr->name);
      
      DeleteChan(cptr);
    }
} /* MakeContact() */

/*
MakeChan()
 Create a ChanInfo structure and return it
*/

static struct ChanInfo *
MakeChan()

{
  struct ChanInfo *chanptr;

  chanptr = (struct ChanInfo *) MyMalloc(sizeof(struct ChanInfo));

  memset(chanptr, 0, sizeof(struct ChanInfo));

  return (chanptr);
} /* MakeChan() */

/*
ExpireChannels()
 Delete any channels that have expired.
*/

void
ExpireChannels(time_t unixtime)
{
  int ii;
  struct ChanInfo *cptr, *next;
  struct Channel *chptr;

  if (!ChannelExpire)
    return;

  for (ii = 0; ii < CHANLIST_MAX; ++ii)
  {
    for (cptr = chanlist[ii]; cptr; cptr = next)
    {
      next = cptr->next;

      if ((!(cptr->flags & (CS_FORBID | CS_NOEXPIRE))) &&
          ((unixtime - cptr->lastused) >= ChannelExpire))
      {
        putlog(LOG2,
          "%s: Expired channel: [%s]",
          n_ChanServ,
          cptr->name);

        chptr = FindChannel(cptr->name);
        if (IsChannelMember(chptr, Me.csptr))
          cs_part(chptr);

        DeleteChan(cptr);
      }
    } /* for (cptr = chanlist[ii]; cptr; cptr = next) */
  }
} /* ExpireChannels() */

#ifndef HYBRID_ONLY

/*
CheckEmptyChans()
 Part any channels in which ChanServ is the only user
*/

void
CheckEmptyChans()

{
  struct UserChannel *tempc;
  struct UserChannel *temp;

  if (Me.csptr)
  {
    for (tempc = Me.csptr->firstchan; tempc; )
    {
      if (tempc->chptr->numusers == 1)
      {
        /* ChanServ is the only client on the channel, part */
        SendUmode(OPERUMODE_Y,
          "%s: Parting empty channel [%s]",
          n_ChanServ,
          tempc->chptr->name);

        temp = tempc->next;
        cs_part(tempc->chptr);
        tempc = temp;
      }
      else
        tempc = tempc->next;
    }
  }
} /* CheckEmptyChans() */

#endif /* !HYBRID_ONLY */

/*
FindChan()
  Return a pointer to registered channel 'channel'
*/

struct ChanInfo *
FindChan(char *channel)

{
  struct ChanInfo *cptr;
  unsigned int hashv;

  if (!channel)
    return (NULL);

  hashv = CSHashChan(channel);
  for (cptr = chanlist[hashv]; cptr; cptr = cptr->next)
    if (!irccmp(cptr->name, channel))
      return (cptr);

  return (NULL);
} /* FindChan() */

/*
AddContact()
  Add contact 'lptr' to 'chanptr'
*/

static void
AddContact(struct Luser *lptr, struct ChanInfo *chanptr)

{
  struct aChannelPtr *fcptr;
  struct f_users *fuptr;

  if (!lptr || !chanptr)
    return;

  fcptr = (struct aChannelPtr *) MyMalloc(sizeof(struct aChannelPtr));
  fcptr->cptr = chanptr;
  fcptr->next = lptr->contact_channels;
  lptr->contact_channels = fcptr;

  fuptr = (struct f_users *) MyMalloc(sizeof(struct f_users));
  fuptr->lptr = lptr;
  fuptr->next = chanptr->contacts;
  chanptr->contacts = fuptr;
} /* AddContact() */

/*
RemContact()
  Remove lptr->nick from the list of channel contacts for 'cptr'
*/

void
RemContact(struct Luser *lptr, struct ChanInfo *cptr)

{
  struct aChannelPtr *fcptr, *cprev;
  struct f_users *fuptr, *uprev;

  if (!cptr || !lptr)
    return;

  cprev = NULL;
  for (fcptr = lptr->contact_channels; fcptr; )
  {
    if (fcptr->cptr == cptr)
    {
      if (cprev)
      {
        cprev->next = fcptr->next;
        MyFree(fcptr);
        fcptr = cprev;
      }
      else
      {
        lptr->contact_channels = fcptr->next;
        MyFree(fcptr);
        fcptr = NULL;
      }

      /*
       * cptr should occur only once in the contact list -
       * break after we find it
       */
      break;
    }

    cprev = fcptr;

    if (fcptr)
      fcptr = fcptr->next;
    else
      fcptr = lptr->contact_channels;
  }

  uprev = NULL;
  for (fuptr = cptr->contacts; fuptr; )
  {
    if (fuptr->lptr == lptr)
    {
      if (uprev)
      {
        uprev->next = fuptr->next;
        MyFree(fuptr);
        fuptr = uprev;
      }
      else
      {
        cptr->contacts = fuptr->next;
        MyFree(fuptr);
        fuptr = NULL;
      }

      break;
    }

    uprev = fuptr;

    if (fuptr)
      fuptr = fuptr->next;
    else
      fuptr = cptr->contacts;
  }
} /* RemContact() */

/*
ChangeChanPass()
  Change channel contact password for cptr->name to 'newpass'
*/

static int
ChangeChanPass(struct ChanInfo *cptr, char *newpass)

{
  if (!cptr)
    return 0;

  if ( cptr->password)
    MyFree(cptr->password);

  #ifdef CRYPT_PASSWORDS 

  if (newpass != NULL)
    cptr->password = MyStrdup(pwmake(newpass, &(cptr->name[1])));
  else
    cptr->password = MyStrdup("*");

  #else

  if (newpass != NULL)
    cptr->password = MyStrdup(newpass);
  else
    cptr->password = MyStrdup("*");

  #endif

  return 1;
} /* ChangeChanPass() */

/*
AddChan()
  Add 'chanptr' to chanlist table
*/

static void
AddChan(struct ChanInfo *chanptr)

{
  unsigned int hashv;

  /* 
   * all the fields of chanptr were filled in already, so just 
   * insert it in the list
   */
  hashv = CSHashChan(chanptr->name);

  chanptr->prev = NULL;
  chanptr->next = chanlist[hashv];
  if (chanptr->next)
    chanptr->next->prev = chanptr;

  chanlist[hashv] = chanptr;

  ++Network->TotalChans;
} /* AddChan() */

/*
DeleteChan()
 Deletes channel 'chanptr'
*/

void
DeleteChan(struct ChanInfo *chanptr)

{
  struct NickInfo *nptr;
  struct AutoKick *ak;
  struct ChanAccess *ca;
  struct f_users *fdrs;
  int hashv;
#ifdef MEMOSERVICES
  struct MemoInfo *mi;
#endif

  if (!chanptr)
    return;

  hashv = CSHashChan(chanptr->name);

#ifdef MEMOSERVICES
  /* check if the chan had any memos - if so delete them */
  if ((mi = FindMemoList(chanptr->name)))
    DeleteMemoList(mi);
#endif

  if (chanptr->password)
    MyFree(chanptr->password);
  if (chanptr->topic)
    MyFree(chanptr->topic);
  if (chanptr->key)
    MyFree(chanptr->key);
  if (chanptr->forward)
    MyFree(chanptr->forward);

  while (chanptr->akick)
  {
    ak = chanptr->akick->next;
    MyFree(chanptr->akick->hostmask);
    if (chanptr->akick->reason)
      MyFree(chanptr->akick->reason);
    MyFree(chanptr->akick);
    chanptr->akick = ak;
  }

  while (chanptr->access)
  {
    ca = chanptr->access->next;

    /*
     * If there is an nptr entry in this access node, we must
     * delete it's corresponding AccessChannel entry.
     */
    if (chanptr->access->nptr && chanptr->access->acptr)
      DeleteAccessChannel(chanptr->access->nptr, chanptr->access->acptr);

    if (chanptr->access->hostmask)
      MyFree(chanptr->access->hostmask);
    MyFree(chanptr->access);
    chanptr->access = ca;
  }

  while (chanptr->contacts)
  {
    fdrs = chanptr->contacts->next;
    RemContact(chanptr->contacts->lptr, chanptr);
    chanptr->contacts = fdrs;
  }

  MyFree(chanptr->name);

  if (chanptr->contact)
  {
    if (!(chanptr->flags & CS_FORBID) &&
        (nptr = GetLink(chanptr->contact)))
      RemoveContactChannelFromNick(&nptr, chanptr);

    MyFree(chanptr->contact);
  }

  if (chanptr->alternate)
    MyFree(chanptr->alternate);
  if (chanptr->email)
    MyFree(chanptr->email);
  if (chanptr->url)
    MyFree(chanptr->url);
  if (chanptr->entrymsg)
    MyFree(chanptr->entrymsg);
  if (chanptr->access_lvl)
    MyFree(chanptr->access_lvl);

  if (chanptr->next)
    chanptr->next->prev = chanptr->prev;
  if (chanptr->prev)
    chanptr->prev->next = chanptr->next;
  else
    chanlist[hashv] = chanptr->next;

  MyFree(chanptr);

  --Network->TotalChans;
} /* DeleteChan() */

/*
 * AddAccess()
 * Add 'mask' or 'nptr' to the access list for 'chanptr' with 'level'
 * Return 1 if successful, 0 if not
 *
 * rewrote this to use master nicknames for access inheritance -kre
 */
static int AddAccess(struct ChanInfo *chanptr, struct Luser *lptr, char
    *mask, struct NickInfo *nptr, int level, time_t created, time_t last_used)
{
  struct ChanAccess *ptr;
  struct NickInfo *master_nptr;

  if (!chanptr || (!mask && !nptr))
    return 0;
  
  /* get master */
  master_nptr = GetMaster(nptr);

  if ((ptr = OnAccessList(chanptr, mask, master_nptr)))
  {
    /* 'mask' is already on the access list - just change the level */
    if (lptr)
      if (GetAccess(chanptr, lptr) <= ptr->level)
        return 0;

    ptr->level = level;
    return 1;
  }

  ptr = (struct ChanAccess *) MyMalloc(sizeof(struct ChanAccess));
  memset(ptr, 0, sizeof(struct ChanAccess));

  ptr->created = created;
  ptr->last_used = last_used;

  if (master_nptr)
  {
    ptr->nptr = master_nptr;
    ptr->hostmask = NULL;

    /* We also want the NickInfo structure to keep records of what
     * channels it has access on, so if we ever delete the nick, we will
     * know where to remove it's access. */
    ptr->acptr = AddAccessChannel(master_nptr, chanptr, ptr);
  }
  else
    ptr->hostmask = MyStrdup(mask);

  ptr->level = level;

  ptr->prev = NULL;
  ptr->next = chanptr->access;
  if (ptr->next)
    ptr->next->prev = ptr;

  chanptr->access = ptr;

  return 1;
} /* AddAccess() */

/*
 * DelAccess()
 * Delete 'mask' from cptr's access list
 * Return -1 if 'mask' has a higher user level than lptr, otherwise
 * the number of matches
 *
 * rewrote using master nicks -kre
 */
static int DelAccess(struct ChanInfo *cptr, struct Luser *lptr, char
    *mask, struct NickInfo *nptr)
{
  struct ChanAccess *temp, *next;
  struct NickInfo *master_nptr;
  int ret = 0, cnt = 0, ulev;
  int found;

  if (!cptr || !lptr || (!mask && !nptr))
    return 0;

  if (!cptr->access)
    return 0;

  master_nptr = GetMaster(nptr);

  ulev = GetAccess(cptr, lptr);

  for (temp = cptr->access; temp; temp = next)
  {
    found = 0;
    next = temp->next;

    if (master_nptr && temp->nptr && (master_nptr == GetMaster(temp->nptr)))
      found = 1;

    if (mask && temp->hostmask)
      if (!irccmp(mask, temp->hostmask))
        found = 1;

    if (found)
    {
     /* don't let lptr->nick delete a hostmask that has a >= level than it
      * does */
      if (temp->level >= ulev)
      {
        ret = -1;
        continue;
      }

      ++cnt;

      /* master_nptr will have an AccessChannel entry which is set to
       * 'temp', so it must be deleted */
      if (master_nptr && temp->acptr)
        DeleteAccessChannel(temp->nptr, temp->acptr);

      DeleteAccess(cptr, temp);
    } /* if (found) */
  }
  
  if (cnt > 0)
    return (cnt);

  return (ret);
} /* DelAccess() */

/* Contract any duplicate entries for a nick into one
 * Returns the number of entries removed -- jilles */
int RemoveDupAccess(struct ChanInfo *cptr, struct NickInfo *nptr)
{
	struct ChanAccess *ca, *first = NULL, *next;
	int count = 0;

	if (cptr == NULL)
		return 0;
	if (nptr == NULL)
	{
		putlog(LOG2, "%s: RemoveDupAccess(%s, NULL) called",
				n_ChanServ, cptr->name);
		return 0;
	}

	ca = cptr->access;
	while (ca != NULL)
	{
		next = ca->next; /* avoid referencing freed memory */
		if (ca->nptr == nptr)
		{
			count++;
			if (first == NULL)
				first = ca;
			else
			{
				if (ca->level > first->level)
					first->level = ca->level;
				if (ca->created < first->created)
					first->created = ca->created;
				if (ca->last_used > first->last_used)
					first->last_used = ca->last_used;
				DeleteAccessChannel(nptr, ca->acptr);
				DeleteAccess(cptr, ca);
			}
		}
		ca = next;
	}
	if (count != 1)
		putlog(LOG2, "%s: RemoveDupAccess(%s, %s) found %d entries",
				n_ChanServ, cptr->name, nptr->nick, count);
	return count > 1 ? count - 1 : 0;
}

/*
 * DeleteAccess()
 * Free the ChanAccess structure 'ptr'
 */
void DeleteAccess(struct ChanInfo *cptr, struct ChanAccess *ptr)
{
  assert(cptr != 0);
  assert(ptr != 0);

  if (ptr->next)
    ptr->next->prev = ptr->prev;
  if (ptr->prev)
    ptr->prev->next = ptr->next;
  else
    cptr->access = ptr->next;

  MyFree(ptr->hostmask);
  MyFree(ptr);
} /* DeleteAccess() */

/*
 * AddAkick()
 * Add 'mask' to 'chanptr' 's autoremove list
 */
static int AddAkick(struct ChanInfo *chanptr, struct Luser *lptr, char
    *mask, char *reason)
{
  struct AutoKick *ptr;

  if (!chanptr || !mask)
    return 0;

  if (lptr)
  {
    struct ChanAccess *ca;
    int found = 0;

    /*
     * Check if lptr is trying to add an akick that matches a
     * level higher than their own
     */
    for (ca = chanptr->access; ca; ca = ca->next)
    {
      if (ca->hostmask)
        if (match(mask, ca->hostmask))
          found = 1;

      if (ca->nptr)
      {
        struct NickHost *hptr;
        char chkstr[MAXLINE];

        for (hptr = ca->nptr->hosts; hptr; hptr = hptr->next)
        {
          if (strchr(hptr->hostmask, '!'))
            strcpy(chkstr, hptr->hostmask);
          else
            ircsprintf(chkstr, "*!%s", hptr->hostmask);

          if (match(mask, chkstr))
          {
            found = 1;
            break;
          }
        }
      }

      if (found)
      {
        if (ca->level >= GetAccess(chanptr, lptr))
          return 0;
        found = 0;
      }
    }
  }

  ptr = (struct AutoKick *) MyMalloc(sizeof(struct AutoKick));
  ptr->hostmask = MyStrdup(mask);
  if (reason)
    ptr->reason = MyStrdup(reason);
  else
    ptr->reason = NULL;
  ptr->next = chanptr->akick;
  chanptr->akick = ptr;
  chanptr->akickcnt++;

  return 1;
} /* AddAkick() */

/*
DelAkick()
  Delete 'mask' from cptr's autoremove list; return number of matches
*/

static int
DelAkick(struct ChanInfo *cptr, char *mask)

{
  struct AutoKick *temp, *prev, *next;
  int cnt;

  if (!cptr || !mask)
    return 0;

  if (!cptr->akick)
    return 0;

  cnt = 0;

  prev = NULL;
  for (temp = cptr->akick; temp; temp = next)
  {
    next = temp->next;

    /* Allow exact matches as well as glob ones */
    if (match(mask, temp->hostmask) || !strcmp(mask, temp->hostmask))
    {
      ++cnt;

      MyFree(temp->hostmask);
      if (temp->reason)
        MyFree(temp->reason);

      if (prev)
      {
        prev->next = temp->next;
        MyFree(temp);
        temp = prev;
      }
      else
      {
        cptr->akick = temp->next;
        MyFree(temp);
        temp = NULL;
      }
    }

    prev = temp;
  }

  cptr->akickcnt -= cnt;

  return (cnt);
} /* DelAkick() */

/*
IsContact()
  Determine if lptr->nick has identified as a contact for 'cptr'
*/

static int
IsContact(struct Luser *lptr, struct ChanInfo *cptr)

{
  struct f_users *temp;

#ifdef EMPOWERADMINS_MORE
  if (IsValidAdmin(lptr))
    return 1;
#endif

  for (temp = cptr->contacts; temp; temp = temp->next)
    if (temp->lptr == lptr)
      return 1;

  return 0;
} /* IsContact() */

/*
GetAccessDirect()
  Return highest access level 'lptr' has on 'cptr', without checking for
   NS_IDENTIFIED etc
*/
static int
GetAccessDirect(struct ChanInfo *cptr, struct Luser *lptr, struct NickInfo *nptr)
{
  char chkstr[MAXLINE];
  struct ChanAccess *ca;
  int level = (-9999);
  int found;

  ircsprintf(chkstr, "%s!%s@%s", lptr->nick, lptr->username,
      lptr->hostname);

  for (ca = cptr->access; ca; ca = ca->next)
  {
    found = 0;
    if (nptr && ca->nptr)
    {
      if (nptr == ca->nptr)
        found = 1;
      else
      {
      #ifdef LINKED_NICKNAMES
        /*
         * If nptr and ca->nptr are the in the same link,
         * give nptr the exact same access ca->nptr has
         */
        if (IsLinked(nptr, ca->nptr))
          found = 1;
      #endif /* LINKED_NICKNAMES */
      }
    }

    if (ca->hostmask)
      if (match(ca->hostmask, chkstr))
        found = 1;

    if (found)
      {
	{
	  struct Channel *chptr = FindChannel(cptr->name);
	  /* Only if the user is currently on the channel */
	  if (IsChannelMember(chptr, lptr))
	    /* Update usage time for this access level */
	    cptr->lastused = ca->last_used = current_ts;
	}
	if (ca->level >= level)
	  {
	    /*
	     * if ca->level is greater then level, keep going because
	     * there may be another hostmask in the access list
	     * which matches lptr's userhost which could have an even
	     * bigger access level
	     */
	    level = ca->level;
	  }
      }
  }

  if (level == (-9999))
    return 0;
  else
    return (level);
}

/*
GetAccess()
  Return highest access level 'lptr' has on 'cptr'
*/

static int
GetAccess(struct ChanInfo *cptr, struct Luser *lptr)

{
  struct NickInfo *nptr;

  if (!cptr || !lptr)
    return 0;

  if (cptr->flags & CS_FORBID)
    return 0;

  if (IsContact(lptr, cptr))
    return (cptr->access_lvl[CA_CONTACT]);

  if ((nptr = FindNick(lptr->nick)))
  {
    /*
     * If nptr has not yet identified, don't give them access
     */
    if (!(nptr->flags & NS_IDENTIFIED))
    {
      if (cptr->flags & CS_SECURE)
      {
        /*
         * If the channel is secured, they MUST be
         * identified to get access - return 0
         */
        return (0);
      }

      if (!AllowAccessIfSOp)
        nptr = NULL;
      else if (!IsChannelOp(FindChannel(cptr->name), lptr))
        nptr = NULL;
      else
      {
        /*
         * They're opped on the channel, and AllowAccessIfSOp
         * is enabled, so allow them access. This will only
         * work during a netjoin, so ChanServ doesn't wrongfully
         * deop whole channels. If a channel op tries to
         * use this feature to get access without identifying,
         * cs_process() will stop them cold.
         */
      }
    }
  }
  else if (cptr->flags & CS_SECURE)
  {
    /*
     * The channel is secured, and they do not have a registered
     * nickname - return 0
     */
    return (0);
  }

  return GetAccessDirect(cptr,lptr,nptr);
} /* GetAccess() */

/*
HasAccess()
  Determine if lptr->nick has level on 'cptr'
*/

int
HasAccess(struct ChanInfo *cptr, struct Luser *lptr, int level)

{
  int rc;
#ifdef EMPOWERADMINS_MORE
  int resetosregistered = 0;
#endif /* EMPOWERADMINS_MORE */

  if (!cptr || !lptr)
    return 0;

  if (cptr->flags & CS_FORBID)
    return 0;

  if (cptr->flags & CS_SECURE)
  {
    struct NickInfo *nptr;

    if ((nptr = FindNick(lptr->nick)))
    {
      if (!(nptr->flags & NS_IDENTIFIED))
        {
          return 0;
        }
    }
    else
      return 0;
  }

#ifdef EMPOWERADMINS_MORE
  /*
   * If AutoOpAdmins is disabled, check if lptr is an admin and if level
   * is AUTOOP (or AUTOVOICE) - if so, check if they are on the channel's
   * access list for the appropriate level; if they are, they are allowed
   * to get opped/voiced (return 1), otherwise return 0. This extra check
   * is needed because GetAccess() will return true if lptr is an admin,
   * regardless of AutoOpAdmins being enabled.
   */
  /* Changed to temporarily force L_OSREGISTERED off -- jilles */
  if (!AutoOpAdmins && IsValidAdmin(lptr) && ((level == CA_AUTOOP) ||
        (level == CA_AUTOVOICE)))
  {
    if (lptr->flags & L_OSREGISTERED)
    {
      lptr->flags &= ~L_OSREGISTERED;
      resetosregistered = 1;
    }
  }
#endif /* EMPOWERADMINS_MORE */

  rc = GetAccess(cptr, lptr) >= cptr->access_lvl[level];

#ifdef EMPOWERADMINS_MORE
  if (resetosregistered)
    lptr->flags |= L_OSREGISTERED;
#endif /* EMPOWERADMINS_MORE */

  return rc;
} /* HasAccess() */

/*
 * OnAccessList()
 * Return 1 if 'hostmask' is on cptr's access list
 *
 * This is used for access list changes only, so only accept exact
 * matches for hostmasks. For nicks, assume nptr is a master nick.
 */
static struct ChanAccess *OnAccessList(struct ChanInfo *cptr, char
    *hostmask, struct NickInfo *nptr)
{
  struct ChanAccess *ca;

  for (ca = cptr->access; ca; ca = ca->next)
  {
    if (nptr && ca->nptr)
      if (nptr == ca->nptr)
        return(ca);

    if (hostmask && ca->hostmask)
      if (!irccmp(ca->hostmask, hostmask))
        return(ca);
  }

  return(NULL);
} /* OnAccessList() */

/*
OnAkickList()
  Return 1 if 'hostmask' is on cptr's autoremove list
*/

static struct AutoKick *
OnAkickList(struct ChanInfo *cptr, char *hostmask)

{
  struct AutoKick *ak;

  for (ak = cptr->akick; ak; ak = ak->next)
    if (match(ak->hostmask, hostmask))
      return (ak);

  return (NULL);
} /* OnAkickList() */

/*
c_help()
  Give lptr->nick help on av[1]
*/

static void
c_help(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  if (ac >= 2)
  {
    char str[MAXLINE];

    if (ac >= 3)
      ircsprintf(str, "%s %s", av[1], av[2]);
    else
    {
      if ((!irccmp(av[1], "ACCESS")) ||
          (!irccmp(av[1], "AKICK")) ||
          (!irccmp(av[1], "AUTOREM")) ||
          (!irccmp(av[1], "LEVEL")) ||
          (!irccmp(av[1], "SET")))
        ircsprintf(str, "%s index", av[1]);
      else
      {
        struct Command *cptr;

        for (cptr = chancmds; cptr->cmd; cptr++)
          if (!irccmp(av[1], cptr->cmd))
            break;

        if (cptr->cmd)
          if ((cptr->level == LVL_ADMIN) &&
              !(IsValidAdmin(lptr)))
          {
            notice(n_ChanServ, lptr->nick,
              "No help available on \002%s\002",
              av[1]);
            return;
          }

        ircsprintf(str, "%s", av[1]);
      }
    }

    GiveHelp(n_ChanServ, lptr->nick, str, NODCC);
  }
  else
    GiveHelp(n_ChanServ, lptr->nick, NULL, NODCC);

  return;
} /* c_help() */

/*
c_register()
  Register channel av[1] with password av[2]
*/

static void
c_register(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Userlist *userptr;
  struct NickInfo *master;
  struct Channel *chptr;
  char *modes;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002REGISTER <#channel> [password]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "REGISTER");
    return;
  }

  if( checkforproc( av[1]) ) {
   notice(n_ChanServ, lptr->nick,
      "Invalid channel name");
   return;
  }

  if (HasFlag(lptr->nick, NS_NOREGISTER))
  {
    notice(n_ChanServ, lptr->nick,
      "You are not permitted to register channels");
    putlog(LOG1,
      "Flagged user %s!%s@%s attempted to register channel [%s]",
      lptr->nick,
      lptr->username,
      lptr->hostname,
      av[1]);
    return;
  }

  userptr = GetUser(1, lptr->nick, lptr->username, lptr->hostname);
  if (RestrictRegister)
  {
    if (!(lptr->flags & L_OSREGISTERED) || !IsOper(userptr))
    {
      notice(n_ChanServ, lptr->nick,
        "Use of the [\002REGISTER\002] command is restricted");
      RecordCommand("%s: %s!%s@%s failed REGISTER [%s]",
        n_ChanServ,
        lptr->nick,
        lptr->username,
        lptr->hostname,
        av[1]);
      return;
    }
  }

  master = GetMaster(nptr);
  if (!IsValidAdmin(lptr) && MaxChansPerUser &&
      (master->fccnt >= MaxChansPerUser))
  {
    notice(n_ChanServ, lptr->nick,
      "You are only allowed to register [\002%d\002] channels",
      MaxChansPerUser);
    return;
  }

  if ((cptr = FindChan(av[1])))
  {
    if (cptr->flags & CS_FORBID)
      RecordCommand("%s: Attempt to register forbidden channel [%s] by %s!%s@%s",
        n_ChanServ,
        cptr->name,
        lptr->nick,
        lptr->username,
        lptr->hostname);

    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is already registered",
      av[1]);
    return;
  }

  chptr = FindChannel(av[1]);
  if (chptr == NULL)
  {
    notice(n_ChanServ, lptr->nick,
      "Channel [\002%s\002] does not exist",
      av[1]);
    return;
  }

  if (!IsChannelOp(chptr, lptr) && !IsValidAdmin(lptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You are not a channel operator on [\002%s\002]",
      av[1]);
    return;
  }

  cptr = MakeChan();

  if (!ChangeChanPass(cptr, ac > 2 ? av[2] : NULL))
  {
    notice(n_ChanServ, lptr->nick,
      "Register failed");
    RecordCommand("%s: failed to create password for registered channel [%s]",
      n_ChanServ,
      av[1]);
    MyFree(cptr);
    return;
  }

  cptr->name = MyStrdup(av[1]);
  cptr->contact = MyStrdup(master->nick);
  cptr->created = cptr->lastused = cptr->last_contact_active = current_ts;
  SetDefaultALVL(cptr);
  /* Let's not have SECURE and SECUREOPS on by default */
  /* cptr->flags |= CS_SECURE;
  cptr->flags |= CS_SECUREOPS; */

  /*
   * If the channel is registered by an operator or higher,
   * make the channel NoExpire
   */
  /* if ((lptr->flags & L_OSREGISTERED) && IsOper(userptr))
    cptr->flags |= CS_NOEXPIRE; */

  AddAccess(cptr, 0, 0, nptr, cptr->access_lvl[CA_ACCESS], current_ts, current_ts);

  /* add cptr to channel list */
  AddChan(cptr);

  /* add channel to the master's contact channel list */
  AddContactChannelToNick(&master, cptr);

  /* give lptr contact access */
  AddContact(lptr, cptr);

  /*
   * Allow this call to cs_join even if HYBRID_ONLY is defined
   * so we can call cs_CheckChan() on it
   */
  cs_join(cptr);

  /*
   * Let's mlock -s+cnt registered channels by default.
   */

  modes = "-s+cnt";
  cptr->modes_off |= MODE_S;
  cptr->modes_on |= MODE_C;
  cptr->modes_on |= MODE_N;
  cptr->modes_on |= MODE_T;

  toserv(":%s MODE %s %s %s\n",
         n_ChanServ,
         cptr->name,
         modes,
         ((cptr->modes_off & MODE_K) && (chptr->key)) ? chptr->key : "");
  UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);

  notice(n_ChanServ, lptr->nick,
    "The channel [\002%s\002] is now registered under your nickname.",
    av[1]);
  if (ac > 2)
    notice(n_ChanServ, lptr->nick,
      "Your channel password is [\002%s\002].  Please remember it for later use.",
      av[2]);
  else
    notice(n_ChanServ, lptr->nick,
      "There is no channel password.  Only your nickname can use /msg %s IDENTIFY %s.",
      n_ChanServ, av[1]);
#ifdef FREENODE
  notice(n_ChanServ, lptr->nick,
    "Channel guidelines can be found on the freenode website");
  notice(n_ChanServ, lptr->nick,
    "(http://freenode.net/channel_guidelines.shtml).");
  notice(n_ChanServ, lptr->nick,
    "Freenode is a service of Peer-Directed Projects Center, an IRS 501(c)(3)");
  notice(n_ChanServ, lptr->nick,
    "(tax-exempt) charitable and educational organization.");
#endif

  RecordCommand("%s: %s!%s@%s REGISTER [%s]",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1]);
} /* c_register() */

/*
c_drop()
  Drop registered channel av[1]
*/

static void
c_drop(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Channel *chptr;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002DROP <channel> [password]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "DROP");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (ac >= 3)
  {
    if (!pwmatch(cptr->password, av[2]))
    {
      notice(n_ChanServ, lptr->nick,
        ERR_BAD_PASS);

      RecordCommand("%s: %s!%s@%s failed DROP [%s]",
        n_ChanServ,
        lptr->nick,
        lptr->username,
        lptr->hostname,
        cptr->name);

      return;
    }
  }
  else if (!(IsContact(lptr, cptr)
#ifdef EMPOWERADMINS
  /* We want empowered (not empoweredmore) admins to be able to drop
   * forbidden and forgotten channels, too. -kre */
    || (IsValidAdmin(lptr) && cptr->flags & CS_FORBID)
#endif /* EMPOWERADMINS */
    ))
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002DROP <channel> <password>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "DROP");
    return;
  }

  RecordCommand("%s: %s!%s@%s DROP [%s]",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name);

  chptr = FindChannel(cptr->name);

  if (IsChannelMember(chptr, Me.csptr))
    cs_part(chptr);

  /* everything checks out ok...delete the channel */
  DeleteChan(cptr);

  notice(n_ChanServ, lptr->nick,
    "The channel [\002%s\002] has been dropped",
    av[1]);
} /* c_drop() */

/*
c_access()
  Modify the channel access list for av[1]
*/

static void
c_access(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Command *cmdptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002ACCESS <channel> {ADD|DEL|LIST} [mask [level]]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "ACCESS");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  cmdptr = GetCommand(accesscmds, av[2]);

  if (!cmdptr || (cmdptr == (struct Command *) -1))
  {
    /* the option they gave was not valid */
    notice(n_ChanServ, lptr->nick,
      "%s switch [\002%s\002]",
      (cmdptr == (struct Command *) -1) ? "Ambiguous" : "Unknown",
      av[2]);
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "ACCESS");
    return;
  }

  if (!nptr && (cmdptr->level != LVL_NONE))
  {
    /* the command requires a registered nickname */

    notice(n_ChanServ, lptr->nick,
      "Your nickname is not registered");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_NickServ,
      "REGISTER");
    return;
  }

  if (nptr)
  {
    if (nptr->flags & NS_FORBID)
    {
      notice(n_ChanServ, lptr->nick,
        "Cannot execute commands for forbidden nicknames");
      return;
    }

    if (cmdptr->level != LVL_NONE)
    {
      if (!(nptr->flags & NS_IDENTIFIED))
      {
        notice(n_ChanServ, lptr->nick,
          "Password identification is required for [\002ACCESS %s\002]",
          cmdptr->cmd);
        notice(n_ChanServ, lptr->nick,
          "Type \002/msg %s IDENTIFY <password>\002 and retry",
          n_NickServ);
        return;
      }
    }
  } /* if (nptr) */

  /* call cptr->func to execute command */
  (*cmdptr->func)(lptr, nptr, ac, av);

  return;
} /* c_access() */

static void
c_access_add(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char hostmask[MAXLINE];
  struct NickInfo *nickptr;
  int level; /* lptr->nick's access level */
  int newlevel;

  if (!(cptr = FindChan(av[1])))
    return;

  if (!HasAccess(cptr, lptr, CA_ACCESS))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_ACCESS],
      "ACCESS ADD",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed ACCESS [%s] ADD %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac >= 4) ? av[3] : "",
      (ac >= 5) ? av[4] : "");
    return;
  }  

  if (ac < 5)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002ACCESS <channel> ADD <hostmask> <level>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "ACCESS ADD");
    return;
  }

  if (atoi(av[4]) < (-DefaultAccess[CA_CONTACT]))
  {
     notice(n_ChanServ, lptr->nick,
       "You cannot add an access level lower than [\002-%d\002]",
          DefaultAccess[CA_CONTACT]);
     return;
  }

  if (((level = GetAccess(cptr, lptr)) <= atoi(av[4])) ||
      (atoi(av[4]) >= cptr->access_lvl[CA_CONTACT]))
  {
    notice(n_ChanServ, lptr->nick,
      "You cannot add an access level greater than [\002%d\002]",
      level - 1);
    RecordCommand("%s: %s!%s@%s failed ACCESS [%s] ADD %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      av[3],
      av[4]);
    return;
  }

  nickptr = NULL;
  hostmask[0] = '\0';
  if (match("*!*@*", av[3]))
    strcpy(hostmask, av[3]);
  else if (match("*!*", av[3]))
  {
    strcpy(hostmask, av[3]);
    strcat(hostmask, "@*");
  }
  else if (match("*@*", av[3]))
  {
    strcpy(hostmask, "*!");
    strcat(hostmask, av[3]);
  }
  else if (match("*.*", av[3]))
  {
    strcpy(hostmask, "*!*@");
    strcat(hostmask, av[3]);
  }
  else
  {
    /* it must be a nickname */
    if (!(nickptr = FindNick(av[3])))
    {
      /* Let's require explicit nick!*@* if they really want that -- jilles */
      notice(n_ChanServ, lptr->nick,
	"Nick [\002%s\002] is not registered",
	av[3]);
      return;
    }
  }

  newlevel = atoi(av[4]);

  /* add hostmask (or nickptr) to the access list */
  if (AddAccess(cptr, lptr, hostmask, nickptr, newlevel, current_ts, 0))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] has been added to the access list "
      "for %s with level [\002%d\002]",
      nickptr ? nickptr->nick : hostmask,
      cptr->name,
      newlevel);

    RecordCommand("%s: %s!%s@%s ACCESS [%s] ADD %s %d",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
      nickptr ? nickptr->nick : hostmask ? hostmask : "unknown!",
      newlevel);

    if (cptr->flags & CS_VERBOSE)
    {
      notice(n_ChanServ, cptr->name, "%s!%s@%s ACCESS [%s] ADD %s %d",
        lptr->nick, lptr->username, lptr->hostname, cptr->name,
        nickptr ? nickptr->nick : hostmask ? hostmask : "unknown!",
        newlevel);
    }

     /* Notify user -KrisDuv 
        I've added identification check -kre */
     if (nickptr && (nickptr->flags & NS_IDENTIFIED))
     {
       struct Channel *chptr = FindChannel(cptr->name);
 
       notice(n_ChanServ, nickptr->nick,
        "You have been added to the access list for %s "
        "with level [\002%d\002]", cptr->name, newlevel);

       /* autoop him if newlevel >= CA_AUTOOP */
       if (chptr)
       {
         struct Luser *luptr = FindClient(nickptr->nick);

         if (!IsChannelOp(chptr, luptr) && 
            (newlevel >= cptr->access_lvl[CA_AUTOOP]))
         {
           char modes[MAXLINE];
           ircsprintf(modes, "+o %s", nickptr->nick);
           toserv(":%s MODE %s %s\n", n_ChanServ, cptr->name, modes);
           UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
         }
       }
     }
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "You may not modify the access entry [\002%s\002]",
      nickptr ? nickptr->nick : hostmask);

    RecordCommand("%s: %s!%s@%s failed ACCESS [%s] ADD %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      av[3],
      av[4]);
  }
} /* c_access_add() */

static void
c_access_del(struct Luser *lptr, struct NickInfo *nptr,
             int ac, char **av)

{
  struct ChanInfo *cptr;
  struct ChanAccess *temp;
  char *host;
  int cnt, idx;
  struct NickInfo *nickptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (!HasAccess(cptr, lptr, CA_ACCESS))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_ACCESS],
      "ACCESS DEL",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed ACCESS [%s] DEL %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac >= 4) ? av[3] : "");
    return;
  }  

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002ACCESS <channel> DEL <hostmask | index>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "ACCESS DEL");
    return;
  }

  host = NULL;
  nickptr = NULL;

  if ((idx = IsNum(av[3])))
  {
    cnt = 0;
    for (temp = cptr->access; temp; temp = temp->next, ++cnt)
    {
      if (idx == (cnt + 1))
      {
        if (temp->hostmask)
          host = MyStrdup(temp->hostmask);
        else
          nickptr = temp->nptr;

        break;
      }
    }

    if (!host && !nickptr)
    {
      notice(n_ChanServ, lptr->nick,
        "[\002%d\002] is not a valid index",
        idx);

      notice(n_ChanServ, lptr->nick,
        ERR_MORE_INFO,
        n_ChanServ,
        "ACCESS LIST");

      return;
    }
  }
  else if (!(nickptr = FindNick(av[3])))
    host = MyStrdup(av[3]);

  if ((cnt = DelAccess(cptr, lptr, host, nickptr)) > 0)
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] has been removed from the access list for %s",
      nickptr ? nickptr->nick : host,
      cptr->name);

    RecordCommand("%s: %s!%s@%s ACCESS [%s] DEL %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      nickptr ? nickptr->nick : host);
  
     if (cptr->flags & CS_VERBOSE)
     {
       notice(n_ChanServ, cptr->name, "%s!%s@%s ACCESS [%s] DEL %s",
	 lptr->nick, lptr->username, lptr->hostname, cptr->name,
         nickptr ? nickptr->nick : host);
     }

     /* Notify user if channel is verbose and user is identified -kre */
     /* dropped verbose check -- jilles */
     if (nickptr && (nickptr->flags & NS_IDENTIFIED))
     {
       notice(n_ChanServ, nickptr->nick,
        "You have been deleted from the access list for [\002%s\002]",
        cptr->name);
     }
  }
  else
  {
    if (cnt == (-1))
    {
      notice(n_ChanServ, lptr->nick,
        "[\002%s\002] matches an access level higher than your own",
        nickptr ? nickptr->nick : host);

      RecordCommand("%s: %s!%s@%s failed ACCESS [%s] DEL %s",
        n_ChanServ,
        lptr->nick,
        lptr->username,
        lptr->hostname,
        cptr->name,
        nickptr ? nickptr->nick : host);
    }
    else
    {
      notice(n_ChanServ, lptr->nick,
        "[\002%s\002] was not found on the access list for %s",
        nickptr ? nickptr->nick : host,
        cptr->name);

      if (host)
        MyFree(host);

      return;
    }
  }

  if (host)
    MyFree(host);
} /* c_access_del() */

static void
c_access_list(struct Luser *lptr, struct NickInfo *nptr,
              int ac, char **av)

{
  struct ChanInfo *cptr;
  struct ChanAccess *ca;
  int idx;
  char *mask = NULL;
  char creation[80];
  int full = 0;

  if (ac >= 4)
  {
    if (!irccmp(av[3], "FULL"))
      full = 1;
    else
      mask = av[3];
  }

  if (!(cptr = FindChan(av[1])))
    return;

  if (cptr->flags & CS_PRIVATE)
  {
    if (!nptr)
    {
      /* the command requires a registered nickname */

      notice(n_ChanServ, lptr->nick,
        "Your nickname is not registered");
      notice(n_ChanServ, lptr->nick,
        "and the channel [\002%s\002] is private",
        cptr->name);
      notice(n_ChanServ, lptr->nick,
        ERR_MORE_INFO,
        n_NickServ,
        "REGISTER");
      return;
    }

    if (!(nptr->flags & NS_IDENTIFIED))
    {
      notice(n_ChanServ, lptr->nick,
        "Password identification is required for [\002ACCESS LIST\002]");
      notice(n_ChanServ, lptr->nick,
        "Type \002/msg %s IDENTIFY <password>\002 and retry",
        n_NickServ);
      return;
    }
  }

  if (((full || cptr->flags & CS_PRIVATE) && GetAccess(cptr, lptr) <= 0) || GetAccess(cptr, lptr) < 0 )
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      full || cptr->flags & CS_PRIVATE ? 1 : 0,
      full ? "ACCESS LIST FULL" : "ACCESS LIST",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed ACCESS [%s] LIST %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac >= 4) ? av[3] : "");
    return;
  }  

  RecordCommand("%s: %s!%s@%s ACCESS [%s] LIST %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    mask ? mask : "");

  if (cptr->access)
  {
    notice(n_ChanServ, lptr->nick,
      "-- Access List for [\002%s\002] --",
      cptr->name);
    notice(n_ChanServ, lptr->nick,
      "Num Level Hostmask                            Time since last use%s", full ? " Time since addition" : "");
    notice(n_ChanServ, lptr->nick,
      "--- ----- --------                            -------------------%s", full ? " -------------------" : "");
    idx = 1;
    for (ca = cptr->access; ca; ca = ca->next, idx++)
    {
      if (mask)
        if (match(mask, ca->hostmask ? ca->hostmask : ca->nptr->nick) == 0)
          continue;
      strcpy(creation, timeago(ca->created, 0));
      notice(n_ChanServ, lptr->nick,
	     "%-3d %-5d %-35s %-19s %-19s",
	     idx,
	     ca->level,
	     ca->hostmask ? ca->hostmask : (ca->nptr ? ca->nptr->nick : ""),
	     ca->last_used ? timeago(ca->last_used, 0) : "<never used>       ",
	     full && ca->created ? creation : "" /*"                   "*/);
    }
    notice(n_ChanServ, lptr->nick,
      "-- End of list --");
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "The access list for [\002%s\002] is empty",
      cptr->name);
  }
} /* c_access_list() */

/*
c_akick()
  Modify the AUTOREM list for channel av[1]
*/

static void
c_akick(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Command *cmdptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002AUTOREM <channel> {ADD|DEL|LIST} [mask]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "AKICK");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_AKICK))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_AKICK],
      "AKICK",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed AKICK [%s] %s %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      StrToupper(av[2]),
      (ac >= 4) ? av[3] : "",
      (ac >= 5) ? av[4] : "");
    return;
  }

  cmdptr = GetCommand(akickcmds, av[2]);

  if (cmdptr && (cmdptr != (struct Command *) -1))
  {
    /* call cmdptr->func to execute command */
    (*cmdptr->func)(lptr, nptr, ac, av);
  }
  else
  {
    /* the option they gave was not valid */
    notice(n_ChanServ, lptr->nick,
      "%s switch [\002%s\002]",
      (cmdptr == (struct Command *) -1) ? "Ambiguous" : "Unknown",
      av[2]);
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "AKICK");
    return;
  }

  return;
} /* c_akick() */

static void
c_akick_add(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char hostmask[MAXLINE];
  char *reason;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002AUTOREM <channel> ADD <hostmask> [reason]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "AKICK ADD");
    return;
  }

  if (MaxAkicks && (cptr->akickcnt >= MaxAkicks))
  {
    notice(n_ChanServ, lptr->nick,
      "Maximum autokick count for [\002%s\002] has been reached",
      cptr->name);
    return;
  }

  if (match("*!*@*", av[3]))
    strcpy(hostmask, av[3]);
  else if (match("*!*", av[3]))
  {
    strcpy(hostmask, av[3]);
    strcat(hostmask, "@*");
  }
  else if (match("*@*", av[3]))
  { 
    strcpy(hostmask, "*!");
    strcat(hostmask, av[3]);
  }
  else if (match("*.*", av[3]))
  {
    strcpy(hostmask, "*!*@");
    strcat(hostmask, av[3]);
  }
  else
  {
    struct Luser *ptr;
    char *mask;

    /* it must be a nickname - try to get hostmask */
    if ((ptr = FindClient(av[3])))
    {
      mask = HostToMask(ptr->username, ptr->hostname);
      ircsprintf(hostmask, "*!%s", mask);
      MyFree(mask);
    }
    else
    {
      strcpy(hostmask, av[3]);
      strcat(hostmask, "!*@*");
    }
  } 

  /* forwarding bans are useless in the current scheme and wouldn't match
   * anyway -- jilles */
  if (match("*!*@*!*", hostmask))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] looks like a forwarding ban which is not supported in AutoRemove lists",
        hostmask);
    return;
  }

  /* avoid ** as that would desync services and ircd ban lists -- jilles */
  collapse(hostmask);

  if (OnAkickList(cptr, hostmask))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] matches a hostmask already on the AutoRemove list for %s",
        hostmask,
        cptr->name);
    return;
  }

  if (ac < 5)
    reason = NULL;
  else
    reason = GetString(ac - 4, av + 4);

  /* add hostmask to the akick list */
  if (AddAkick(cptr, lptr, hostmask, reason))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] has been added to the autoremove "
      "list for %s with reason [%s]",
      hostmask, cptr->name, reason ? reason : "");

    RecordCommand("%s: %s!%s@%s AKICK [%s] ADD %s %s",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
      hostmask, reason ? reason : "");
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "You may not add [\002%s\002] to the autoremove list for %s",
      hostmask,
      cptr->name);

    RecordCommand("%s: %s!%s@%s failed AKICK [%s] ADD %s %s",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
      av[3],
      reason ? reason : "");

    MyFree(reason);
    return;
  }

  MyFree(reason);
} /* c_akick_add() */

static void
c_akick_del(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct AutoKick *temp;
  char *host = NULL;
  int cnt, idx;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002AUTOREM <channel> DEL <hostmask | index>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "AKICK DEL");
    return;
  }

  if ((idx = IsNum(av[3])))
  {
    cnt = 0;
    for (temp = cptr->akick; temp; temp = temp->next, cnt++)
      if (idx == (cnt + 1))
      {
        host = MyStrdup(temp->hostmask);
        break;
      }

    if (!host)
    {
      notice(n_ChanServ, lptr->nick,
        "[\002%d\002] is not a valid index",
        idx);
      notice(n_ChanServ, lptr->nick,
        ERR_MORE_INFO,
        n_ChanServ,
        "AKICK LIST");
      return;
    }
  }
  else
    host = MyStrdup(av[3]);

  /* just like in AUTOREM ADD -- jilles */
  collapse(host);

  if (!DelAkick(cptr, host))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] was not found on the autoremove list for %s",
      host,
      cptr->name);
    MyFree(host);
    return;
  }

  notice(n_ChanServ, lptr->nick,
    "[\002%s\002] has been removed from the autoremove list for %s",
    host,
    cptr->name);

  RecordCommand("%s: %s!%s@%s AKICK [%s] DEL %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    host);

  MyFree(host);
} /* c_akick_del() */

static void
c_akick_list(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo  *cptr;
  struct AutoKick *ak;
  int idx;
  char *mask = NULL;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac >= 4)
    mask = av[3];

  RecordCommand("%s: %s!%s@%s AKICK [%s] LIST %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    mask ? mask : "");

  if (cptr->akick)
  {
    notice(n_ChanServ, lptr->nick,
      "-- AutoRemove List for [\002%s\002] --",
      cptr->name);
    notice(n_ChanServ, lptr->nick,
      "Num %-35s Reason",
      "Hostmask");
    notice(n_ChanServ, lptr->nick,
      "--- %-35s ------",
      "--------");
    idx = 1;
    for (ak = cptr->akick; ak; ak = ak->next, idx++)
    {
      if (mask)
        if (match(mask, ak->hostmask) == 0)
          continue;
      notice(n_ChanServ, lptr->nick,
        "%-3d %-35s %s",
        idx,
        ak->hostmask,
        ak->reason ? ak->reason : "");
    }
    notice(n_ChanServ, lptr->nick,
      "-- End of list --");
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "The autoremove list for [\002%s\002] is empty",
      cptr->name);
  }
} /* c_akick_list() */

/*
c_list()
  Display list of channels matching av[1]
*/

static void
c_list(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *temp;
  int IsAnAdmin;
  int ii,
      mcnt, /* total matches found */
      acnt; /* total matches - private nicks */

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002LIST <pattern>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO, 
      n_ChanServ,
      "LIST");
    return;
  }

  RecordCommand("%s: %s!%s@%s LIST %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1]);

  if (IsValidAdmin(lptr))
    IsAnAdmin = 1;
  else
    IsAnAdmin = 0;

  acnt = mcnt = 0;
  notice(n_ChanServ, lptr->nick,
    "-- Listing channels matching [\002%s\002] --",
    av[1]);

  for (ii = 0; ii < CHANLIST_MAX; ++ii)
  {
    for (temp = chanlist[ii]; temp; temp = temp->next)
    {
      if (match(av[1], temp->name))
      {
        mcnt++;
        if ((IsAnAdmin) || (!(temp->flags & CS_PRIVATE) && (acnt < 100)))
        {
          char  str[20];

          acnt++;
          if (temp->flags & CS_FORBID)
            strcpy(str, "<< FORBIDDEN >>");
          else if (temp->flags & CS_PRIVATE)
            strcpy(str, "<< PRIVATE >>");
          else if (FindChannel(temp->name))
            strcpy(str, "<< ACTIVE >>");
          else
            str[0] = '\0';

          notice(n_ChanServ, lptr->nick, 
            "%-10s %15s created %s ago",
            temp->name,
            str,
            timeago(temp->created, 1));
        }
      }
    }
  }

  notice(n_ChanServ, lptr->nick, 
    "-- End of list (%d/%d matches shown) --",
    acnt,
    mcnt);
} /* c_list() */

/*
c_level()
  Change the level(s) for different privileges on channel av[1]
*/

static void
c_level(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002LEVEL <channel> {SET|RESET|LIST} [index|type [level]]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "LEVEL");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!irccmp(av[2], "LIST"))
  {
    int ii;

    notice(n_ChanServ, lptr->nick,
      "-- Access Levels for [\002%s\002] --",
      cptr->name);
    notice(n_ChanServ, lptr->nick,
      "Index Level %-9s Description",
      "Type");
    notice(n_ChanServ, lptr->nick,
      "----- ----- %-9s -----------",
      "----");
    for (ii = 0; ii < CA_SIZE; ++ii)
    {
      if (!accessinfo[ii].cmd)
	continue;
      if (cptr->access_lvl[ii] <= cptr->access_lvl[CA_CONTACT])
	notice(n_ChanServ, lptr->nick,
	       "%-5d %-5d %-9s %s",
	       ii + 1,
	       cptr->access_lvl[ii],
	       accessinfo[ii].cmd,
	       accessinfo[ii].desc);
      else
	notice(n_ChanServ, lptr->nick,
	       "%-5d %-5s %-9s %s",
	       ii + 1,
	       "OFF",
	       accessinfo[ii].cmd,
	       accessinfo[ii].desc);
    }
    notice(n_ChanServ, lptr->nick,
      "-- End of list --");
    return;
  }

  if (!IsContact(lptr, cptr) && !HasAccess(cptr, lptr, CA_LEVEL))
  {
    notice(n_ChanServ, lptr->nick,
      "Contact access is required for [\002LEVEL\002]");
    RecordCommand("%s: %s!%s@%s failed LEVEL [%s] %s %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      av[2],
      (ac >= 4) ? av[3] : "",
      (ac >= 5) ? av[4] : "");
    return;
  }  

  if (!irccmp(av[2], "SET"))
  {
    int    newlevel, index;

    if (ac < 5)
    {
      notice(n_ChanServ, lptr->nick,
        "Syntax: \002LEVEL <channel> SET <index|type> <level>\002");
      notice(n_ChanServ, lptr->nick,
        ERR_MORE_INFO,
        n_ChanServ,
        "LEVEL SET");
      return;
    }

    index = (-1);
    if (!IsNum(av[3]))
    {
      if (!irccmp(av[3], "AUTODEOP"))
        index = CA_AUTODEOP;
      else if (!irccmp(av[3], "AUTOVOICE"))
        index = CA_AUTOVOICE;
      else if (!irccmp(av[3], "CMDVOICE"))
        index = CA_CMDVOICE;
      else if (!irccmp(av[3], "ACCESS"))
        index = CA_ACCESS;
      else if (!irccmp(av[3], "CMDINVITE"))
        index = CA_CMDINVITE;
#ifdef HYBRID7
      /* Add setup for autohalfop and cmdhalfop -Janos */
      else if (!irccmp(av[3], "AUTOHALFOP"))
        index = CA_AUTOHALFOP;
      else if (!irccmp(av[3], "CMDHALFOP"))
        index = CA_CMDHALFOP;
#endif /* HYBRID7 */
      else if (!irccmp(av[3], "AUTOOP"))
        index = CA_AUTOOP;
      else if (!irccmp(av[3], "CMDOP"))
        index = CA_CMDOP;
      else if (!irccmp(av[3], "CMDUNBAN"))
        index = CA_CMDUNBAN;
      else if (!irccmp(av[3], "AUTOKICK"))
        index = CA_AKICK;
      else if (!irccmp(av[3], "CMDCLEAR"))
        index = CA_CMDCLEAR;
      else if (!irccmp(av[3], "SET"))
        index = CA_SET;
      else if (!irccmp(av[3], "TOPIC"))
        index = CA_TOPIC;
      else if (!irccmp(av[3], "LEVEL"))
        index = CA_LEVEL;

      if (index == (-1))
      {
        notice(n_ChanServ, lptr->nick,
          "Invalid type specified [\002%s\002]",
          av[3]);
        notice(n_ChanServ, lptr->nick,
          ERR_MORE_INFO,
          n_ChanServ,
          "LEVEL LIST");
        return;
      }
    }
    else
    {
      if( strlen(av[3]) > 5 ) {
        notice(n_ChanServ, lptr->nick,
          "Invalid entry, sorry, try again!");
        return;
      }
      index = atoi(av[3]) - 1;
      if (index < 0 )
      {
        notice(n_ChanServ, lptr->nick,
          "Invalid type specified [\002%s\002]",
          av[3]);
        return;
      }

      if ((index >= CA_SIZE) || !accessinfo[index].cmd)
      {
        notice(n_ChanServ, lptr->nick,
          "The index [\002%s\002] is not valid",
          av[3]);
        return;
      }
    }

    if (strlen(av[4]) > 5 )
    {
      notice(n_ChanServ, lptr->nick,
        "Invalid level, sorry, try again!");
      return;
    }

    if (!irccmp(av[4], "OFF"))
      {
	cptr->access_lvl[index] = newlevel = cptr->access_lvl[CA_CONTACT] + 1;
	notice(n_ChanServ, lptr->nick,
	       "The level required for [\002%s\002] has been changed to \002OFF\002 on %s",
	       accessinfo[index].cmd,
	       cptr->name);
      }
    else
      {
	newlevel = atoi(av[4]);
	if (newlevel >= cptr->access_lvl[CA_CONTACT])
	  {
	    notice(n_ChanServ, lptr->nick,
		   "You cannot create a level greater than [\002%d\002]",
		   cptr->access_lvl[CA_CONTACT]);
	    return;
	  }
	cptr->access_lvl[index] = newlevel;
	notice(n_ChanServ, lptr->nick,
	       "The level required for [\002%s\002] has been changed to \002%d\002 on %s",
	       accessinfo[index].cmd,
	       newlevel,
	       cptr->name);
      }

    RecordCommand("%s: %s!%s@%s LEVEL [%s] SET %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      av[3],
      av[4]);

    return;
  }

  if (!irccmp(av[2], "RESET"))
  {
    int index;

    if (ac < 4)
    {
      notice(n_ChanServ, lptr->nick,
        "Syntax: \002LEVEL <channel> RESET <index|type|ALL>\002");
      notice(n_ChanServ, lptr->nick,
        ERR_MORE_INFO,
        n_ChanServ,
        "LEVEL RESET");
      return;
    }

    index = (-1);
    if (!IsNum(av[3]))
    {
      if (!irccmp(av[3], "AUTODEOP"))
        index = CA_AUTODEOP;
      else if (!irccmp(av[3], "AUTOVOICE"))
        index = CA_AUTOVOICE;
      else if (!irccmp(av[3], "CMDVOICE"))
        index = CA_CMDVOICE;
      else if (!irccmp(av[3], "ACCESS"))
        index = CA_ACCESS;
      else if (!irccmp(av[3], "CMDINVITE"))
        index = CA_CMDINVITE;
      else if (!irccmp(av[3], "AUTOOP"))
        index = CA_AUTOOP;
#ifdef HYBRID7
      /* Add setup for autohalfop and cmdhalfop -Janos */
      else if (!irccmp(av[3], "AUTOHALFOP"))
        index = CA_AUTOHALFOP;
      else if (!irccmp(av[3], "CMDHALFOP"))
        index = CA_CMDHALFOP;
#endif /* HYBRID7 */
      else if (!irccmp(av[3], "CMDOP"))
        index = CA_CMDOP;
      else if (!irccmp(av[3], "CMDUNBAN"))
        index = CA_CMDUNBAN;
      else if (!irccmp(av[3], "AUTOKICK"))
        index = CA_AKICK;
      else if (!irccmp(av[3], "CMDCLEAR"))
        index = CA_CMDCLEAR;
      else if (!irccmp(av[3], "SET"))
        index = CA_SET;
      else if (!irccmp(av[3], "TOPIC"))
        index = CA_TOPIC;
      else if (!irccmp(av[3], "LEVEL"))
        index = CA_LEVEL;
      else if (!irccmp(av[3], "ALL"))
        index = (-2);

      if (index == (-1))
      {
        notice(n_ChanServ, lptr->nick,
          "Invalid type specified [\002%s\002]",
          av[3]);
        notice(n_ChanServ, lptr->nick,
          ERR_MORE_INFO,
          n_ChanServ,
          "LEVEL LIST");
        return;
      }
    }
    else
    {
      index = atoi(av[3]) - 1;
      if ((index >= CA_SIZE) || !accessinfo[index].cmd)
      {
        notice(n_ChanServ, lptr->nick,
          "The index [\002%s\002] is not valid",
          av[3]);
        return;
      }
    }

    if (index == (-2))
    {
      /* reset everything */
      SetDefaultALVL(cptr);
      notice(n_ChanServ, lptr->nick,
        "The access level list for %s has been reset",
        cptr->name);
    }
    else
    {
      cptr->access_lvl[index] = DefaultAccess[index];
      notice(n_ChanServ, lptr->nick,
        "The level required for [\002%s\002] has been reset to \002%d\002 on %s",
        accessinfo[index].cmd,
        cptr->access_lvl[index],
        cptr->name);
    }

    RecordCommand("%s: %s!%s@%s LEVEL [%s] RESET %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      av[3]);

    return;
  } /* if (!irccmp(av[2], "RESET")) */

  /* the option they gave was not a SET, RESET, or LIST */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002LEVEL <channel> {SET|RESET|LIST} [index|type [level]]\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "LEVEL");

  return;
} /* c_level() */

/*
c_identify()
  Check if lptr->nick supplied the correct contact password for av[1]
*/

static void
c_identify(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char nmask[MAXLINE];
  int isfounder = 0;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002IDENTIFY <channel> [password]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "IDENTIFY");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  /* Note: must be identified with nickserv to come here */
  isfounder = !irccmp(cptr->contact, nptr->master ? nptr->master->nick : nptr->nick);

  if (!isfounder && !pwmatch(cptr->password, ac < 3 ? "" : av[2]))
  {
    notice(n_ChanServ, lptr->nick, ERR_BAD_PASS);

    RecordCommand("%s: %s!%s@%s failed IDENTIFY [%s]",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      av[1]);

    return;
  }

  if (IsContact(lptr, cptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You already have contact access to [\002%s\002]",
      av[1]);
    return;
  }

  AddContact(lptr, cptr);

  notice(n_ChanServ, lptr->nick,
    "You are now recognized as a contact for [\002%s\002]",
    av[1]);

  ircsprintf(nmask, "%s!%s@%s", lptr->nick, lptr->username,
      lptr->hostname);

  RecordCommand("%s: %s!%s@%s IDENTIFY [%s] (%s)",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    isfounder ? "contact nick" : "password");
} /* c_identify() */

/*
c_set()
  Set various channel options
*/

static void
c_set(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Command *cmdptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> <option> [parameter]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_SET))
  {
    char tmp[MAXLINE];

    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_SET],
      "SET",
      av[1]);

    /* static buffers .. argh */
    strncpy(tmp, StrToupper(av[2]), sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    RecordCommand("%s: %s!%s@%s failed SET [%s] %s %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      tmp,
      (ac < 4) ? "" : StrToupper(av[3]));

    return;
  }

  cmdptr = GetCommand(setcmds, av[2]);

  if (cmdptr && (cmdptr != (struct Command *) -1))
  {
    /* call cmdptr->func to execute command */
    (*cmdptr->func)(lptr, nptr, ac, av);
  }
  else
  {
    /* the option they gave was not valid */
    notice(n_ChanServ, lptr->nick,
      "%s switch [\002%s\002]",
      (cmdptr == (struct Command *) -1) ? "Ambiguous" : "Unknown",
      av[2]);
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET");
    return;
  }
} /* c_set() */

static void
c_set_topiclock(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] TOPICLOCK %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Topic Lock for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_TOPICLOCK) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_TOPICLOCK;
    notice(n_ChanServ, lptr->nick,
      "Toggled Topic Lock for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_TOPICLOCK;
    notice(n_ChanServ, lptr->nick,
      "Toggled Topic Lock for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> TOPICLOCK {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET TOPICLOCK");
} /* c_set_topiclock() */

static void
c_set_private(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] PRIVATE %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Privacy for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_PRIVATE) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_PRIVATE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Privacy for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_PRIVATE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Privacy for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> PRIVATE {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET PRIVATE");
} /* c_set_private() */

/*
 * Set verbose mode on channel - code from IrcBg, slightly modified. -kre
 */
static void c_set_verbose(struct Luser *lptr, struct NickInfo *nptr, int
    ac, char **av)
{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] VERBOSE %s",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Verbose for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_VERBOSE) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    notice(n_ChanServ, cptr->name, "%s!%s@%s enabled notices of %s access list changes",
      lptr->nick, lptr->username, lptr->hostname, cptr->name);
    cptr->flags |= CS_VERBOSE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Verbose for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    notice(n_ChanServ, cptr->name, "%s!%s@%s disabled notices of %s access list changes",
      lptr->nick, lptr->username, lptr->hostname, cptr->name);
    cptr->flags &= ~CS_VERBOSE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Verbose for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> VERBOSE {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO, n_ChanServ, "SET VERBOSE");

} /* c_set_verbose() */

static void
c_set_secure(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] SECURE %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Security for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_SECURE) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_SECURE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Security for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_SECURE;
    notice(n_ChanServ, lptr->nick,
      "Toggled Security for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> SECURE {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET SECURE");
} /* c_set_secure() */

static void
c_set_secureops(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] SECUREOPS %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "SecureOps for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_SECUREOPS) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_SECUREOPS;
    notice(n_ChanServ, lptr->nick,
      "Toggled SecureOps for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_SECUREOPS;
    notice(n_ChanServ, lptr->nick,
      "Toggled SecureOps for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> SECUREOPS {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET SECUREOPS");
} /* c_set_secureops() */

static void
c_set_splitops(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] SPLITOPS %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "SplitOps for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_SPLITOPS) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_SPLITOPS;
    notice(n_ChanServ, lptr->nick,
      "Toggled SplitOps for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_SPLITOPS;
    notice(n_ChanServ, lptr->nick,
      "Toggled SplitOps for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> SPLITOPS {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET SPLITOPS");
} /* c_set_splitops() */

/* This needs rewriting to be a sane feature */
#if 0
static void
c_set_restricted(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)
{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] RESTRICTED %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Restricted Access for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_RESTRICTED) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_RESTRICTED;
    notice(n_ChanServ, lptr->nick,
      "Toggled Restricted Access for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_RESTRICTED;
    notice(n_ChanServ, lptr->nick,
      "Toggled Restricted Access for channel %s [\002OFF\002]",
      cptr->name);

    /* Leave channel if there is no need to stay -kre */
    if (!cs_ShouldBeOnChan(cptr))
      cs_part(FindChannel(cptr->name)); /* leave the channel */

    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> RESTRICTED {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET RESTRICTED");
} /* c_set_restricted() */
#endif

static void
c_set_guard(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  RecordCommand("%s: %s!%s@%s SET [%s] GUARD %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : StrToupper(av[3]));

  if (!AllowGuardChannel)
  {
    notice(n_ChanServ, lptr->nick,
      "ChanGuard option is currently \002disabled\002");
    return;
  }

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "ChanGuard for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_GUARD) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[3], "ON"))
  {
    cptr->flags |= CS_GUARD;
    notice(n_ChanServ, lptr->nick,
      "Toggled ChanGuard for channel %s [\002ON\002]",
      cptr->name);
    cs_join(cptr);
    return;
  }

  if (!irccmp(av[3], "OFF"))
  {
    cptr->flags &= ~CS_GUARD;
    notice(n_ChanServ, lptr->nick,
      "Toggled ChanGuard for channel %s [\002OFF\002]",
      cptr->name);
    cs_part(FindChannel(cptr->name));
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002SET <channel> GUARD {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "SET GUARD");
} /* c_set_guard() */

static void
c_set_password(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (!IsContact(lptr, cptr))
  {
    notice(n_ChanServ, lptr->nick,
      "Only contacts can change the channel password");
    RecordCommand("%s: %s!%s@%s failed SET [%s] PASSWORD%s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      ac >= 4 ? " ..." : "");
    return;
  }

  RecordCommand("%s: %s!%s@%s SET [%s] PASSWORD%s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    ac >= 4 ? " ..." : "");

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> PASSWORD [newpass]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET PASSWORD");
    return;
  }

  if (!ChangeChanPass(cptr, ac > 3 ? av[3] : NULL))
  {
    notice(n_ChanServ, lptr->nick,
      "Password change failed");
  }
  else if (ac > 3)
    notice(n_ChanServ, lptr->nick,
      "The password for %s has been changed to [\002%s\002]",
      cptr->name,
      av[3]);
  else
    notice(n_ChanServ, lptr->nick,
      "The password for %s has been removed. Only %s can use /msg %s IDENTIFY %s",
      cptr->name,
      cptr->contact,
      n_ChanServ,
      cptr->name);
} /* c_set_password() */

static void
c_set_contact(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct NickInfo *fptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (!IsContact(lptr, cptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You must IDENTIFY as a contact before resetting the contact nickname");
    RecordCommand("%s: %s!%s@%s failed SET [%s] CONTACT %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac < 4) ? "" : av[3]);

    return;
  }

  RecordCommand("%s: %s!%s@%s SET [%s] CONTACT %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : av[3]);

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> CONTACT <nickname>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET CONTACT");
    return;
  }

  if (!(fptr = GetLink(av[3])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NOT_REGGED,
      av[3]);
    return;
  }

  RemoveContactChannelFromNick(&fptr, cptr);

  if (cptr->contact)
  {
    struct NickInfo *oldnick = GetLink(cptr->contact);

    if (oldnick)
      RemoveContactChannelFromNick(&oldnick, cptr);

    MyFree(cptr->contact);
  }

  AddContactChannelToNick(&fptr, cptr);

  cptr->contact = MyStrdup(fptr->nick);

  notice(n_ChanServ, lptr->nick,
    "The contact nickname for %s has been changed to [\002%s\002]",
    cptr->name,
    fptr->nick);
} /* c_set_contact() */

static void
c_set_alternate(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct NickInfo *fptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (!IsContact(lptr, cptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You must IDENTIFY as a contact before setting the alternate contact nickname");
    RecordCommand("%s: %s!%s@%s failed SET [%s] ALTERNATE %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac < 4) ? "" : av[3]);
    return;
  }

  RecordCommand("%s: %s!%s@%s SET [%s] ALTERNATE %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (ac < 4) ? "" : av[3]);

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> ALTERNATE <nickname|->\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET ALTERNATE");
    return;
  }

  if (strcmp(av[3], "-"))
  {
    if (!(fptr = GetMaster(FindNick(av[3]))))
    {
      notice(n_ChanServ, lptr->nick,
        ERR_NOT_REGGED,
        av[3]);
      return;
    }

    if (fptr == GetMaster(nptr))
    {
      notice(n_ChanServ, lptr->nick,
        "You cannot set the alternate contact nickname to yourself!");
      return;
    }

    if (cptr->alternate)
      MyFree(cptr->alternate);

    cptr->alternate = MyStrdup(fptr->nick);

    notice(n_ChanServ, lptr->nick,
      "The alternate contact nickname for %s has been changed to [\002%s\002]",
      cptr->name,
      fptr->nick);
  }
  else
  {
    if (cptr->alternate)
      MyFree(cptr->alternate);
    cptr->alternate = NULL;
    notice(n_ChanServ, lptr->nick,
      "The alternate contact nickname for %s has been cleared",
      cptr->name);
  }
} /* c_set_alternate() */

static void
c_set_mlock(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)
{
  struct ChanInfo *cptr;
  char modes[MAXLINE];
  struct Channel *chptr;
  int minus = 0,
    argidx = 4,
    temp_modes_on,
    temp_modes_off;
  unsigned int ii;

  if (!(cptr = FindChan(av[1])))
    return;

  temp_modes_on = (MODE_CAPP | MODE_CAPL | MODE_CAPD) & cptr->modes_on;
  temp_modes_off = (MODE_CAPP | MODE_CAPL | MODE_CAPD) & cptr->modes_off;

  if (ac < 4)
    {
      notice(n_ChanServ, lptr->nick,
             "Syntax: \002SET <channel> MLOCK <modes>\002");
      notice(n_ChanServ, lptr->nick,
             ERR_MORE_INFO,
             n_ChanServ,
             "SET MLOCK");
      return;
    }

  RecordCommand("%s: %s!%s@%s SET [%s] MLOCK %s",
                n_ChanServ,
                lptr->nick,
                lptr->username,
                lptr->hostname,
                cptr->name,
                av[3]);

  cptr->modes_on = cptr->modes_off = 0;
  cptr->limit = 0;
  if (cptr->key)
    {
      MyFree(cptr->key);
      cptr->key = NULL;
    }
  if (cptr->forward)
    {
      MyFree(cptr->forward);
      cptr->forward = NULL;
    }
  if (cptr->throttle)
    {
      MyFree(cptr->throttle);
      cptr->throttle = NULL;
    }
  if (cptr->dline)
    {
      MyFree(cptr->dline);
      cptr->dline = NULL;
    }

  for (ii = 0; ii < strlen(av[3]); ++ii)
    {
      switch (av[3][ii])
        {
        case '+':
          {
            minus = 0;
            break;
          }
        case '-':
          {
            minus = 1;
            break;
          }

        case 'n':
          {
            if (minus)
              cptr->modes_off |= MODE_N;
            else
              cptr->modes_on |= MODE_N;
            break;
          }

#ifdef HYBRID7
          /* Add mode a removal -Janos */
        case 'a':
        case 'A':
          {
            if (minus)
              cptr->modes_off |= MODE_A;
            else
              cptr->modes_on |= MODE_A;
            break;
          }
#endif /* HYBRID7 */

        case 't':
          {
            if (minus)
              cptr->modes_off |= MODE_T;
            else
              cptr->modes_on |= MODE_T;
            break;
          }

        case 's':
          {
            if (minus)
              cptr->modes_off |= MODE_S;
            else
              cptr->modes_on |= MODE_S;
            break;
          }

        case 'p':
          {
            if (minus)
              cptr->modes_off |= MODE_P;
            else
              cptr->modes_on |= MODE_P;
            break;
          }

        case 'm':
          {
            if (minus)
              cptr->modes_off |= MODE_M;
            else
              cptr->modes_on |= MODE_M;
            break;
          }

        case 'i':
          {
            if (minus)
              cptr->modes_off |= MODE_I;
            else
              cptr->modes_on |= MODE_I;
            break;
          }

        case 'r':
          {
            if (minus)
              cptr->modes_off |= MODE_R;
            else
              cptr->modes_on |= MODE_R;
            break;
          }

        case 'z':
          {
            if (minus)
              cptr->modes_off |= MODE_Z;
            else
              cptr->modes_on |= MODE_Z;
            break;
          }

        case 'P':
          {
            if ((lptr->umodes) & UMODE_P)
              {
                if (minus)
                  cptr->modes_off |= MODE_CAPP;
                else
                  cptr->modes_on |= MODE_CAPP;
              }
            break;
          }

        case 'L':
          {
            if ((lptr->umodes) & UMODE_CAPX)
              {
                if (minus)
                  cptr->modes_off |= MODE_CAPL;
                else
                  cptr->modes_on |= MODE_CAPL;
              }
            break;
          }

#if 0
        case 'F':
          {
            if ((lptr->umodes) & UMODE_CAPX)
              {
                if (minus)
                  cptr->modes_off |= MODE_CAPF;
                else
                  cptr->modes_on |= MODE_CAPF;
              }
            break;
          }
#endif

        case 'c':
          {
            if (minus)
              cptr->modes_off |= MODE_C;
            else
              cptr->modes_on |= MODE_C;
            break;
          }

        case 'g':
          {
            if (minus)
              cptr->modes_off |= MODE_G;
            else
              cptr->modes_on |= MODE_G;
            break;
          }

        case 'Q':
          {
            if (minus)
              cptr->modes_off |= MODE_CAPQ;
            else
              cptr->modes_on |= MODE_CAPQ;
            break;
          }

        case 'R':
          {
            if (minus)
              cptr->modes_off |= MODE_CAPR;
            else
              cptr->modes_on |= MODE_CAPR;
            break;
          }

        case 'l':
          {
            if (minus)
              {
                cptr->limit = 0;
                cptr->modes_off |= MODE_L;
              }
            else
              {
                if (ac >= (argidx + 1))
                  if (IsNum(av[argidx]))
                    cptr->limit = atoi(av[argidx++]);
              }
            break;
          }

        case 'k':
          {
            if (minus)
              {
                if (cptr->key)
                  MyFree(cptr->key);
                cptr->key = NULL;
                cptr->modes_off |= MODE_K;
              }
            else
              if (ac >= (argidx + 1))
                {
                  if (cptr->key)
                    MyFree(cptr->key);
                  cptr->key = MyStrdup(av[argidx++]);
                }
            break;
          }

        case 'f':
          {
            if (minus)
              {
                if (cptr->forward)
                  MyFree(cptr->forward);
                cptr->forward = NULL;
                cptr->modes_off |= MODE_F;
              }
            else
              if (ac >= (argidx + 1))
                {
                  if (cptr->forward)
                    MyFree(cptr->forward);
                  cptr->forward = MyStrdup(av[argidx++]);
                }
            break;
          }

        case 'J':
          {
            if (minus)
              {
                if (cptr->throttle)
                  MyFree(cptr->throttle);
                cptr->throttle = NULL;
                cptr->modes_off |= MODE_CAPJ;
              } 
            else
              if (ac >= (argidx + 1))
                {
                  if (cptr->throttle)
                    MyFree(cptr->throttle);
                  cptr->throttle = MyStrdup(av[argidx++]);
                }
            break;
          }

        case 'D':
          {
            if ((lptr->umodes) & UMODE_J)
              {
                if (minus)
                  {
                    if (cptr->dline)
                      MyFree(cptr->dline);
                    cptr->dline = NULL;
                    cptr->modes_off |= MODE_CAPD;
                  } 
                else
                  if (ac >= (argidx + 1))
                    {
                      if (cptr->dline)
                        MyFree(cptr->dline);
                      cptr->dline = MyStrdup(av[argidx++]);
                    }
                break;
              }
          }

        default: break;
        } /* switch */
    }

  if (!((lptr->umodes) & UMODE_P) && temp_modes_on & MODE_CAPP)
    {
      cptr->modes_on |= MODE_CAPP;
    }
  if (!((lptr->umodes) & UMODE_P) && temp_modes_off & MODE_CAPP)
    {
      cptr->modes_off |= MODE_CAPP;
    }
  if (!((lptr->umodes) & UMODE_CAPX) && temp_modes_on & MODE_CAPL)
    {
      cptr->modes_on |= MODE_CAPL;
    }
  if (!((lptr->umodes) & UMODE_CAPX) && temp_modes_off & MODE_CAPL)
    {
      cptr->modes_off |= MODE_CAPL;
    }
  if (!((lptr->umodes) & UMODE_J) && temp_modes_on & MODE_CAPD)
    {
      cptr->modes_on |= MODE_CAPD;
    }
  if (!((lptr->umodes) & UMODE_J) && temp_modes_off & MODE_CAPD)
    {
      cptr->modes_off |= MODE_CAPD;
    }

  modes[0] = '\0';
  if (cptr->modes_off)
    {
      strcat(modes, "-");
      if (cptr->modes_off & MODE_S)
        strcat(modes, "s");
      if (cptr->modes_off & MODE_P)
        strcat(modes, "p");
      if (cptr->modes_off & MODE_N)
        strcat(modes, "n");
      if (cptr->modes_off & MODE_T)
        strcat(modes, "t");
      if (cptr->modes_off & MODE_M)
        strcat(modes, "m");
      if (cptr->modes_off & MODE_I)
        strcat(modes, "i");
      if (cptr->modes_off & MODE_C)
        strcat(modes, "c");
      if (cptr->modes_off & MODE_G)
        strcat(modes, "g");
      if (cptr->modes_off & MODE_R)
        strcat(modes, "r");
      if (cptr->modes_off & MODE_Z)
        strcat(modes, "z");
#if 0
      if (cptr->modes_off & MODE_CAPF)
        strcat(modes, "F");
#endif
      if (cptr->modes_off & MODE_CAPJ)
        strcat(modes, "J");
      if (cptr->modes_off & MODE_CAPD)
        strcat(modes, "D");
      if (cptr->modes_off & MODE_CAPP)
        strcat(modes, "P");
      if (cptr->modes_off & MODE_CAPQ)
        strcat(modes, "Q");
      if (cptr->modes_off & MODE_CAPL)
        strcat(modes, "L");
      if (cptr->modes_off & MODE_CAPR)
        strcat(modes, "R");
      if (cptr->modes_off & MODE_L)
        strcat(modes, "l");
      if (cptr->modes_off & MODE_K)
        strcat(modes, "k");
#ifdef HYBRID7
      /* Mode A removal -Janos */
      if (cptr->modes_off & MODE_A)
        strcat(modes, "a");
#endif /* HYBRID7 */
      if (cptr->modes_off & MODE_F)
        strcat(modes, "f");
    }
  if (cptr->modes_on ||
      cptr->limit ||
      cptr->key ||
      cptr->forward ||
      cptr->throttle ||
      cptr->dline)
    {
      strcat(modes, "+");
      if (cptr->modes_on & MODE_S)
        strcat(modes, "s");
#ifdef HYBRID7
      /* Add mode a -Janos */
      if (cptr->modes_on & MODE_A)
        strcat(modes, "a");
#endif /* HYBRID7 */
      if (cptr->modes_on & MODE_P)
        strcat(modes, "p");
      if (cptr->modes_on & MODE_N)
        strcat(modes, "n");
      if (cptr->modes_on & MODE_T)
        strcat(modes, "t");
      if (cptr->modes_on & MODE_C)
        strcat(modes, "c");
      if (cptr->modes_on & MODE_G)
        strcat(modes, "g");
      if (cptr->modes_on & MODE_M)
        strcat(modes, "m");
      if (cptr->modes_on & MODE_I)
        strcat(modes, "i");
      if (cptr->modes_on & MODE_R)
        strcat(modes, "r");
      if (cptr->modes_on & MODE_Z)
        strcat(modes, "z");
#if 0
      if (cptr->modes_on & MODE_CAPF)
        strcat(modes, "F");
#endif
      if (cptr->modes_on & MODE_CAPP)
        strcat(modes, "P");
      if (cptr->modes_on & MODE_CAPQ)
        strcat(modes, "Q");
      if (cptr->modes_on & MODE_CAPL)
        strcat(modes, "L");
      if (cptr->modes_on & MODE_CAPR)
        strcat(modes, "R");
      if (cptr->limit)
        strcat(modes, "l");
      if (cptr->key)
        strcat(modes, "k");
      if (cptr->forward)
        strcat(modes, "f");
      if (cptr->throttle)
        strcat(modes, "J");
      if (cptr->dline)
        strcat(modes, "D");

      if (cptr->limit)
        {
          char  temp[MAXLINE];
          ircsprintf(temp, "%s %ld", modes, cptr->limit);
          strcpy(modes, temp);
        }
      if (cptr->key)
        {
          strcat(modes, " ");
          strcat(modes, cptr->key);
        }
      if (cptr->forward)
        {
          strcat(modes, " ");
          strcat(modes, cptr->forward);
        }
      if (cptr->throttle)
        {
          strcat(modes, " ");
          strcat(modes, cptr->throttle);
        }
      if (cptr->dline)
        {
          strcat(modes, " ");
          strcat(modes, cptr->dline);
        }
    }

  if ((chptr = FindChannel(cptr->name)))
    {
      toserv(":%s MODE %s %s %s\n",
             n_ChanServ,
             cptr->name,
             modes,
             ((cptr->modes_off & MODE_K) && (chptr->key)) ? chptr->key : "");
      UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
    }

  notice(n_ChanServ, lptr->nick,
         "Now enforcing modes [\002%s\002] for %s",
         modes,
         cptr->name);
} /* c_set_mlock() */

static void
c_set_topic(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char *topic;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> TOPIC <topic|->\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET TOPIC");
    return;
  }

  if (!irccmp(av[3], "-"))
  {
    if (cptr->topic)
      MyFree(cptr->topic);
    cptr->topic = NULL;
    notice(n_ChanServ, lptr->nick,
      "The topic for %s has been cleared",
      cptr->name);
    RecordCommand("%s: %s!%s@%s SET [%s] TOPIC -",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);
    return;
  }

  /* But hey we've already checked (ac < 4)! -kre */
#if 0
  if (ac < 4)
    topic = NULL;
  else
#endif
    topic = GetString(ac - 3, av + 3);

  /* Truncate topiclen. It can't be NULL, since ac would be < 4 -kre */
  if (strlen(topic) > TOPICLEN)
      topic[TOPICLEN]=0;

  RecordCommand("%s: %s!%s@%s SET [%s] TOPIC %s",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
    topic);

  MyFree(cptr->topic);

  cptr->topic = topic;

  cs_SetTopic(FindChannel(cptr->name), cptr->topic);

  notice(n_ChanServ, lptr->nick,
    "The topic for %s has been set to [\002%s\002]",
    cptr->name, cptr->topic);
} /* c_set_topic() */

static void
c_set_entrymsg(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char *emsg;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> ENTRYMSG <message|->\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET ENTRYMSG");
    return;
  }

  if (!irccmp(av[3], "-"))
  {
    if (cptr->entrymsg)
      MyFree(cptr->entrymsg);
    cptr->entrymsg = NULL;
    notice(n_ChanServ, lptr->nick,
      "The entry message for %s has been cleared",
      cptr->name);
    RecordCommand("%s: %s!%s@%s SET [%s] ENTRYMSG -",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);
    return;
  }

  if (ac < 4)
    emsg = NULL;
  else
    emsg = GetString(ac - 3, av + 3);

  RecordCommand("%s: %s!%s@%s SET [%s] ENTRYMSG %s",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
    emsg);

  if (cptr->entrymsg)
    MyFree(cptr->entrymsg);

  cptr->entrymsg = emsg;

  notice(n_ChanServ, lptr->nick,
    "The entry message for %s has been set to [\002%s\002]",
    cptr->name,
    cptr->entrymsg);
} /* c_set_entrymsg() */

static void
c_set_email(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> EMAIL <email address|->\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET EMAIL");
    return;
  }

  if (cptr->email)
    MyFree(cptr->email);

  if (!irccmp(av[3], "-"))
  {
    cptr->email = NULL;
    notice(n_ChanServ, lptr->nick,
      "The email address for %s has been cleared",
      cptr->name);
    RecordCommand("%s: %s!%s@%s SET [%s] EMAIL -",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);
    return;
  }

  RecordCommand("%s: %s!%s@%s SET [%s] EMAIL %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    av[3]);

  cptr->email = MyStrdup(av[3]);

  notice(n_ChanServ, lptr->nick,
    "The email address for %s has been set to [\002%s\002]",
    cptr->name,
    cptr->email);
} /* c_set_email() */

static void
c_set_url(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (!(cptr = FindChan(av[1])))
    return;

  if (ac < 4)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SET <channel> URI <http://URI|->\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SET URI");
    return;
  }

  if (cptr->url)
    MyFree(cptr->url);

  if (!irccmp(av[3], "-"))
  {
    cptr->url = NULL;
    notice(n_ChanServ, lptr->nick,
      "The URI for %s has been cleared",
      cptr->name);
    RecordCommand("%s: %s!%s@%s SET [%s] URI -",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);
    return;
  }

  RecordCommand("%s: %s!%s@%s SET [%s] URI %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    av[3]);

  cptr->url = MyStrdup(av[3]);

  notice(n_ChanServ, lptr->nick,
    "The URI for %s has been set to [\002%s\002]",
    cptr->name,
    cptr->url);
} /* c_set_url() */

/*
c_getkey()
  Return key for channel av[1]
*/

static void
c_getkey(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Channel *chptr=0;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002GETKEY <channel>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "GETKEY");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (cptr->flags & CS_FORBID)
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] is a forbidden channel",
      cptr->name);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDINVITE))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDINVITE],
      "GETKEY",
      cptr->name);

    RecordCommand("%s: %s!%s@%s failed GETKEY [%s]",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);

    return;
  }

  chptr = FindChannel(cptr->name);
  if (chptr == NULL)
  {
    notice(n_ChanServ, lptr->nick,
      "Channel [\002%s\002] is not active",
      cptr->name);
    return;
  }

  if (IsChannelMember(chptr, lptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You are already on [\002%s\002]",
      cptr->name);
    return;
  }

  /* They can see this from outside, no need to log. */
  if (chptr->key == NULL || chptr->key[0] == '\0')
  {
    notice(n_ChanServ, lptr->nick,
      "Channel [\002%s\002] is not keyed",
      cptr->name);
    return;
  }

  RecordCommand("%s: %s!%s@%s GETKEY [%s]",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name);

  notice(n_ChanServ, lptr->nick,
    "Channel [\002%s\002] key is [\002%s\002]",
    chptr->name, chptr->key);
} /* c_getkey() */

/*
c_invite()
  Invite lptr->nick to channel av[1]
*/

static void
c_invite(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Channel *chptr=0;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002INVITE <channel>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "INVITE");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (cptr->flags & CS_FORBID)
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] is a forbidden channel",
      cptr->name);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDINVITE))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDINVITE],
      "INVITE",
      cptr->name);

    RecordCommand("%s: %s!%s@%s failed INVITE [%s]",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);

    return;
  }

  RecordCommand("%s: %s!%s@%s INVITE [%s]",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name);

  if (IsChannelMember(FindChannel(av[1]), lptr))
  {
    notice(n_ChanServ, lptr->nick,
      "You are already on [\002%s\002]",
      cptr->name);
    return;
  }

  /*
   * Everything checks out - invite
   *
   * For ircd-hybrid 6+, ChanServ need to be inside the channel
   * to invite - have ChanServ join and part
   */

  if ((chptr = FindChannel(cptr->name)) || (cptr->modes_on & MODE_CAPP))
  {
#ifdef JOIN_CHANNELS
    /* it only needs to join if it's not guarding - toot */
    if (!(cptr->flags & CS_GUARD))
    {
      toserv(":%s SJOIN %ld %s + :@%s\n",
        Me.name,
        chptr->since,
        cptr->name,
        n_ChanServ);
    }
#endif

    toserv(":%s INVITE %s %s\n",
      n_ChanServ,
      lptr->nick,
      cptr->name);

#ifdef JOIN_CHANNELS
    /* only PART if it's not in GUARD mode - toot */
    /* It will PART also if AllowGuardChannel is not set -kre */
    if (!(cptr->flags & CS_GUARD) || !AllowGuardChannel)
    {
      toserv(":%s PART %s\n",
        n_ChanServ,
        cptr->name);
    }
#endif
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is empty",
      cptr->name);
  }
} /* c_invite() */

/*
c_op()
  Op lptr->nick on channel av[1]
*/

static void
c_op(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)
{
  struct ChanInfo *cptr;
  struct Channel *chptr;
  char *onicks, *dnicks;
  struct UserChannel *uchan;
  struct ChannelUser *cuser;
  struct Luser *currlptr;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick, "Syntax: \002OP <channel> [nicks]\002");
    notice(n_ChanServ, lptr->nick, ERR_MORE_INFO, n_ChanServ, "OP");
    return;
  }

  if (!irccmp(av[1], "ALL"))
    cptr = NULL;
  else if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!cptr)
  {
    /* They want to be opped in all channels they are currently in. */
    for (uchan = lptr->firstchan; uchan; uchan = uchan->next)
    {
      if (HasAccess(FindChan(uchan->chptr->name), lptr, CA_CMDOP))
      {
        if (!(uchan->flags & CH_OPPED))
        {
          toserv(":%s MODE %s +o %s\n", n_ChanServ,
            uchan->chptr->name, lptr->nick);
          uchan->flags |= CH_OPPED;

          if ((cuser = FindUserByChannel(uchan->chptr, lptr)))
            cuser->flags |= CH_OPPED;
        }
        else
        {
          notice(n_ChanServ, lptr->nick,
            "You are already opped on [\002%s\002]", uchan->chptr->name);
        }
      }
    }

    notice(n_ChanServ, lptr->nick,
      "You have been opped on all channels you have access to");

    RecordCommand("%s: %s!%s@%s OP ALL",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname);

    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDOP))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS, cptr->access_lvl[CA_CMDOP], "OP", cptr->name);
    RecordCommand("%s: %s!%s@%s failed OP [%s]",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name);
    return;
  }

  chptr = FindChannel(av[1]);
  if (ac < 3)
  {
    if (HasFlag(lptr->nick, NS_NOCHANOPS))
      return;

    if (!IsChannelMember(chptr, lptr))
    {
      notice(n_ChanServ, lptr->nick,
        "You are not on [\002%s\002]",
        cptr->name);
      return;
    }

    if (IsChannelOp(chptr, lptr))
    {
      notice(n_ChanServ, lptr->nick,
        "You are already opped on [\002%s\002]", cptr->name);
      return;
    }

    onicks = MyStrdup(lptr->nick);
    dnicks = MyStrdup("");

  }
  else
  {
    int ii, arc;
    char *tempnix, *tempptr, **arv;

    /* they want to op other people */
    tempnix = GetString(ac - 2, av + 2);

    tempptr = tempnix;
    arc = SplitBuf(tempnix, &arv);
    onicks = (char *) MyMalloc(sizeof(char));
    onicks[0] = '\0';
    dnicks = (char *) MyMalloc(sizeof(char));
    dnicks[0] = '\0';
    for (ii = 0; ii < arc; ii++)
    {
      if (*arv[ii] == '-')
        currlptr = FindClient(arv[ii] + 1);
      else
        currlptr = FindClient(arv[ii]);

      if (!IsChannelMember(chptr, currlptr))
        continue;

      if ((arv[ii][0] == '-') && IsChannelOp(chptr, currlptr) &&
		      irccmp(n_ChanServ, arv[ii] + 1) != 0 &&
		      irccmp(n_OperServ, arv[ii] + 1) != 0)
      {
        dnicks = (char *) MyRealloc(dnicks, strlen(dnicks)
            + strlen(arv[ii] + 1) + (2 * sizeof(char)));
        strcat(dnicks, arv[ii] + 1);
        strcat(dnicks, " ");
      }
      else if (!IsChannelOp(chptr, currlptr))
      {
	struct Luser *alptr = FindClient(arv[ii]);
        if ((cptr->flags & CS_SECUREOPS) && (alptr != lptr) &&
            (!HasAccess(cptr, alptr, CA_CMDOP)))
          continue;

        /*
         * Don't op them if they are flagged
         */
        if (HasFlag(arv[ii], NS_NOCHANOPS))
          continue;
        
        onicks = (char *) MyRealloc(onicks, strlen(onicks)
            + strlen(arv[ii]) + (2 * sizeof(char)));
        strcat(onicks, arv[ii]);
        strcat(onicks, " ");
      }
    }

    MyFree(tempptr);
    MyFree(arv);
  }

  SetModes(n_ChanServ, 0, 'o', chptr, dnicks);
  SetModes(n_ChanServ, 1, 'o', chptr, onicks);

  RecordCommand("%s: %s!%s@%s OP [%s]%s%s%s%s",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
    strlen(onicks) ? " [+] " : "", strlen(onicks) ? onicks : "",
    strlen(dnicks) ? " [-] " : "", strlen(dnicks) ? dnicks : "");

  MyFree(onicks);
  MyFree(dnicks);
} /* c_op() */

#ifdef HYBRID7
/* c_hop() 
 * (De)Halfop nicks on channel av[1]
 * -Janos
 *
 * XXX: *Urgh!* Get rid of this nasty long code, merge with c_op() and
 * rewrite the latter.. -kre
 */
static void c_hop(struct Luser *lptr, struct NickInfo *nptr, int ac, char
    **av)
{
  struct ChanInfo *cptr;
  struct Channel *chptr;
  char *hnicks, *dnicks;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick, "Syntax: \002HALFOP <channel>\002");
    notice(n_ChanServ, lptr->nick, ERR_MORE_INFO, n_ChanServ, "HALFOP");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick, ERR_CH_NOT_REGGED, av[1]);
    return;
  }

  /* NOTE: only CMDOP people can +h other people */
  if (!HasAccess(cptr, lptr, CA_CMDOP))
  {
    notice(n_ChanServ, lptr->nick, ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDOP], "OP", cptr->name);
    RecordCommand("%s: %s!%s@%s failed HALFOP [%s]",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name);
    return;
  }

  chptr = FindChannel(av[1]);
  if (ac < 3)
  {
    hnicks = MyStrdup(lptr->nick);
    dnicks = MyStrdup("");
    if (!IsChannelMember(chptr, lptr))
    {
      notice(n_ChanServ, lptr->nick,
        "You are not on [\002%s\002]", cptr->name);
      MyFree(hnicks);
      MyFree(dnicks);
      return;
    }
  }
  else
  {
    int ii, arc;
    char *tempnix, *tempptr, **arv;
    struct Luser *currlptr;

    /* they want to voice other people */
    tempnix = GetString(ac - 2, av + 2);

    tempptr = tempnix;
    arc = SplitBuf(tempnix, &arv);
    hnicks = (char *) MyMalloc(sizeof(char));
    hnicks[0] = '\0';
    dnicks = (char *) MyMalloc(sizeof(char));
    dnicks[0] = '\0';
    for (ii = 0; ii < arc; ii++)
    {
      if (*arv[ii] == '-')
        currlptr = FindClient(arv[ii] + 1);
      else
        currlptr = FindClient(arv[ii]);

      if (!IsChannelMember(chptr, currlptr))
        continue;

      if (arv[ii][0] == '-')
      {
        dnicks = (char *) MyRealloc(dnicks, strlen(dnicks)
            + strlen(arv[ii] + 1) + (2 * sizeof(char)));
        strcat(dnicks, arv[ii] + 1);
        strcat(dnicks, " ");
      }
      else
      {
        hnicks = (char *) MyRealloc(hnicks, strlen(hnicks)
            + strlen(arv[ii]) + (2 * sizeof(char)));
        strcat(hnicks, arv[ii]);
        strcat(hnicks, " ");
      }
    }

    MyFree(tempptr);
    MyFree(arv);
  }

  SetModes(n_ChanServ, 0, 'h', chptr, dnicks);
  SetModes(n_ChanServ, 1, 'h', chptr, hnicks);

  RecordCommand("%s: %s!%s@%s HALFOP [%s]%s%s%s%s",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, cptr->name,
    strlen(hnicks) ? " [+] " : "", strlen(hnicks) ? hnicks : "",
    strlen(dnicks) ? " [-] " : "", strlen(dnicks) ? dnicks : "");

  MyFree(hnicks);
  MyFree(dnicks);

  return;
} /* c_halfop() */
#endif /* HYBRID7 */

/*
c_voice()
  (De)Voice nicks on channel av[1]
*/

static void
c_voice(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Channel *chptr;
  char *vnicks, *dnicks;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002VOICE <channel>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "VOICE");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDVOICE))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDVOICE],
      "VOICE",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed VOICE [%s]",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name);
    return;
  }

  chptr = FindChannel(av[1]);
  if (ac < 3)
  {
    vnicks = MyStrdup(lptr->nick);
    dnicks = MyStrdup("");
    if (!IsChannelMember(chptr, lptr))
    {
      notice(n_ChanServ, lptr->nick,
        "You are not on [\002%s\002]",
        cptr->name);
      MyFree(vnicks);
      MyFree(dnicks);
      return;
    }
  }
  else
  {
    int ii, arc;
    char *tempnix, *tempptr, **arv;
    struct Luser *currlptr;

    /* they want to voice other people */
    tempnix = GetString(ac - 2, av + 2);

    tempptr = tempnix;
    arc = SplitBuf(tempnix, &arv);
    vnicks = (char *) MyMalloc(sizeof(char));
    vnicks[0] = '\0';
    dnicks = (char *) MyMalloc(sizeof(char));
    dnicks[0] = '\0';
    for (ii = 0; ii < arc; ii++)
    {
      if (*arv[ii] == '-')
        currlptr = FindClient(arv[ii] + 1);
      else
        currlptr = FindClient(arv[ii]);

      if (!IsChannelMember(chptr, currlptr))
        continue;

      if (arv[ii][0] == '-')
      {
        dnicks = (char *) MyRealloc(dnicks, strlen(dnicks)
            + strlen(arv[ii] + 1) + (2 * sizeof(char)));
        strcat(dnicks, arv[ii] + 1);
        strcat(dnicks, " ");
      }
      else
      {
        vnicks = (char *) MyRealloc(vnicks, strlen(vnicks)
            + strlen(arv[ii]) + (2 * sizeof(char)));
        strcat(vnicks, arv[ii]);
        strcat(vnicks, " ");
      }
    }

    MyFree(tempptr);
    MyFree(arv);
  }

  SetModes(n_ChanServ, 0, 'v', chptr, dnicks);
  SetModes(n_ChanServ, 1, 'v', chptr, vnicks);

  RecordCommand("%s: %s!%s@%s VOICE [%s]%s%s%s%s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    strlen(vnicks) ? " [+] " : "",
    strlen(vnicks) ? vnicks : "",
    strlen(dnicks) ? " [-] " : "",
    strlen(dnicks) ? dnicks : "");

  MyFree(vnicks);
  MyFree(dnicks);

  return;
} /* c_voice() */

/*
c_unban()
  Unban all hostmasks matching lptr->nick on channel av[1]
*/

static void
c_unban(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  char chkstr[MAXLINE], chkstr2[MAXLINE], *bans;
  struct Channel *chptr;
  struct ChannelBan *bptr;
#ifdef GECOSBANS
  struct ChannelGecosBan *dptr;
#endif
  int ncleared = 0, all = 0;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002UNBAN <channel> [ALL]\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "UNBAN");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDUNBAN))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDUNBAN],
      "UNBAN",
      cptr->name);
    RecordCommand("%s: %s!%s@%s failed UNBAN [%s] %s",
      n_ChanServ,
      lptr->nick,
      lptr->username,
      lptr->hostname,
      cptr->name,
      (ac >= 3) ? av[2] : "");
    return;
  }
 
   ircsprintf(chkstr, "%s!%s@%s", lptr->nick, lptr->username,
       lptr->hostname);
   ircsprintf(chkstr2, "%s!%s@%s", lptr->nick, lptr->username,
       lptr->realip);

  if (ac >= 3)
    if (!irccmp(av[2], "ALL"))
    {
      if (!HasAccess(cptr, lptr, CA_CMDCLEAR))
      {
	notice(n_ChanServ, lptr->nick, ERR_NEED_ACCESS,
	  cptr->access_lvl[CA_CMDCLEAR], "UNBAN ALL", cptr->name);
        return;
      }
      else
        all = 1;
    }

  if (!(chptr = FindChannel(cptr->name)))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      cptr->name);
    return;
  }

  if (!all && HasException(chptr, lptr))
  {
    notice(n_ChanServ, lptr->nick,
      "Channel [\002%s\002] has a ban exception (+e) for you, not clearing bans.",
      cptr->name);
    return;
  }

  bans = (char *) MyMalloc(sizeof(char));
  bans[0] = '\0';
  for (bptr = chptr->firstban; bptr; bptr = bptr->next)
  {
    if (all || match(bptr->mask, chkstr) || match(bptr->mask, chkstr2))
    {
      bans = (char *) MyRealloc(bans, strlen(bans) + strlen(bptr->mask) + (2 * sizeof(char)));
      strcat(bans, bptr->mask);
      strcat(bans, " ");
      ncleared++;
    }
  }

  SetModes(n_ChanServ, 0, 'b', chptr, bans);

  MyFree(bans);

#ifdef GECOSBANS
  bans = (char *) MyMalloc(sizeof(char));
  bans[0] = '\0';
  for (dptr = chptr->firstgecosban; dptr; dptr = dptr->next)
  {
    if (all || match(dptr->mask, lptr->realname))
    {
      bans = (char *) MyRealloc(bans, strlen(bans) + strlen(dptr->mask) + (2 * sizeof(char)));
      strcat(bans, dptr->mask);
      strcat(bans, " ");
      ncleared++;
    }
  }

  SetModes(n_ChanServ, 0, 'd', chptr, bans);

  MyFree(bans);
#endif

  if (ncleared)
  {
    notice(n_ChanServ, lptr->nick,
      "All (%d) bans matching [\002%s\002] and [\002%s\002] have been cleared on %s",
      ncleared,
      (all) ? "*!*@*" : chkstr,
      (all) ? "*!*@*" : chkstr2,
      cptr->name);
  }
  else
  {
    notice(n_ChanServ, lptr->nick,
      "No bans matching [\002%s\002] and [\002%s\002] were found on %s",
      (all) ? "*!*@*" : chkstr,
      (all) ? "*!*@*" : chkstr2,
      cptr->name);
  }

  RecordCommand("%s: %s!%s@%s UNBAN [%s] %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    (all) ? "ALL" : "");

  return;
} /* c_unban() */

/*
c_info()
  Display info about channel av[1]
*/

static void
c_info(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)
{
  struct ChanInfo *cptr;
  char buf[MAXLINE];
  int contact_online = 0, alternate_online = 0;

  if (ac < 2)
    {
      notice(n_ChanServ, lptr->nick,
	     "Syntax: \002INFO <channel>\002");
      notice(n_ChanServ, lptr->nick,
	     ERR_MORE_INFO,
	     n_ChanServ,
	     "INFO");
      return;
    }

  if (!(cptr = FindChan(av[1])))
    {
      notice(n_ChanServ, lptr->nick,
	     ERR_CH_NOT_REGGED,
	     av[1]);
      return;
    }
  
  if (((cptr->flags & CS_PRIVATE) || (cptr->flags & CS_FORBID))
      && !IsContact(lptr, cptr) && GetAccess(cptr, lptr) <= 0
#ifdef EMPOWERADMINS
      && !IsValidAdmin(lptr)
#endif
    )
    {
      notice(n_ChanServ, lptr->nick,
	     "Channel [\002%s\002] is private",
	     cptr->name);
      return;
    }

  RecordCommand("%s: %s!%s@%s INFO [%s]",
		n_ChanServ,
		lptr->nick,
		lptr->username,
		lptr->hostname,
		cptr->name);

  {
    struct NickInfo *ni;
    if ((ni = FindNick(cptr->alternate)))
      if (ni->flags & NS_IDENTIFIED)
	alternate_online = 1;
    if ((ni = FindNick(cptr->contact)))
      if (ni->flags & NS_IDENTIFIED)
	contact_online = 1;
  }

  notice(n_ChanServ, lptr->nick,
    "     Channel: %s",
    cptr->name);
  notice(n_ChanServ, lptr->nick,
	 "     Contact: %s%s%s%s",
	 cptr->contact ? cptr->contact : "",
	 contact_online ? " << ONLINE >>" : (cptr->last_contact_active ? ", last seen: " : ""),
	 (contact_online || !cptr->last_contact_active) ? "" : timeago(cptr->last_contact_active, 1),
	 (contact_online || !cptr->last_contact_active) ? "" : " ago");
  if (cptr->alternate)
    notice(n_ChanServ, lptr->nick,
	   "   Alternate: %s%s%s%s",
	   cptr->alternate ? cptr->alternate : "",
	   alternate_online ? " << ONLINE >>" : (cptr->last_alternate_active ? ", last seen: " : ""),
	   (alternate_online || !cptr->last_alternate_active) ? "" : timeago(cptr->last_alternate_active, 1),
	   (alternate_online || !cptr->last_alternate_active) ? "" : " ago");
  notice(n_ChanServ, lptr->nick,
    "  Registered: %s ago",
    timeago(cptr->created, 1));
  if (!FindChannel(cptr->name))
    notice(n_ChanServ, lptr->nick,
      "   Last Used: %s ago",
      timeago(cptr->lastused, 1));

  if (cptr->topic)
    notice(n_ChanServ, lptr->nick,
      "       Topic: %s",
      cptr->topic);

  if (cptr->email)
    notice(n_ChanServ, lptr->nick,
      "       Email: %s",
      cptr->email);

  if (cptr->url)
    notice(n_ChanServ, lptr->nick,
      " Contact URI: %s",
      cptr->url);

  buf[0] = '\0';
  if (cptr->flags & CS_TOPICLOCK)
    strcat(buf, "TopicLock, ");
  if (cptr->flags & CS_SECURE)
    strcat(buf, "Secure, ");
  if (cptr->flags & CS_SECUREOPS)
    strcat(buf, "SecureOps, ");
  if (cptr->flags & CS_GUARD)
    strcat(buf, "ChanGuard, ");
  if (cptr->flags & CS_RESTRICTED)
    strcat(buf, "Restricted, ");
  if (cptr->flags & CS_PRIVATE)
    strcat(buf, "Private, ");
  if (cptr->flags & CS_FORBID)
    strcat(buf, "Forbidden, ");
  if (cptr->flags & CS_NOEXPIRE)
    strcat(buf, "NoExpire, ");
  if (cptr->flags & CS_SPLITOPS)
    strcat(buf, "SplitOps, ");
  if (cptr->flags & CS_VERBOSE)
    strcat(buf, "Verbose, ");

  if (*buf)
  {
    /* kill the trailing "," */
    buf[strlen(buf) - 2] = '\0';
    notice(n_ChanServ, lptr->nick,
      "     Options: %s",
      buf);
  }

  buf[0] = '\0';
  if (cptr->modes_off)
  {
    strcat(buf, "-");
    if (cptr->modes_off & MODE_S)
      strcat(buf, "s");
#ifdef HYBRID7
    /* Mode A removal -Janos */
    if (cptr->modes_off & MODE_A)
      strcat(buf, "a");
#endif /* HYBRID7 */
    if (cptr->modes_off & MODE_P)
      strcat(buf, "p");
    if (cptr->modes_off & MODE_N)
      strcat(buf, "n");
    if (cptr->modes_off & MODE_T)
      strcat(buf, "t");
    if (cptr->modes_off & MODE_C)
      strcat(buf, "c");
    if (cptr->modes_off & MODE_G)
      strcat(buf, "g");
    if (cptr->modes_off & MODE_R)
      strcat(buf, "r");
    if (cptr->modes_off & MODE_Z)
      strcat(buf, "z");
#if 0
    if (cptr->modes_off & MODE_CAPF)
      strcat(buf, "F");
#endif
    if (cptr->modes_off & MODE_CAPJ)
      strcat(buf, "J");
    if (cptr->modes_off & MODE_CAPD)
      strcat(buf, "D");
    if (cptr->modes_off & MODE_CAPP)
      strcat(buf, "P");
    if (cptr->modes_off & MODE_CAPQ)
      strcat(buf, "Q");
    if (cptr->modes_off & MODE_CAPL)
      strcat(buf, "L");
    if (cptr->modes_off & MODE_CAPR)
      strcat(buf, "R");
    if (cptr->modes_off & MODE_M)
      strcat(buf, "m");
    if (cptr->modes_off & MODE_I)
      strcat(buf, "i");
    if (cptr->modes_off & MODE_L)
      strcat(buf, "l");
    if (cptr->modes_off & MODE_K)
      strcat(buf, "k");
    if (cptr->modes_off & MODE_F)
      strcat(buf, "f");
  }
  if (cptr->modes_on || cptr->limit || cptr->key || cptr->forward || cptr->throttle || cptr->dline)
  {
    strcat(buf, "+");
    if (cptr->modes_on & MODE_S)
      strcat(buf, "s");
#ifdef HYBRID7
    /* Add mode A -Janos */
    if (cptr->modes_on & MODE_A)
      strcat(buf, "a");
#endif /* HYBRID7 */
    if (cptr->modes_on & MODE_P)
      strcat(buf, "p");
    if (cptr->modes_on & MODE_N)
      strcat(buf, "n");
    if (cptr->modes_on & MODE_T)
      strcat(buf, "t");
    if (cptr->modes_on & MODE_C)
      strcat(buf, "c");
    if (cptr->modes_on & MODE_G)
      strcat(buf, "g");
    if (cptr->modes_on & MODE_R)
      strcat(buf, "r");
    if (cptr->modes_on & MODE_Z)
      strcat(buf, "z");
#if 0
    if (cptr->modes_on & MODE_CAPF)
      strcat(buf, "F");
#endif
    if (cptr->modes_on & MODE_CAPP)
      strcat(buf, "P");
    if (cptr->modes_on & MODE_CAPQ)
      strcat(buf, "Q");
    if (cptr->modes_on & MODE_CAPL)
      strcat(buf, "L");
    if (cptr->modes_on & MODE_CAPR)
      strcat(buf, "R");
    if (cptr->modes_on & MODE_M)
      strcat(buf, "m");
    if (cptr->modes_on & MODE_I)
      strcat(buf, "i");
    if (cptr->limit)
      strcat(buf, "l");
    if (cptr->key)
      strcat(buf, "k");
    if (cptr->forward)
      strcat(buf, "f");
    if (cptr->throttle)
      strcat(buf, "J");
    if (cptr->dline)
      strcat(buf, "D");
  }

 if (buf[0])
    notice(n_ChanServ, lptr->nick,
      "   Mode Lock: %s",
      buf);
} /* c_info() */

/*
c_clear()
  Clear modes/bans/ops/halfops/voices from a channel
*/

static void
c_clear(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Command *cmdptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002CLEAR <channel> {OPS|"
#ifdef HYBRID7
      /* Allow halfops for hybrid7 to be cleared, too -kre */
      "HALFOPS|"
#endif /* HYBRID7 */
      "VOICES|MODES|BANS|"
#ifdef GECOSBANS
      "GECOSBANS|"
#endif /* GECOSBANS */
      "ALL|USERS}\002");
    notice(n_ChanServ, lptr->nick, ERR_MORE_INFO,
        n_ChanServ, "CLEAR");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick, ERR_CH_NOT_REGGED, av[1]);
    return;
  }

  if (!HasAccess(cptr, lptr, CA_CMDCLEAR))
  {
    notice(n_ChanServ, lptr->nick, ERR_NEED_ACCESS,
      cptr->access_lvl[CA_CMDCLEAR], "CLEAR", cptr->name);
    RecordCommand("%s: %s!%s@%s failed CLEAR [%s] %s",
      n_ChanServ, lptr->nick, lptr->username, lptr->hostname,
      cptr->name, av[2]);
    return;
  }

  cmdptr = GetCommand(clearcmds, av[2]);

  if (cmdptr && (cmdptr != (struct Command *) -1))
  {
    RecordCommand("%s: %s!%s@%s CLEAR [%s] [%s]",
      n_ChanServ, lptr->nick, lptr->username,
      lptr->hostname, cptr->name, cmdptr->cmd);

    /* call cmdptr->func to execute command */
    (*cmdptr->func)(lptr, nptr, ac, av);
  }
  else
  {
    /* the option they gave was not valid */
    notice(n_ChanServ, lptr->nick,
      "%s switch [\002%s\002]",
      (cmdptr == (struct Command *) -1) ? "Ambiguous" : "Unknown",
      av[2]);
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "CLEAR");
    return;
  }

  notice(n_ChanServ, lptr->nick,
    "[%s] have been cleared on [\002%s\002]",
    cmdptr->cmd,
    cptr->name);
} /* c_clear() */

static void
c_clear_ops(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChannelUser *cuser;
  char *ops;
  struct Channel *chptr;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active", av[1]);
    return;
  }

  ops = (char *) MyMalloc(sizeof(char));
  ops[0] = '\0';
  for (cuser = chptr->firstuser; cuser; cuser = cuser->next)
  {
    if (FindService(cuser->lptr) ||
        !(cuser->flags & CH_OPPED))
      continue;

    ops = (char *) MyRealloc(ops, strlen(ops)
        + strlen(cuser->lptr->nick) + (2 * sizeof(char)));
    strcat(ops, cuser->lptr->nick);
    strcat(ops, " ");
  }

  SetModes(n_ChanServ, 0, 'o', chptr, ops);

  MyFree(ops);
} /* c_clear_ops() */

#ifdef HYBRID7
/* Clear halfops on channel -Janos */
static void c_clear_hops(struct Luser *lptr, struct NickInfo *nptr, int
    ac, char **av)
{
  struct ChannelUser *cuser;
  char *hops;
  struct Channel *chptr;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active", av[1]);
    return;
  }

  hops = (char *) MyMalloc(sizeof(char));
  hops[0] = '\0';
  for (cuser = chptr->firstuser; cuser; cuser = cuser->next)
  {
    if (FindService(cuser->lptr) ||
        !(cuser->flags & CH_HOPPED))
    continue;

    hops = (char *) MyRealloc(hops, strlen(hops)
        + strlen(cuser->lptr->nick) + (2 * sizeof(char)));
    strcat(hops, cuser->lptr->nick);
    strcat(hops, " ");
  }

  SetModes(n_ChanServ, 0, 'h', chptr, hops);

  MyFree(hops);
} /* c_clear_hops() */
#endif /* HYBRID7 */

static void
c_clear_voices(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct Channel *chptr;
  struct ChannelUser *cuser;
  char *voices;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      av[1]);
    return;
  }

  voices = (char *) MyMalloc(sizeof(char));
  voices[0] = '\0';
  for (cuser = chptr->firstuser; cuser; cuser = cuser->next)
  {
    if (FindService(cuser->lptr) ||
        !(cuser->flags & CH_VOICED))
      continue;

    voices = (char *) MyRealloc(voices, strlen(voices) + strlen(cuser->lptr->nick) + (2 * sizeof(char)));
    strcat(voices, cuser->lptr->nick);
    strcat(voices, " ");
  }

  SetModes(n_ChanServ, 0, 'v', chptr, voices);

  MyFree(voices);
} /* c_clear_voices() */

static void
c_clear_modes(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  char modes[MAXLINE];
  struct Channel *chptr;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      av[1]);
    return;
  }

  strcpy(modes, "-");

  if (chptr->modes & MODE_S)
    strcat(modes, "s");
#ifdef HYBRID7
  /* Support mode A -Janos */
  if (chptr->modes & MODE_A)
    strcat(modes, "a");  
#endif /* HYBRID7 */
  if (chptr->modes & MODE_P)
    strcat(modes, "p");
  if (chptr->modes & MODE_N)
    strcat(modes, "n");
  if (chptr->modes & MODE_T)
    strcat(modes, "t");
  if (chptr->modes & MODE_C)
    strcat(modes, "c");
  if (chptr->modes & MODE_G)
    strcat(modes, "g");
  if (chptr->modes & MODE_M)
    strcat(modes, "m");
  if (chptr->modes & MODE_I)
    strcat(modes, "i");
  if (chptr->modes & MODE_R)
    strcat(modes, "R");
  if (chptr->modes & MODE_Z)
    strcat(modes, "Z");
#if 0
  if (chptr->modes & MODE_CAPF && lptr->umodes & UMODE_CAPX)
    strcat(modes, "F");
#endif
  if (chptr->modes & MODE_CAPP && lptr->umodes & UMODE_P)
    strcat(modes, "P");
  if (chptr->modes & MODE_CAPQ)
    strcat(modes, "Q");
  if (chptr->modes & MODE_CAPL && lptr->umodes & UMODE_CAPX)
    strcat(modes, "L");
  if (chptr->modes & MODE_CAPR)
    strcat(modes, "R");
  if (chptr->limit)
    strcat(modes, "l");
  if (chptr->key)
  {
    strcat(modes, "k ");
    strcat(modes, chptr->key);
  }
  if (chptr->forward)
  {
    strcat(modes, "f ");
    strcat(modes, chptr->forward);
  }
  if (chptr->throttle)
  {
    strcat(modes, "J ");
    strcat(modes, chptr->throttle);
  }
  if (chptr->dline && lptr->umodes & UMODE_J)
  {
    strcat(modes, "D ");
    strcat(modes, chptr->dline);
  }

  toserv(":%s MODE %s %s\n",
    n_ChanServ,
    chptr->name,
    modes);

  UpdateChanModes(Me.csptr, n_ChanServ, chptr, modes);
} /* c_clear_modes() */

static void
c_clear_bans(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct Channel *chptr;
  struct ChannelBan *bptr;
  char *bans;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      av[1]);
    return;
  }

  bans = (char *) MyMalloc(sizeof(char));
  bans[0] = '\0';

  for (bptr = chptr->firstban; bptr; bptr = bptr->next)
  {
    bans = (char *) MyRealloc(bans, strlen(bans) + strlen(bptr->mask) + (2 * sizeof(char)));
    strcat(bans, bptr->mask);
    strcat(bans, " ");
  }

  SetModes(n_ChanServ, 0, 'b', chptr, bans);

  MyFree(bans);
} /* c_clear_bans() */

static void
c_clear_users(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct Channel *chptr;
  struct ChannelUser *cuser;
  char *knicks,
       *ops; /* deop before removing */
  char reason[200];
  int wasi;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      av[1]);
    return;
  }

  knicks = (char *) MyMalloc(sizeof(char));
  knicks[0] = '\0';
  ops = (char *) MyMalloc(sizeof(char));
  ops[0] = '\0';

  for (cuser = chptr->firstuser; cuser; cuser = cuser->next)
  {
    if (FindService(cuser->lptr))
      continue;

    if (cuser->flags & CH_OPPED)
    {
      ops = (char *) MyRealloc(ops, strlen(ops)
          + strlen(cuser->lptr->nick) + (2 * sizeof(char)));
      strcat(ops, cuser->lptr->nick);
      strcat(ops, " ");
    }

    knicks = (char *) MyRealloc(knicks, strlen(knicks)
        + strlen(cuser->lptr->nick) + (2 * sizeof(char)));
    strcat(knicks, cuser->lptr->nick);
    strcat(knicks, " ");
  }

  SetModes(n_ChanServ, 0, 'o', chptr, ops);
  MyFree(ops);

  wasi = chptr->modes & MODE_I;
  if (!wasi)
  {
    toserv(":%s MODE %s +i\n",
      n_ChanServ,
      chptr->name);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, "+i");
  }

  sprintf(reason, "CLEAR USERS command from %s", lptr->nick);
  KickBan(0, n_ChanServ, chptr, knicks, reason);
  MyFree(knicks);

  if (!wasi)
  {
    toserv(":%s MODE %s -i\n",
      n_ChanServ,
      chptr->name);
    UpdateChanModes(Me.csptr, n_ChanServ, chptr, "-i");
  }
} /* c_clear_users() */

static void
c_clear_all(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  c_clear_ops(lptr, nptr, ac, av);
#ifdef HYBRID7
  /* Delete halfops, too -Janos */
  c_clear_hops(lptr, nptr, ac, av);
#endif /* HYBRID7 */
  c_clear_voices(lptr, nptr, ac, av);
  c_clear_modes(lptr, nptr, ac, av);
  c_clear_bans(lptr, nptr, ac, av);
#ifdef GECOSBANS
  c_clear_gecos_bans( lptr, nptr, ac, av);
#endif /* GECOSBANS */
} /* c_clear_all() */

#ifdef GECOSBANS
static void c_clear_gecos_bans(struct Luser *lptr, struct NickInfo *nptr,
    int ac, char **av)
{
  struct Channel *chptr;
  struct ChannelGecosBan *bptr;
  char *bans;

  if (!(chptr = FindChannel(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is not active",
      av[1]);
    return;
  }

  bans = (char *)MyMalloc(sizeof(char));
  bans[0] = '\0';

  for (bptr = chptr->firstgecosban; bptr; bptr = bptr->next)
  {
    bans = (char *)MyRealloc(bans, strlen(bans) 
        + strlen(bptr->mask) + (2 * sizeof(char)));
    strcat(bans, bptr->mask);
    strcat(bans, " ");
  }

  SetModes(n_ChanServ, 0, 'd', chptr, bans);

  MyFree(bans);
} /* c_clear_gecos_bans() */
#endif /* GECOSBANS */

#ifdef EMPOWERADMINS

/*
c_forbid()
  Prevent anyone from using channel av[1]
*/
static void
c_forbid(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Channel *chptr;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002FORBID <channel>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "FORBID");
    return;
  }

  RecordCommand("%s: %s!%s@%s FORBID [%s]",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1]);

  o_Wallops("Forbid from %s!%s@%s for channel [%s]",
    lptr->nick, lptr->username, lptr->hostname,
    av[1] );

  if ((cptr = FindChan(av[1])))
  {
    /* the channel is already registered */
    if (cptr->flags & CS_FORBID)
    {
      notice(n_ChanServ, lptr->nick,
        "The channel [\002%s\002] is already forbidden",
        cptr->name);
      return;
    }

    cptr->flags |= CS_FORBID;
  }
  else
  {
    if (av[1][0] != '#')
    {
      notice(n_ChanServ, lptr->nick,
        "[\002%s\002] is an invalid channel",
        av[1]);
      return;
    }

    /* Create channel - it didn't exist -kre */
    cptr = MakeChan();
    cptr->name = MyStrdup(av[1]);
    cptr->created = current_ts;
    cptr->flags |= CS_FORBID;
    SetDefaultALVL(cptr);
    AddChan(cptr);
  }

  if ((chptr = FindChannel(av[1])))
  {
    /* There are people in the channel - must remove them */
    cs_join(cptr);
  }

  notice(n_ChanServ, lptr->nick,
    "The channel [\002%s\002] is now forbidden", av[1]);
} /* c_forbid() */

/*
 * c_unforbid()
 * Removes effects of c_forbid(), admin level required
 */
static void c_unforbid(struct Luser *lptr, struct NickInfo *nptr, int ac,
    char **av)

{
  struct ChanInfo *cptr;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick, "Syntax: \002UNFORBID <channel>\002");
    notice(n_ChanServ, lptr->nick, ERR_MORE_INFO, n_ChanServ, "UNFORBID");
    return;
  }

  RecordCommand("%s: %s!%s@%s UNFORBID [%s]",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname, av[1]);

  o_Wallops("Unforbid from %s!%s@%s for channel [%s]",
    lptr->nick, lptr->username, lptr->hostname, av[1]);

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] is an invalid or non-existing channel", av[1]);
    return;
  }

  if (!cptr->password)
  {
    struct Channel *chptr;

    /* Well, it has empty fields - it was either from old forbid() code,
     * or AddChan() made nickname from new forbid() - either way it is
     * safe to delete it -kre */
    chptr = FindChannel(cptr->name);

    if (IsChannelMember(chptr, Me.csptr))
      cs_part(chptr);

    DeleteChan(cptr);

    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] has been dropped",
      av[1]);
  }
  else
  {
    cptr->flags &= ~CS_FORBID;

    notice(n_ChanServ, lptr->nick,
      "The channel [\002%s\002] is now unforbidden", av[1]);
  }
} /* c_unforbid() */

/*
c_setpass()
  Change the channel (av[1]) password to av[2]
*/

static void
c_setpass(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002SETPASS <channel> <password>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "SETPASS");
    return;
  }

  RecordCommand("%s: %s!%s@%s SETPASS [%s] ...",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1]);

  o_Wallops("SETPASS from %s!%s@%s for channel [%s]",
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1] );

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!ChangeChanPass(cptr, av[2]))
  {
    notice(n_ChanServ, lptr->nick,
      "Password change failed");
    RecordCommand("%s: Failed to change password for [%s] to [%s] (SETPASS)",
      n_ChanServ,
      cptr->name,
      av[2]);
    return;
  }

  notice(n_ChanServ, lptr->nick,
    "Contact password for %s has been changed to [\002%s\002]",
    cptr->name,
    av[2]);
} /* c_setpass() */

/*
c_status()
  Give the current access level of nick av[2] on channel av[1]
*/

static void
c_status(struct Luser *lptr, struct NickInfo *nptr, int ac, char **av)

{
  struct ChanInfo *cptr;
  struct Luser *tmpuser;

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002STATUS <channel> <nickname>\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "STATUS");
    return;
  }

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (!(tmpuser = FindClient(av[2])))
  {
    notice(n_ChanServ, lptr->nick,
      "[\002%s\002] is not currently online",
      av[2]);
    return;
  }

  notice(n_ChanServ, lptr->nick,
    "%s has an access level of [\002%d\002] on %s",
    tmpuser->nick,
    GetAccess(cptr, tmpuser),
    cptr->name);

  RecordCommand("%s: %s!%s@%s STATUS [%s] %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    cptr->name,
    tmpuser->nick);
} /* c_status() */

/*
 * c_noexpire()
 * Prevent channel av[1] from expiring
 */
static void c_noexpire(struct Luser *lptr, struct NickInfo *nptr, int ac,
    char **av)
{
  struct ChanInfo *cptr;

  if (ac < 2)
  {
    notice(n_ChanServ, lptr->nick,
      "Syntax: \002NOEXPIRE <channel> {ON|OFF}\002");
    notice(n_ChanServ, lptr->nick,
      ERR_MORE_INFO,
      n_ChanServ,
      "NOEXPIRE");
    return;
  }

  RecordCommand("%s: %s!%s@%s NOEXPIRE [%s] %s",
    n_ChanServ,
    lptr->nick,
    lptr->username,
    lptr->hostname,
    av[1],
    (ac < 3) ? "" : StrToupper(av[2]));

  if (!(cptr = FindChan(av[1])))
  {
    notice(n_ChanServ, lptr->nick,
      ERR_CH_NOT_REGGED,
      av[1]);
    return;
  }

  if (ac < 3)
  {
    notice(n_ChanServ, lptr->nick,
      "NoExpire for channel %s is [\002%s\002]",
      cptr->name,
      (cptr->flags & CS_NOEXPIRE) ? "ON" : "OFF");
    return;
  }

  if (!irccmp(av[2], "ON"))
  {
    cptr->flags |= CS_NOEXPIRE;
    notice(n_ChanServ, lptr->nick,
      "Toggled NoExpire for channel %s [\002ON\002]",
      cptr->name);
    return;
  }

  if (!irccmp(av[2], "OFF"))
  {
    cptr->flags &= ~CS_NOEXPIRE;
    notice(n_ChanServ, lptr->nick,
      "Toggled NoExpire for channel %s [\002OFF\002]",
      cptr->name);
    return;
  }

  /* user gave an unknown param */
  notice(n_ChanServ, lptr->nick,
    "Syntax: \002NOEXPIRE <channel> {ON|OFF}\002");
  notice(n_ChanServ, lptr->nick,
    ERR_MORE_INFO,
    n_ChanServ,
    "NOEXPIRE");
} /* c_noexpire() */

/*
 * Clears noexpire modes setup on channels. Code taken from IrcBg and
 * slightly modified. -kre
 */
void c_clearnoexpire(struct Luser *lptr, struct NickInfo *nptr, int ac,
    char **av)
{
  int ii;
  struct ChanInfo *cptr;

  RecordCommand("%s: %s!%s@%s CLEARNOEXP",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname);

  for (ii = 0; ii < CHANLIST_MAX; ii++)
    for (cptr = chanlist[ii]; cptr; cptr = cptr->next)
        cptr->flags &= ~CS_NOEXPIRE;

  notice(n_ChanServ, lptr->nick,
      "All noexpire flags are cleared." );

} /* c_clearnoexpire() */

#if 0
/* 
 * c_fixts()
 * Traces chanels for TS < TS_MAX_DELTA (either defined in settings or as
 * av[1]. Then such channel is found, c_clear_users() is used to sync
 * channel -kre
 */
static void c_fixts(struct Luser *lptr, struct NickInfo *nptr, int ac,
    char **av)
{
  int tsdelta = 0;
  time_t now = 0;
  struct Channel *cptr = NULL;
  char dMsg[] = "Detected channel #\002%s\002 with TS %d "
                "below TS_MAX_DELTA %d";
  int acnt = 0;
  char **arv = NULL;
  char line[MAXLINE];
  
  if (ac < 2)
    tsdelta = MaxTSDelta;
  else
    tsdelta = atoi(av[1]);

  now = current_ts;

  /* Be paranoid */
  if (tsdelta <= 0)
  {
    notice(n_ChanServ, lptr->nick,
        "Wrong TS_MAX_DELTA specified, using default of 8w");
    tsdelta = 4838400; /* 8 weeks */
  }

  for (cptr = ChannelList; cptr; cptr = cptr->next)
  {
    if ((now - cptr->since) >= tsdelta)
    {
      SendUmode(OPERUMODE_Y, dMsg, cptr->name, cptr->since, tsdelta);
      notice(n_ChanServ, lptr->nick, dMsg, cptr->name, cptr->since,
          tsdelta);
      putlog(LOG1, "%s: Bogus TS channel: [%s] (TS=%d)", 
          n_ChanServ, cptr->name, cptr->since);

      /* Use c_clear_users() for fixing channel TS. */
      ircsprintf(line, "FOOBAR #%s", cptr->name);
      acnt = SplitBuf(line, &arv);
      c_clear_users(lptr, nptr, acnt, arv); 

      MyFree(arv);
    }
  }
} /* c_fixts */

/*
 * c_resetlevels
 *
 * Resets levels of _all_ channels to DefaultAccess. -kre
 */
static void c_resetlevels(struct Luser *lptr, struct NickInfo *nptr, int ac,
        char **av)
{
  int ii;
  struct ChanInfo *cptr;

  RecordCommand("%s: %s!%s@%s RESETLEVELS",
    n_ChanServ, lptr->nick, lptr->username, lptr->hostname);

  for (ii = 0; ii < CHANLIST_MAX; ii++)
    for (cptr = chanlist[ii]; cptr; cptr = cptr->next)
        SetDefaultALVL(cptr);

  notice(n_ChanServ, lptr->nick,
      "All channels have been reset to default access levels." );

} /* c_resetlevels */
#endif
#endif /* EMPOWERADMINS */

/*
 * Routine to set Channel level to default. This fixes a *huge* bug
 * reported by KrisDuv <krisduv2000@yahoo.fr>.
 */
void SetDefaultALVL(struct ChanInfo *cptr)
{
  int i;

  if (cptr->access_lvl == NULL)  
    cptr->access_lvl = MyMalloc(sizeof(int) * CA_SIZE);

  for (i = 0; i < CA_SIZE; ++i)
    cptr->access_lvl[i] = DefaultAccess[i];
}

#endif /* defined(NICKSERVICES) && defined(CHANNELSERVICES) */
