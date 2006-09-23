/************************************************************************
 *   IRC - Internet Relay Chat, include/s_cr.h
 *   April 7. 2001 - einride
 */

#ifndef INCLUDED_s_cr_h
#define INCLUDED_s_cr_h

#include "client.h"
#include "common.h"


#ifdef CHALLENGERESPONSE

void cr_sendchallenge(struct Client * cptr, struct ConfItem * n_conf);
void cr_sendresponse(struct Client * cptr, struct ConfItem * c_conf, char * chall, char* salt);
void cr_gotresponse(struct Client * cptr, char * resp);

#endif

#endif
