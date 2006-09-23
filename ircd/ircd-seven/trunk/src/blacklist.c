/*
 *  charybdis: A slightly useful ircd.
 *  blacklist.c: Manages DNS blacklist entries and lookups
 *
 *  Copyright (C) 2006 charybdis development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "client.h"
#include "res.h"
#include "tools.h"
#include "memory.h"
#include "numeric.h"
#include "reject.h"
#include "s_conf.h"
#include "s_user.h"
#include "blacklist.h"

dlink_list blacklist_list = { NULL, NULL, 0 };

/* private interfaces */
static struct Blacklist *find_blacklist(char *name)
{
	dlink_node *nptr;

	DLINK_FOREACH(nptr, blacklist_list.head)
	{
		struct Blacklist *blptr = (struct Blacklist *) nptr->data;

		if (!irccmp(blptr->host, name))
			return blptr;
	}

	return NULL;
}

static void blacklist_dns_callback(void *vptr, struct DNSReply *reply)
{
	struct BlacklistClient *blcptr = (struct BlacklistClient *) vptr;

	if (blcptr == NULL || blcptr->client_p == NULL)
		return;

	if (blcptr->client_p->preClient == NULL)
	{
		sendto_realops_snomask(SNO_GENERAL, L_ALL,
				"blacklist_dns_callback(): blcptr->client_p->preClient (%s) is NULL", get_client_name(blcptr->client_p, HIDE_IP));
		MyFree(blcptr);
		return;
	}

	/* they have a blacklist entry for this client */
	if (reply != NULL && blcptr->client_p->preClient->dnsbl_listed == NULL)
	{
		blcptr->client_p->preClient->dnsbl_listed = blcptr->blacklist;
		/* reference to blacklist moves from blcptr to client_p->preClient... */
	}
	else
		unref_blacklist(blcptr->blacklist);

	dlinkDelete(&blcptr->node, &blcptr->client_p->preClient->dnsbl_queries);

	/* yes, it can probably happen... */
	if (dlink_list_length(&blcptr->client_p->preClient->dnsbl_queries) == 0 && blcptr->client_p->flags & FLAGS_SENTUSER && !EmptyString(blcptr->client_p->name))
	{
		char buf[USERLEN];
		strlcpy(buf, blcptr->client_p->username, USERLEN);
		register_local_user(blcptr->client_p, blcptr->client_p, buf);
	}

	MyFree(blcptr);
}

/* XXX: no IPv6 implementation, not to concerned right now though. */
static void initiate_blacklist_dnsquery(struct Blacklist *blptr, struct Client *client_p)
{
	struct BlacklistClient *blcptr = MyMalloc(sizeof(struct BlacklistClient));
	char buf[IRCD_BUFSIZE];
	int ip[4];

	blcptr->blacklist = blptr;
	blcptr->client_p = client_p;

	blcptr->dns_query.ptr = blcptr;
	blcptr->dns_query.callback = blacklist_dns_callback;

	/* XXX: yes I know this is bad, I don't really care right now */
	sscanf(client_p->sockhost, "%d.%d.%d.%d", &ip[3], &ip[2], &ip[1], &ip[0]);

	/* becomes 2.0.0.127.torbl.ahbl.org or whatever */
	ircsnprintf(buf, IRCD_BUFSIZE, "%d.%d.%d.%d.%s", ip[0], ip[1], ip[2], ip[3], blptr->host);

	gethost_byname_type(buf, &blcptr->dns_query, T_A);

	dlinkAdd(blcptr, &blcptr->node, &client_p->preClient->dnsbl_queries);
	blptr->refcount++;
}

/* public interfaces */
struct Blacklist *new_blacklist(char *name, char *reject_reason)
{
	struct Blacklist *blptr;

	if (name == NULL || reject_reason == NULL)
		return NULL;

	blptr = find_blacklist(name);
	if (blptr == NULL)
	{
		blptr = MyMalloc(sizeof(struct Blacklist));
		dlinkAddAlloc(blptr, &blacklist_list);
	}
	else
		blptr->status &= ~CONF_ILLEGAL;
	strlcpy(blptr->host, name, HOSTLEN);
	strlcpy(blptr->reject_reason, reject_reason, IRCD_BUFSIZE);

	return blptr;
}

void unref_blacklist(struct Blacklist *blptr)
{
	blptr->refcount--;
	if (blptr->status & CONF_ILLEGAL && blptr->refcount <= 0)
	{
		dlinkFindDestroy(blptr, &blacklist_list);
		MyFree(blptr);
	}
}

void lookup_blacklists(struct Client *client_p)
{
	dlink_node *nptr;

	/* We don't do IPv6 right now, sorry! */
	if (client_p->localClient->ip.ss_family == AF_INET6)
		return;

	DLINK_FOREACH(nptr, blacklist_list.head)
	{
		struct Blacklist *blptr = (struct Blacklist *) nptr->data;

		if (!(blptr->status & CONF_ILLEGAL))
			initiate_blacklist_dnsquery(blptr, client_p);
	}
}

void abort_blacklist_queries(struct Client *client_p)
{
	dlink_node *ptr, *next_ptr;
	struct BlacklistClient *blcptr;

	if (client_p->preClient == NULL)
		return;
	DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->preClient->dnsbl_queries.head)
	{
		blcptr = ptr->data;
		dlinkDelete(&blcptr->node, &client_p->preClient->dnsbl_queries);
		unref_blacklist(blcptr->blacklist);
		delete_resolver_queries(&blcptr->dns_query);
		MyFree(blcptr);
	}
}

void destroy_blacklists(void)
{
	dlink_node *ptr, *next_ptr;
	struct Blacklist *blptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, blacklist_list.head)
	{
		blptr = ptr->data;
		blptr->hits = 0; /* keep it simple and consistent */
		if (blptr->refcount > 0)
			blptr->status |= CONF_ILLEGAL;
		else
		{
			MyFree(ptr->data);
			dlinkDestroy(ptr, &blacklist_list);
		}
	}
}
