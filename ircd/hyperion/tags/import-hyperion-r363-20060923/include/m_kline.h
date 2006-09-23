/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kline.h
 *   Copyright (C) 1990 Jarkko Oikarinen
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
 *
 * 
 */
#ifndef INCLUDED_m_kline_h
#define INCLUDED_m_kline_h

#include <time.h>

struct Client;
struct ConfItem;

struct PKDlines
{
  struct PKDlines* next;
  struct Client*   sptr;
  struct Client*   rcptr;
  char*            user; /* username of K/D lined user */
  char*            host; /* hostname of K/D lined user */
  char*            reason; /* reason they are K/D lined */
  time_t           when; /* when the K/D line was set */
  int              type;
  time_t           until; /* expiry time */
};

typedef struct PKDlines aPendingLine;

/*
 * This number represents the number of non-wildcard characters
 * that must be in the kline string in order for it to be
 * considered valid. "* ? ! @ ." are considered wildcard
 * characters for the kline routine.
 */

#define NONWILDCHARS 4

/* This belongs in m_unkline.h, but I'm tired */

struct unkline_record
{
  char *mask;
  time_t placed;
  struct unkline_record *next;
};

extern struct unkline_record *recorded_unklines;

extern int dline_client(struct Client *, const char *);

extern int unkline_conf(struct ConfItem *aconf);
extern int expire_ancient_klines(void);

#endif /* INCLUDED_m_kline_h */
