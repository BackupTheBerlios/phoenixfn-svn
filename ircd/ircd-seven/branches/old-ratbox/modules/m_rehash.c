/*
 * ircd-seven: makes cows say "~oof~".
 * m_rehash.c: Re-reads the configuration file.
 *
 * Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 * Copyright (C) 1996-2002 Hybrid Development Team
 * Copyright (C) 2002-2005 ircd-ratbox development team
 * Copyright (C) 2006 Elfyn McBratney
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to:
 *
 *	Free Software Foundation, Inc.
 *	51 Franklin St
 *	Fifth Floor
 *	Boston, MA
 *	02111-1307
 *	USA
 *
 * $Id$
 */

#include "stdinc.h"
#include "client.h"
#include "channel.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "hostmask.h"
#include "reject.h"
#include "hash.h"
#include "cache.h"
#include "s_serv.h"

static int mo_rehash(struct Client *, struct Client *, int, const char **);
static int me_rehash(struct Client *, struct Client *, int, const char **);

static void rehash_bans_loc (struct Client *);
static void rehash_dns (struct Client *);
static void rehash_motd (struct Client *);
static void rehash_omotd (struct Client *);
static void rehash_tklines (struct Client *);
static void rehash_tdlines (struct Client *);
static void rehash_txlines (struct Client *);
static void rehash_tresvs (struct Client *);
static void rehash_rejectcache (struct Client *);
static void rehash_help (struct Client *);

struct Message rehash_msgtab = {
	"REHASH", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, {me_rehash, 0}, {mo_rehash, 0}}
};

mapi_clist_av1 rehash_clist[] = { &rehash_msgtab, NULL };
DECLARE_MODULE_AV1(rehash, NULL, NULL, rehash_clist, NULL, NULL, "$Revision$");

static struct hash_commands {
	const char *cmd;
	void (*handler) (struct Client * source_p);
} rehash_commands[] = {
	{"BANS",	rehash_bans_loc},
	{"DNS", 	rehash_dns},
	{"MOTD", 	rehash_motd},
	{"OMOTD", 	rehash_omotd},
	{"TKLINES", 	rehash_tklines},
	{"TDLINES", 	rehash_tdlines},
	{"TXLINES",	rehash_txlines},
	{"TRESVS",	rehash_tresvs},
	{"REJECTCACHE",	rehash_rejectcache},
	{"HELP", 	rehash_help},
	{NULL, 		NULL},
};

static void
rehash_bans_loc(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is rehashing bans",
		get_oper_name(source_p));

	rehash_bans(0);
}

static void
rehash_dns(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is rehashing DNS", 
		get_oper_name(source_p));

	/* reread /etc/resolv.conf and reopen res socket */
	restart_resolver();
}

static void
rehash_motd(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL,
		"%s is forcing re-reading of MOTD file",
		get_oper_name(source_p));

	free_cachefile(user_motd);
	user_motd = cache_file(MPATH, "ircd.motd", 0);
}

static void
rehash_omotd(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL,
		"%s is forcing re-reading of OPER MOTD file",
		get_oper_name(source_p));

	free_cachefile(oper_motd);
	oper_motd = cache_file(OPATH, "opers.motd", 0);
}

static void
rehash_tklines(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr, *next_ptr;
	int i;

	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is clearing temp klines",
		get_oper_name(source_p));

	for(i = 0; i < LAST_TEMP_TYPE; i++)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, temp_klines[i].head)
		{
			aconf = ptr->data;

			delete_one_address_conf(aconf->host, aconf);
			dlinkDestroy(ptr, &temp_klines[i]);
		}
	}
}

static void
rehash_tdlines(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr, *next_ptr;
	int i;

	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is clearing temp dlines",
		get_oper_name(source_p));

	for(i = 0; i < LAST_TEMP_TYPE; i++)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, temp_dlines[i].head)
		{
			aconf = ptr->data;

			delete_one_address_conf(aconf->host, aconf);
			dlinkDestroy(ptr, &temp_dlines[i]);
		}
	}
}

static void
rehash_txlines(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;
	dlink_node *next_ptr;

	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is clearing temp xlines",
		get_oper_name(source_p));

	DLINK_FOREACH_SAFE(ptr, next_ptr, xline_conf_list.head)
	{
		aconf = ptr->data;

		if(!aconf->hold)
			continue;

		free_conf(aconf);
		dlinkDestroy(ptr, &xline_conf_list);
	}
}

