/*
 * global.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: global.h,v 1.1.1.1 2001/03/03 01:48:53 wcampbel Exp $
 */

#ifndef INCLUDED_global_h
#define INCLUDED_global_h

#ifndef INCLUDED_config_h
#include "config.h"        /* GLOBALSERVICES */
#define INCLUDED_config_h
#endif

#ifdef GLOBALSERVICES

/*
 * Prototypes
 */

void gs_process(char *nick, char *command);

#endif /* GLOBALSERVICES */

#endif /* INCLUDED_global_h */
