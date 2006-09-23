/************************************************************************
 *   IRC - Internet Relay Chat, src/feature.c
 *   This file is copyright (C) 2005 Jilles Tjoelker
 *                                    <jilles@stack.nl>
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

#include "client.h"
#include "ircd.h"
#include "common.h"
#include "feature.h"
#include "numeric.h"
#include "irc_string.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "caps.h"

#include <stdlib.h>
#include <string.h>

static char *identd_prefix_p = GlobalSetOptions.identd_prefix;
static char *noidentd_prefix_p = GlobalSetOptions.noidentd_prefix;
static char *identifyservice_p = GlobalSetOptions.identifyservice;
static char *identifycommand_p = GlobalSetOptions.identifycommand;
static char *noidprivmsg_notice_p = GlobalSetOptions.noidprivmsg_notice;
static char *umodeban_reason_p = GlobalSetOptions.umodeban_reason;

#define FEATURE_NICKPREFIX 0
#define FEATURE_IDENTD_PREFIX 1
#define FEATURE_NOIDENTD_PREFIX 2
#define FEATURE_IDENTIFYSERVICE 3
#define FEATURE_IDENTIFYCOMMAND 4
#define FEATURE_DOPINGOUT 5
#define FEATURE_NOIDPRIVMSG 6
#define FEATURE_NOIDPRIVMSG_NOTICE 7
#define FEATURE_UMODEBAN_LENGTH 8
#define FEATURE_UMODEBAN_CONNECTED 9
#define FEATURE_UMODEBAN_CHANNELS 10
#define FEATURE_UMODEBAN_REASON 11
#define FEATURE_LAST 13

/* In contrast to the config.h values in /info, these are lower case */
struct
{
	const char *name;
	const char *desc;
	int *intvalue;
	char **stringvalue;
} featurenames[] =
{
	{ "nickprefix", "Prefix for unregistered nicks", &GlobalSetOptions.nickprefix, NULL },
	{ "identd_prefix", "Username prefix (identd)", NULL, &identd_prefix_p },
	{ "noidentd_prefix", "Username prefix (no identd)", NULL, &noidentd_prefix_p },
	{ "identifyservice", "Nickname to identify to with server password", NULL, &identifyservice_p },
	{ "identifycommand", "Command to identify to identifyservice", NULL, &identifycommand_p },
	{ "dopingout", "Disconnect clients on ping timeout", &GlobalSetOptions.dopingout, NULL },
	{ "noidprivmsg", "Do not allow unidentified clients to use PRIVMSG", &GlobalSetOptions.noidprivmsg, NULL },
	{ "noidprivmsg_notice", "Notice to tell people who are affected by F:noidprivmsg", NULL, &noidprivmsg_notice_p },
	{ "umodeban_length", "Length of expiry of /UMODE triggered K:lines in minutes, or 0 to disable", &GlobalSetOptions.umodeban_length, NULL },
	{ "umodeban_connected", "Maximum duration which a client can be connected for before /UMODE triggered K:lines no longer take effect", &GlobalSetOptions.umodeban_connected, NULL },
	{ "umodeban_channels", "For /UMODE triggered K:lines, whether or not to require zero channels joined before banning", &GlobalSetOptions.umodeban_channel, NULL },
	{ "umodeban_reason", "Reason on /UMODE triggered K:lines", NULL, &umodeban_reason_p },
	{ NULL, NULL, NULL, NULL }
};