static void
rehash_tresvs(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;
	dlink_node *next_ptr;
	int i;

	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is clearing temp resvs",
		get_oper_name(source_p));

	HASH_WALK_SAFE(i, R_MAX, ptr, next_ptr, resvTable)
	{
		aconf = ptr->data;

		if(!aconf->hold)
			continue;

		free_conf(aconf);
		dlinkDestroy(ptr, &resvTable[i]);
	}
	HASH_WALK_END

	DLINK_FOREACH_SAFE(ptr, next_ptr, resv_conf_list.head)
	{
		aconf = ptr->data;

		if(!aconf->hold)
			continue;

		free_conf(aconf);
		dlinkDestroy(ptr, &resv_conf_list);
	}
}

static void
rehash_rejectcache(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL, "%s is clearing reject cache",
		get_oper_name(source_p));
	flush_reject();
}

static void
rehash_help(struct Client *source_p)
{
	sendto_realops_flags(UMODE_ALL, L_ALL,
		"%s is forcing re-reading of HELP files", 
		get_oper_name(source_p));
	clear_help_hash();
	load_help();
}

static int
do_rehash (struct Client *source_p, const char *cmd)
{
	int		i;
	static char	cmdbuf[100];

	if (cmd != NULL) {
		for (i = 0; rehash_commands[i].cmd != NULL &&
				rehash_commands[i].handler != NULL; i++) {
			if (irccmp(cmd, rehash_commands[i].cmd) == 0) {
				sendto_one(source_p, form_str(RPL_REHASHING),
					me.name, source_p->name,
					rehash_commands[i].cmd);
				rehash_commands[i].handler(source_p);
				ilog(L_MAIN, "REHASH %s From %s[%s]", cmd,
					get_oper_name(source_p),
					source_p->sockhost);
				return 0;
			}
		}

		/* No match... */
		cmdbuf[0] = '\0';
		for (i = 0; rehash_commands[i].cmd != NULL &&
				rehash_commands[i].handler != NULL; i++) {
			strlcat(cmdbuf, " ", sizeof(cmdbuf));
			strlcat(cmdbuf, rehash_commands[i].cmd, sizeof(cmdbuf));
		}

		sendto_one(source_p, ":%s NOTICE %s :rehash one of:%s",
			me.name, source_p->name, cmdbuf);
		return 0;
	}

	sendto_one(source_p, form_str(RPL_REHASHING), me.name, source_p->name,
			ConfigFileEntry.configfile);
	sendto_realops_flags(UMODE_ALL, L_ALL,
		"%s is rehashing server config file",
		get_oper_name(source_p));
	ilog(L_MAIN, "REHASH From %s[%s]", get_oper_name(source_p),
		source_p->sockhost);
	rehash(0);

	return 0;
}

static inline int
contains_generic_wild (const char *str)
{
	const char *p = str;

	s_assert(str != NULL);

	while (*p) {
		if (IsGenWildChar(*p))
			return YES;
	}

	return NO;
}

/*
 * mo_rehash - REHASH message handler
 *
 * parv[1] = rehash command or target server
 * parv[2] = target server
 */
static int
mo_rehash (struct Client *client_p, struct Client *source_p,
	int parc, const char **parv)
{
	const char	*cmd = NULL;
	const char	*target_server = NULL;

	if(!IsOperRehash(source_p)) {
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name,
			   source_p->name, "rehash");
		return 0;
	} else if (parc > 3) {
		sendto_one(source_p,
			":%s NOTICE %s :REHASH takes at most two arguments",
			me.name, source_p->name);
		return 0;
	}

	if (parc > 2)
		cmd = parv[1], target_server = parv[2];
	else if (parc > 1 && contains_generic_wild(parv[1]))
		target_server = parv[1];

	sendto_realops_flags(UMODE_DEBUG, L_ALL,
		"%s is attempting to rehash%s%s%s%s",
		get_oper_name(source_p),
		cmd != NULL ? " " : "",
		cmd != NULL ? cmd : "",
		target_server != NULL ? " on " : "",
		target_server != NULL ? target_server : "");

	if (target_server != NULL) {
		if (!IsOperRemote(source_p)) {
			sendto_one(source_p, form_str(ERR_NOPRIVS), me.name,
				source_p->name, "remote");
			return 0;
		}

		sendto_match_servs(source_p, target_server, CAP_ENCAP, NOCAPS,
			"ENCAP %s REHASH %s", target_server,
			cmd != NULL ? cmd : "");
		if (!match(target_server, me.name))
			return 0;
	}

	return do_rehash(source_p, cmd);
}

/* {{{ me_rehash()
 *
 * parv[1] = rehash command
 */
static int
me_rehash (struct Client *client_p, struct Client *source_p,
	int parc, const char **parv)
{
	if (!IsPerson(source_p))
		return 0;
	if (!find_shared_conf_client(source_p, SHARED_REHASH))
		return 0;

	return do_rehash(source_p, parc > 1 ? parv[1] : NULL);
}
/* }}} */

/*
 * vim: ts=8 sw=8 noet fdm=marker tw=80
 */
