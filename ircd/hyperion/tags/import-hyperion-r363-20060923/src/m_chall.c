/************************************************************************
 *   IRC - Internet Relay Chat, src/m_chall.c
 *   April 6. 2001, einride.
 */

#include "config.h"

#ifdef CHALLENGERESPONSE

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
#include "umodes.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

int m_chall(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  static char blank[] = "";
  char* salt = parc > 4 ? parv[4] : blank;
  char* chall = parc > 3 ? parv[3] : blank;
  char* remote_host = parc > 2 ? parv[2] : blank;
  char* host = parc > 1 ? parv[1] : blank;
  struct ConfItem *n_conf, *c_conf;
  struct Client* acptr;

  if (IsPerson(sptr))
    {
      if (IsServer(cptr))
        {
          sendto_ops_flag(UMODE_SERVNOTICE,"CHALL command from remote user %s -- %s is a hacked server",
				 get_client_name(sptr,HIDE_IP),
				 get_client_name(cptr,HIDE_IP));
        }
      else
        {
          sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                     me.name, parv[0], "CHALL");
        }
      return 0;
    }

  /* If we know this, we shouldn't be getting a chall */
  if (IsPerson(cptr) || IsServer(cptr)) 
    return 0;

  /* What are they trying to auth against, is it me? */
  if (irccmp(remote_host, me.name) != 0)
    {
      sendto_one(cptr, "ERROR :I am %s, not %s", me.name, remote_host);
      sendto_ops_flag(UMODE_SERVCONNECT, "Server claiming to be %s challenged thinking I was %s, rejected", host, remote_host);
      return exit_client(cptr, cptr, &me, "Sorry, wrong number");
    }

  if ((acptr = find_server(host)))
    {
      sendto_one(cptr,"ERROR :Server %s already exists", host);
      if (!(acptr->from->caps & CAP_SERVICES)) /* If it's connected to services, it's actually a server jupe. Silently ignore. */
	sendto_ops_flag(UMODE_EXTERNAL, "Challenge for %s rejected, server already exists",
			host);
      return exit_client(cptr, cptr, &me, "Server already exists");
    }

  sendto_ops_flag(UMODE_SERVCONNECT, "CHALL received, trying to use N:line for %s", host);

  if (parc < 5)
    {
      sendto_one(cptr, "ERROR: Invalid challenge");
      sendto_ops_flag(UMODE_SERVCONNECT, "Invalid CHALL from remote host for %s", host);
      return 0;
    }

  /* See if we got c and n lines. */
  if (!(n_conf = find_conf_by_name(host, CONF_NOCONNECT_SERVER))) {
    sendto_ops_flag(UMODE_SERVCONNECT, "Challenge rejected. No N line for server %s", host);
    logprintf(L_NOTICE, "Challenge rejected. No N line for server %s", host);
    sendto_one(cptr, "ERROR :Access denied. No N line"); 
    return exit_client(cptr, cptr, cptr, "Access denied. No N line");    
  } 

  if (!(c_conf = find_conf_by_name(host, CONF_CONNECT_SERVER))) {
    sendto_ops_flag(UMODE_SERVCONNECT, "Challenge rejected. No C line for server %s", host);
    logprintf(L_NOTICE, "Challenge rejected. No C line for server %s", host);
    sendto_one(cptr, "ERROR :Access denied. No C line");
    return exit_client(cptr, cptr, cptr, "Access denied. No C line");    
  }
  
  /* Check if the ip matches the c/n IPs */
#ifdef IPV6
  if((memcmp((char*)cptr->ip.S_ADDR, (char*)c_conf->ipnum.S_ADDR, sizeof(struct IN_ADDR) != 0)) ||
        (memcmp((char*)cptr->ip.S_ADDR, (char*)n_conf->ipnum.S_ADDR,  sizeof(struct IN_ADDR)) != 0))
  {
#else
  if ( (cptr->ip.s_addr != c_conf->ipnum.s_addr) || (cptr->ip.s_addr != n_conf->ipnum.s_addr)) {
#endif
    sendto_ops_flag(UMODE_SERVCONNECT, "Challenge rejected. C/N line IP doesn't match connection");
    logprintf(L_NOTICE, "Challenge rejected. C/N line IP doesn't match connection");
    sendto_one(cptr, "ERROR :Access denied. C/N line don't match you"); 
    return exit_client(cptr, cptr, cptr, "Access denied. C/N line don't match");        
  }
  
  /* Ok, it got c/n lines and correct ip, lets challenge it back */
  if (!Challenged(cptr))
    cr_sendchallenge(cptr, n_conf);

  /* And send a response. */
  cr_sendresponse(cptr, c_conf, chall, salt);

  /* check for TS as in m_pass */
  if (parc > 5)
    {
      if (0 == irccmp(parv[5], "TS"))
        cptr->tsinfo = TS_DOESTS;
    }
  return 0;
}
#endif
