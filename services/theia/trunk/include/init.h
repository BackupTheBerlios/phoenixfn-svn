/*
 * init.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id: init.h,v 1.1.1.1 2001/03/03 01:48:53 wcampbel Exp $
 */

#ifndef INCLUDED_init_h
#define INCLUDED_init_h

struct Luser;

/*
 * Prototypes
 */

void ProcessSignal(int signal);
void InitListenPorts();
void InitLists();
void InitSignals();
void PostCleanup();
void InitServs(struct Luser *servptr);

#endif /* INCLUDED_init_h */