void set_feature(const char *name, const char *value)
{
	int f = 0;
	int n;
	int *intp;
	char **stringp;

	while (featurenames[f].name != NULL)
	{
		if (!irccmp(featurenames[f].name, name))
			break;
		f++;
	}

	intp = featurenames[f].intvalue;
	stringp = featurenames[f].stringvalue;

	switch (f)
	{
		case FEATURE_NICKPREFIX:
			n = atoi(value) & 255;
			if (n == 1)
				n = '~';
			if (n == 0)
				*intp = n;
			else if (IsNickChar(n) && n != '-' && !IsDigit(n))
				*intp = n;
			else
			{
				logprintf(L_ERROR, "F:NICKPREFIX cannot be set to %i (%c)",
						n, n);
			}
			break;
		case FEATURE_IDENTD_PREFIX:
			strncpy_irc(GlobalSetOptions.identd_prefix, value, sizeof(GlobalSetOptions.identd_prefix));
			if (strchr(GlobalSetOptions.identd_prefix, '*') ||
				strchr(GlobalSetOptions.identd_prefix, '?') ||
				strchr(GlobalSetOptions.identd_prefix, '!') ||
				strchr(GlobalSetOptions.identd_prefix, '@'))
			{
				logprintf(L_ERROR, "Invalid F:identd_prefix \"%s\", using \"\"", GlobalSetOptions.identd_prefix);
				GlobalSetOptions.identd_prefix[0] = '\0';
			}
			break;
		case FEATURE_NOIDENTD_PREFIX:
			strncpy_irc(GlobalSetOptions.noidentd_prefix, value, sizeof(GlobalSetOptions.noidentd_prefix));
			if (strchr(GlobalSetOptions.noidentd_prefix, '*') ||
				strchr(GlobalSetOptions.noidentd_prefix, '?') ||
				strchr(GlobalSetOptions.noidentd_prefix, '!') ||
				strchr(GlobalSetOptions.noidentd_prefix, '@'))
			{
				logprintf(L_ERROR, "Invalid F:noidentd_prefix \"%s\", using \"~\"", GlobalSetOptions.noidentd_prefix);
				strcpy(GlobalSetOptions.identd_prefix, "~");
			}
			break;
		case FEATURE_IDENTIFYSERVICE:
			strncpy_irc(GlobalSetOptions.identifyservice, value, sizeof(GlobalSetOptions.identifyservice));
			break;
		case FEATURE_IDENTIFYCOMMAND:
			strncpy_irc(GlobalSetOptions.identifycommand, value, sizeof(GlobalSetOptions.identifycommand));
			break;
		case FEATURE_DOPINGOUT:
		case FEATURE_NOIDPRIVMSG:
			n = atoi(value) ? 1 : 0;
			*intp = n;
			break;
		case FEATURE_NOIDPRIVMSG_NOTICE:
			strncpy_irc(GlobalSetOptions.noidprivmsg_notice, value, sizeof(GlobalSetOptions.noidprivmsg_notice));
			break;
		case FEATURE_UMODEBAN_LENGTH:
		case FEATURE_UMODEBAN_CONNECTED:
		case FEATURE_UMODEBAN_CHANNELS:
			*intp = atoi(value);
			break;
		case FEATURE_UMODEBAN_REASON:
			strncpy_irc(GlobalSetOptions.umodeban_reason, value, sizeof(GlobalSetOptions.umodeban_reason));
			break;
		default:
			logprintf(L_ERROR, "Unknown F:line feature %s", name);
	}
}

void dump_features(struct Client *client_p)
{
	int f = 0;

	while (featurenames[f].name != NULL)
	{
		if (featurenames[f].intvalue)
			sendto_one(client_p, ":%s %d %s :%-30s %-5d [%-30s]",
					me.name, RPL_INFO, client_p->name,
					featurenames[f].name,
					*featurenames[f].intvalue,
					featurenames[f].desc);
		else if (featurenames[f].stringvalue)
			sendto_one(client_p, ":%s %d %s :%-30s %-5s [%-30s]",
					me.name, RPL_INFO, client_p->name,
					featurenames[f].name,
					*featurenames[f].stringvalue,
					featurenames[f].desc);
		else
			sendto_one(client_p, ":%s %d %s :%-30s %-5s [%-30s]",
					me.name, RPL_INFO, client_p->name,
					featurenames[f].name,
					"???",
					featurenames[f].desc);
		f++;
	}
}
