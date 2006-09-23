/************************************************************************
 *   IRC - Internet Relay Chat, include/s_user.h
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
 *
 */

#include "config.h"
#include "umodes.h"

#include <stdarg.h>

FLAG_ITEM user_mode_table[] =
{ 
  {UMODE_USER_AUSPEX, 'a'},
  {UMODE_AUSPEX, 'A'},
  {UMODE_BOTS,  'b'},
  {UMODE_CHANGEOTHER, 'B'},
  {UMODE_CCONN, 'c'},
  {UMODE_NOCTCP, 'C'},
  {UMODE_DEBUG, 'd'},
  {UMODE_DIE, 'D'},
  {UMODE_IDENTIFIED, 'e'},
  {UMODE_BLOCK_NOTID, 'E'},
  {UMODE_FULL,  'f'},
  {UMODE_FLUDPROOF, 'F'},
  {UMODE_GLOBAL_KILL, 'G'},
  {UMODE_HIGHPRIORITY, 'h'},
  {UMODE_REHASH, 'H'},
  {UMODE_INVISIBLE, 'i'},
  {UMODE_NOINVITE, 'I'},
  {UMODE_AUTODLINE, 'j'},
  {UMODE_SKILL, 'k'},
  {UMODE_KILL, 'K'},
#ifdef CHANNEL_CREATION_NOTICE
  {UMODE_CHCREATE, 'l'},
#endif
  {UMODE_FORCELUSERS, 'L'},
  {UMODE_IMMUNE, 'm'},
  {UMODE_MASSNOTICE, 'M'},
  {UMODE_NCHANGE, 'n'},
  {UMODE_ANYNICK, 'N'},
  {UMODE_OPER, 'o'},
  /*  {UMODE_LOCOP, 'O'},*/
  {UMODE_GOD,   'p'},
  {UMODE_SETNAME, 'P'},
  {UMODE_NOFORWARD, 'Q'},
  {UMODE_REJ, 'r'},
  {UMODE_REMOTE, 'R'},
  {UMODE_SERVNOTICE, 's'},
  {UMODE_REMOTEINFO, 'S'},
  {UMODE_CANLOGCHANNEL, 't'},
  {UMODE_SHOWASSTAFF, 'T'},
  {UMODE_MORECHANS, 'u'},
  {UMODE_UNKLINE, 'U'},
  {UMODE_SEEOPERPRIVS, 'v'},
  {UMODE_SEEROUTING, 'V'},
  {UMODE_WALLOP, 'w'},
  {UMODE_SENDWALLOPS, 'W'},
  {UMODE_EXTERNAL, 'x'},
  {UMODE_EXPERIMENTAL, 'X'},
  {UMODE_SPY, 'y'},
  {UMODE_SERVCONNECT, 'Y'},
  {UMODE_OPERWALL, 'z'},
  {UMODE_SENDOPERWALL, 'Z'},
  {UMODE_SEESOPERS, '0'},
  {UMODE_SEEILINES, '1'},
  {UMODE_SEEKLINES, '2'},
  {UMODE_SEEQLINES, '3'},
  {UMODE_SEESTATST, '4'},
  {UMODE_SEESTATSSERV, '5'},
  {UMODE_DONTBLOCK, '6'},
  {UMODE_TESTLINE, '9'},
  {UMODE_GRANT, '*'},
  {UMODE_FREESPOOF, '@'},
  {0, 0}
};

int user_modes_from_c_to_bitmask[256];
char umode_list[256];
user_modes user_umodes, null_umodes;

void init_umodes(void)
{
  unsigned int i, field, bit;
  char *p = umode_list;
  /* Fill out the reverse umode map */
  for (i = 0; user_mode_table[i].letter; i++)
    {
      user_modes_from_c_to_bitmask[(unsigned char)user_mode_table[i].letter] = user_mode_table[i].mode;
      *p++ = user_mode_table[i].letter;
    }
  *p = '\0';
  /* Fill out the bitfield map */
  bit = 1;
  field = 0;
  for (i = 0; i < BITFIELD_SIZE; i++)
    {
      bitfield_lookup[i].bit = bit;
      bitfield_lookup[i].field = field;
      if (bit == 0x80000000)
	{
	  field++;
	  bit = 1;
	}
      else
	bit = bit << 1;
    }

  ClearBitfield(null_umodes);
  ClearBitfield(user_umodes);
  /* These are the umodes which any user can set */
  SetBit(user_umodes, UMODE_INVISIBLE);
  SetBit(user_umodes, UMODE_WALLOP);
  SetBit(user_umodes, UMODE_NOINVITE);
  SetBit(user_umodes, UMODE_BLOCK_NOTID);
  SetBit(user_umodes, UMODE_NOCTCP);
  SetBit(user_umodes, UMODE_NOFORWARD);
}

char *
umodes_as_string(user_modes *flags)
{
  static char flags_out[MAX_UMODE_COUNT];
  char *flags_ptr;
  int i;

  flags_ptr = flags_out;
  *flags_ptr = '\0';

  for (i = 0; user_mode_table[i].letter; i++ )
    {
      if (TestBit(*flags, user_mode_table[i].mode))
	*flags_ptr++ = user_mode_table[i].letter;
    }
  *flags_ptr = '\0';
  return(flags_out);
}

void
umodes_from_string(user_modes *u, char *flags)
{
  for(; *flags; flags++)
    SetBit((*u), user_modes_from_c_to_bitmask[(unsigned char)*flags]);
}

user_modes *
build_umodes(user_modes *u, int modes, ...)
{
  va_list args;
  int i;

  va_start(args, modes);
  for (i = 0; i < modes; i++)
    SetBit(*u, user_mode_table[va_arg(args, unsigned int)].mode);
  va_end(args);
  return u;
}

/* Neither argument may be null */
char *
umode_difference(user_modes *old_modes, user_modes *new_modes)
{
  static char buf[MAX_UMODE_COUNT+3];
  int i,flag;
  char *p = buf;
  char t = '=';

  /* Do this twice because we don't want +a-b+c-d and so on, we
   * want -bd+ac
   */
  for (i = 0; user_mode_table[i].letter; i++ )
    {
      flag = user_mode_table[i].mode;
      
      if (TestBit(*old_modes, flag) && !TestBit(*new_modes, flag))
	{
	  if (t != '-')
	    t = *p++ = '-';
	  *p++ = user_mode_table[i].letter;
	}
    }

  for (i = 0; user_mode_table[i].letter; i++ )
    {
      flag = user_mode_table[i].mode;

      if (!TestBit(*old_modes, flag) && TestBit(*new_modes, flag))
	{
	  if (t != '+')
	    t = *p++ = '+';
	  *p++ = user_mode_table[i].letter;
	}
    }
  *p = '\0';
  return buf;
}
