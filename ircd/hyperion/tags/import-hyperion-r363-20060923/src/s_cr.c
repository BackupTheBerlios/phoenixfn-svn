#include "client.h"
#include "common.h"
#include "s_log.h"
#include "irc_string.h"
#include "s_bsd.h"
#include "ircd.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "umodes.h"
#include "s_cr.h"
#include "md5crypt.h"
#include "md5.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef CHALLENGERESPONSE

void cr_sendchallenge(struct Client * cptr, struct ConfItem * n_conf) {
  /* Create a challenge using random data and sending it as a hex value.
   * Store this hex value in cptr->passwd, don't want more fields in cptr.
   * As this is sent before anything else, we pass the server name too so
   * the receiving end can look up our C/N lines.
   */
  char chall[17]="";
  unsigned int c;
  int i;
  for (i=0; i<16; i++) {
    c = (unsigned int) (16.0 * (random() / (RAND_MAX + 1.0)));
    chall[i] = (c>=10) ? (c - 10 + 'A' ) : (c + '0');
  }
  chall[16]=0;
  strncpy_irc(cptr->name, n_conf->name, HOSTLEN + 1);
  strncpy_irc(cptr->passwd, chall, PASSWDLEN + 1);
  sendto_one(cptr, "CHALL %s %s %s %s :TS", me.name, cptr->name, chall, get_salt(n_conf->passwd));
  SetChallenged(cptr);
  sendto_ops_flag(UMODE_EXTERNAL, "Sent password challenge to %s", cptr->name);
}

static void cr_hashstring(char *src, char *trg) {
  unsigned char md[16];
  MD5_CTX ctx;
  MD5Init(&ctx);
  MD5Update(&ctx, (unsigned char*)src, strlen(src));
  MD5Final(md, &ctx);
  /* Endianness fix */
  byteReverse(md, 4);
  ircsnprintf(trg, 9, "%08x", *(u_int32_t *) &md[0]);
  ircsnprintf(trg + 8, 9, "%08x", *(u_int32_t *) &md[4]);
  ircsnprintf(trg + 16, 9, "%08x", *(u_int32_t *) &md[8]);
  ircsnprintf(trg + 24, 9, "%08x", *(u_int32_t *) &md[12]);
}

/* Now modified so it doesn't hand out responses to anybody who asks for them
 *
 * If either:
 *  -- I initiated the connection (cptr->serv will be defined)
 *  -- The remote host has already responded to my challenge correctly
 * Then (and only then) do I send out a response. If the remote end initiated
 *  the connection, they must authenticate *first*, so I buffer the response
 *  in cptr->response and send it on later
 *
 *  -- asuffield
 */

void cr_sendresponse(struct Client * cptr, struct ConfItem * c_conf, char * chall, char* salt) {
  struct ConfItem* n_conf = find_conf_by_name(cptr->name, CONF_NOCONNECT_SERVER);
  char work[PASSWDLEN*2], hash[40];
  strncpy_irc(work, chall, PASSWDLEN + 1);
  strncpy_irc(work + strlen(work), c_conf->passwd ? libshadow_md5_crypt(c_conf->passwd, salt) : "", PASSWDLEN + 1);
  cr_hashstring(work, hash);
  if (!IsUnknown(cptr) || (strcmp(cptr->passwd, n_conf->passwd) == 0))
    {
      sendto_one(cptr, "RESP %s", hash);
      SetResponded(cptr);
      sendto_ops_flag(UMODE_EXTERNAL, "Sent password response to %s", cptr->name);
    }
  else
    {
      strncpy_irc(cptr->response, hash, 40);
    }
}

void cr_gotresponse(struct Client * cptr, char * resp) {
  struct ConfItem * n_conf;
  char work[PASSWDLEN*2], hash[36];
  n_conf = find_conf_by_name(cptr->name, CONF_NOCONNECT_SERVER);
  if (n_conf && cptr->passwd[0]) {
    strncpy_irc(work, cptr->passwd, PASSWDLEN + 1);
    strncpy_irc(work + strlen(work), n_conf->passwd ? n_conf->passwd : "", PASSWDLEN + 1);
    cr_hashstring(work, hash);
    if (!strcmp(resp, hash)) {
      strncpy_irc(cptr->passwd, n_conf->passwd, PASSWDLEN + 1);
      sendto_ops_flag(UMODE_SEEROUTING, "Got a good password response from %s", cptr->name);
      /* OK, do we have a response pending ourself? */
      if (cptr->response[0] && !Responded(cptr))
	{
	  sendto_one(cptr, "RESP %s", cptr->response);
	  cptr->response[0] = '\0';
	  SetResponded(cptr);
	  sendto_ops_flag(UMODE_EXTERNAL, "Sent password response to %s", cptr->name);
	}
    } else {
      sendto_ops_flag(UMODE_SEEROUTING, "Failed password response from %s", cptr->name);
    }
  }
}

#endif
