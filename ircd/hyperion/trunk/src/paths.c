/************************************************************************
 *   IRC - Internet Relay Chat, src/paths.c
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

/* This brings all the paths together in one place, so that
 * you don't have to rebuild the entire tree every time they change,
 * and so that they can move to a config file later
 *  -- asuffield
 */

#include "paths.h"

/* DPATH SPATH CPATH MPATH KPATH - directoy and files locations
 * Full pathnames and defaults of irc system's support files. Please note that
 * these are only the recommended names and paths. Change as needed.
 * You must define these to something, even if you don't really want them.
 *
 * DPATH = directory, (chdir() to here on startup)
 * SPATH = server executable,
 * CPATH = conf file,
 * MPATH = MOTD
 * KPATH = kline conf file
 * DLPATH = dline conf file
 *
 * OMOTD = path to MOTD for opers
 * MCPATH = path to modules.conf
 * 
 * For /restart to work, SPATH needs to be a full pathname
 * (unless "." is in your exec path). -Rodder
 *
 * No it doesn't -- asuffield
 *
 * HPATH is the opers help file, seen by opers on /quote help.
 * -Dianora
 *
 * DPATH must have a trailing /
 * Do not use ~'s in any of these paths
 *
 */
/* FNAME_USERLOG and FNAME_OPERLOG - logs of local USERS and OPERS
 * Define this filename to maintain a list of persons who log
 * into this server. Logging will stop when the file does not exist.
 * Logging will be disable also if you do not define this.
 * FNAME_USERLOG just logs user connections, FNAME_OPERLOG logs every
 * successful use of /oper.  These are either full paths or files within DPATH.
 *
 * These need to be defined if you want to use SYSLOG logging, too.
 */

#define DPATH    PREFIX "/"
#define SPATH    "sbin/hyperion-ircd"
#define CPATH    "etc/dancer-ircd/ircd.conf"
#define MPATH    "etc/dancer-ircd/motd"
#define HPATH    "etc/dancer-ircd/ohelp"
#define OPATH    "etc/dancer-ircd/omotd"
#define LPATH    "var/log/dancer-ircd/ircd.log"
#define HLBASE   "var/log/dancer-ircd/hash"
#define USERLOG  "var/log/dancer-ircd/user.log"
#define OPERLOG  "var/log/dancer-ircd/oper.log"
#define PPATH    "var/run/dancer-ircd/dancer-ircd.pid"
#define KPATH    "var/lib/dancer-ircd/kline.conf"
#define DLPATH   "var/lib/dancer-ircd/dline.conf"
#define MXPATH   "var/lib/dancer-ircd/ircd.max"
#define DUMPPATH "var/lib/dancer-ircd/dump"

const char *work_dir = DPATH;
const char *config_file = CPATH;
const char *my_binary = SPATH;
const char *motd_file = MPATH;
const char *main_log_file = LPATH;
const char *pid_file = PPATH;
const char *oper_help_file = HPATH;
const char *oper_motd_file = OPATH;
const char *kline_config_file = KPATH;
const char *dline_config_file = DLPATH;
const char *max_client_file = MXPATH;
const char *user_log_file = USERLOG;
const char *oper_log_file = OPERLOG;
const char *hash_log_file_base = HLBASE;
const char *default_dump_file = DUMPPATH;
