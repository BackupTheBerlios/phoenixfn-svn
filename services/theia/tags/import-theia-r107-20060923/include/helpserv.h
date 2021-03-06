/*
 * helpserv.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: helpserv.h,v 1.1.1.1 2001/03/03 01:48:53 wcampbel Exp $
 */

#ifndef INCLUDED_helpserv_h
#define INCLUDED_helpserv_h

#ifndef INCLUDED_config_h
#include "config.h"        /* HELPSERVICES */
#define INCLUDED_config_h
#endif

#ifdef HELPSERVICES

void hs_process(char *nick, char *command);

#endif /* HELPSERVICES */

void GiveHelp(char *serv, char *nick, char *command, int sockfd);

#endif /* INCLUDED_helpserv_h */
