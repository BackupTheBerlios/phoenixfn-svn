/*
 * show.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: show.h,v 1.1.1.1 2001/03/03 01:48:53 wcampbel Exp $
 */

#ifndef INCLUDED_show_h
#define INCLUDED_show_h

#ifndef INCLUDED_config_h
#include "config.h"        /* NICKSERVICES, CHANNELSERVICES */
#define INCLUDED_config_h
#endif

#define  SHOW_HELP        0x000001 /* -h/-? */
#define  SHOW_TOTALCOUNT  0x000002 /* -n */
#define  SHOW_FILE        0x000004 /* -f file */
#define  SHOW_DETAIL      0x000008 /* display specific nickname(s) */

/*
 * Prototypes
 */

#ifdef NICKSERVICES

void ShowNicknames(int, char **);

#ifdef CHANNELSERVICES
void ShowChannels(int, char **);
#endif /* CHANNELSERVICES */

#endif /* NICKSERVICES */

#endif /* INCLUDED_show_h */
