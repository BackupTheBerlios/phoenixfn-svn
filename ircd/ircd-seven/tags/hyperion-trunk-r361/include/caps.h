/************************************************************************
 *   IRC - Internet Relay Chat, include/caps.h
 *   This file is copyright (C) 2001 Andrew Suffield
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

#ifndef INCLUDED_caps_h
#define INCLUDED_caps_h
#include "config.h"

struct Capability
{
  const char *name;      /* name of capability */
  unsigned int cap;       /* mask value */
};

/*
 * Capability macros.
 */
#define IsCapable(x, cap)       (((x)->caps & (cap)) == cap)
#define NotCapable(x, cap)      (((x)->caps & (cap)) == 0)
#define SetCapable(x, cap)      ((x)->caps |= (cap))
#define ClearCap(x, cap)        ((x)->caps &= ~(cap))

extern struct Capability client_captab[];

#define CAP_CAP			0x00000001	/* received a CAP to begin with */
#define CAP_MARKUP		0x00000002	/* Can handle markup in RPL_MESSAGE */
#define CAP_IDENTIFY_MSG	0x00000004	/* Send +/- prefix on messages for idenfitied/not */
#define CAP_IDENTIFY_CTCP       0x00000008      /* Send +/- prefix on CTCPs */

extern const char *client_caps_as_string(int);

#endif
