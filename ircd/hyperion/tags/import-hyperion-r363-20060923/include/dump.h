
/************************************************************************
 *   IRC - Internet Relay Chat, include/dump.h
 *   This file is copyright (C) 2002 Andrew Suffield
 *                                    <asuffield@freenode.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "config.h"

typedef int dumper(char *, const char *, const char *, int);

/* A dumper is repeatedly called with the same parameters until it
 * returns false. The first argument (output) is the dumped data each
 * time, or ignored if false returned. 
 *
 * A dumper will keep being called until it returns false, unless the
 * ircd exits, so can use global state (not pretty, but simple)
 *
 * The output buffer is 1024 characters in length
 */
