/************************************************************************
 *   IRC - Internet Relay Chat, src/m_resp.c
 *   April 6. 2001, einride.
 */
#include "m_commands.h"
#include "client.h"
#include "irc_string.h"
#include "send.h"
#include "numeric.h"
#include "ircd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_cr.h"
#include "s_serv.h"

#ifdef CHALLENGERESPONSE
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

int m_resp(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char * resp = parc > 1 ? parv[1] : NULL;
  struct ConfItem *c_conf, *n_conf;

  if (IsServer(cptr) && !MyConnect(sptr))
    {
      sendto_ops_flag(UMODE_SERVNOTICE, "RESP command from remote user %s -- %s is a hacked server",
		      sptr->name, cptr->name);
      return 0;
    }
  if (IsClient(sptr))
    {
      sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
		 me.name, parv[0], "RESP");
      return 0;
    }
  /* If we didn't challenge, kill it */
  if (!Challenged(cptr))
    {
      logprintf(L_NOTICE, "Got a RESP but never sent a CHALL? Something's fishy.");
      return exit_client(cptr, cptr, cptr, "I didn't challenge you!");
    }

  /* Ok we challenged. Handle the resp */
  cr_gotresponse(cptr, resp);

  if (!IsUnknown(cptr)) {
    /* If we're the connecting party, send CAPAB/SERVER now.
       If not, we're in the state we would be after a single PASS line
       received, and we just wait for m_server to trigger */

    c_conf = find_conf_name(cptr->confs, cptr->name, CONF_CONNECT_SERVER);
    n_conf = find_conf_name(cptr->confs, cptr->name, CONF_NOCONNECT_SERVER);  
    if (c_conf && n_conf)
      {
	send_capabilities(cptr, (c_conf->flags & CONF_FLAGS_ZIP_LINK));
	sendto_one(cptr, "SERVER %s 1 :%s",
		   my_name_for_link(me.name, n_conf), me.info);
      }
  }
  return 0;
}

#endif
