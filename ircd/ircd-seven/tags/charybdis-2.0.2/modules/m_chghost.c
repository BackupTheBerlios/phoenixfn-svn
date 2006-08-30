/*
 * Copyright (c) 2005 William Pitcock <nenolod -at- nenolod.net>
 * and Jilles Tjoelker <jilles -at- stack.nl>
 * All rights reserved.
 *
 * Redistribution in both source and binary forms are permitted
 * provided that the above copyright notice remains unchanged.
 *
 * m_chghost.c: A module for handling spoofing dynamically.
 */

#include "stdinc.h"
#include "tools.h"
#include "send.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "ircd.h"
#include "numeric.h"
#include "memory.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_serv.h"
#include "s_user.h"
#include "hash.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "sprintf_irc.h"
#include "whowas.h"
#include "monitor.h"

static int me_realhost(struct Client *, struct Client *, int, const char **);
static int me_chghost(struct Client *, struct Client *, int, const char **);
static int mo_chghost(struct Client *, struct Client *, int, const char **);

static void h_chghost_burst_client(hook_data_client *hdata);
static void h_chghost_introduce_client(hook_data_client *hdata);

struct Message realhost_msgtab = {
	"REALHOST", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_ignore, mg_ignore, mg_ignore, {me_realhost, 2}, mg_ignore}
};

struct Message chghost_msgtab = {
	"CHGHOST", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, {me_chghost, 3}, {mo_chghost, 3}}
};

mapi_clist_av1 chghost_clist[] = { &chghost_msgtab, &realhost_msgtab, NULL };

mapi_hfn_list_av1 chghost_hfnlist[] = {
	{ "burst_client",	(hookfn) h_chghost_burst_client },
	{ "introduce_client",	(hookfn) h_chghost_introduce_client },
	{ NULL, NULL }
};

DECLARE_MODULE_AV1(chghost, NULL, NULL, chghost_clist, NULL, chghost_hfnlist, "$Revision: 928 $");

/*
 * me_realhost
 * parv[0] = origin
 * parv[1] = real host
 *
 * Yes this contains a little race condition if someone does a whois
 * in between the UID and REALHOST and use_whois_actually is enabled.
 * I don't think that's a big problem as the whole thing is a
 * race condition.
 */
static int
me_realhost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	if (!IsPerson(source_p))
		return 0;

	del_from_hostname_hash(source_p->orighost, source_p);
	strlcpy(source_p->orighost, parv[1], HOSTLEN);
	if (irccmp(source_p->host, source_p->orighost))
		SetDynSpoof(source_p);
	else
		ClearDynSpoof(source_p);
	add_to_hostname_hash(source_p->orighost, source_p);
	return 0;
}

static void
h_chghost_burst_client(hook_data_client *hdata)
{
	if (!irccmp(hdata->target->host, hdata->target->orighost))
		return;

	sendto_one(hdata->client, ":%s ENCAP * REALHOST %s",
			get_id(hdata->target, hdata->client),
			hdata->target->orighost);
}

/* Introduce REALHOST on clients that were spoofed pre-registration */
static void
h_chghost_introduce_client(hook_data_client *hdata)
{
	if (!irccmp(hdata->target->host, hdata->target->orighost))
		return;

	sendto_server(hdata->client, NULL, CAP_TS6, NOCAPS, ":%s ENCAP * REALHOST %s",
			hdata->target->id, hdata->target->orighost);
	sendto_server(hdata->client, NULL, NOCAPS, CAP_TS6, ":%s ENCAP * REALHOST %s",
			hdata->target->name, hdata->target->orighost);
}

static void
do_chghost(struct Client *source_p, struct Client *target_p,
		const char *newhost)
{
	change_nick_user_host(target_p, target_p->name, target_p->username, newhost, 0, "Changing host");
	if (irccmp(target_p->host, target_p->orighost))
	{
		SetDynSpoof(target_p);
		if (MyClient(target_p))
			sendto_one_numeric(target_p, RPL_HOSTHIDDEN, "%s :is now your hidden host (set by %s)", target_p->host, source_p->name);
	}
	else
	{
		ClearDynSpoof(target_p);
		if (MyClient(target_p))
			sendto_one_numeric(target_p, RPL_HOSTHIDDEN, "%s :hostname reset by %s", target_p->host, source_p->name);
	}
	if (MyClient(source_p))
		sendto_one_notice(source_p, ":Changed hostname for %s to %s", target_p->name, target_p->host);
	if (!IsServer(source_p) && !IsService(source_p))
		sendto_realops_snomask(SNO_GENERAL, L_ALL, "%s changed hostname for %s to %s", get_oper_name(source_p), target_p->name, target_p->host);
}
  
/*
 * me_chghost
 * parv[0] = origin
 * parv[1] = target
 * parv[2] = host
 */
static int
me_chghost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	struct Client *target_p;

	if (!(target_p = find_person(parv[1])))
		return -1;

	do_chghost(source_p, target_p, parv[2]);

	return 0;
}

/*
 * mo_chghost
 * parv[0] = origin
 * parv[1] = target
 * parv[2] = host
 */
/* Disable this because of the abuse potential -- jilles
 * No, make it toggleable via ./configure. --nenolod
 */
static int
mo_chghost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
#ifdef ENABLE_OPER_CHGHOST
	struct Client *target_p;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS),
			   me.name, source_p->name, "admin");
		return 0;
	}

	if (!(target_p = find_named_person(parv[1])))
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK,
				form_str(ERR_NOSUCHNICK), parv[1]);
		return -1;
	}

	do_chghost(source_p, target_p, parv[2]);

	sendto_server(NULL, NULL,
		CAP_TS6, NOCAPS, ":%s ENCAP * CHGHOST %s :%s",
		use_id(source_p), use_id(target_p), parv[2]);
	sendto_server(NULL, NULL,
		NOCAPS, CAP_TS6, ":%s ENCAP * CHGHOST %s :%s",
		parv[0], target_p->name, parv[2]);
#else
	sendto_one_notice(source_p, ":CHGHOST is disabled");
#endif

	return 0;
}

