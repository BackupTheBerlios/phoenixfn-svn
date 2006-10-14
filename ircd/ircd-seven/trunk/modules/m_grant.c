/*
 * Copyright (C) 2006 Jilles Tjoelker
 *
 * $Id$
 */

#include "stdinc.h"
#include "modules.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "s_conf.h"
#include "s_newconf.h"

static int mo_grant(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

struct Message grant_msgtab = {
  "GRANT", 0, 0, 0, MFLG_SLOW,
  { mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_grant, 3}}
};

mapi_clist_av1 grant_clist[] = { &grant_msgtab, NULL };

DECLARE_MODULE_AV1(grant, NULL, NULL, grant_clist, NULL, NULL, "$Revision$");

/* copied from src/newconf.c */
struct mode_table
{
	const char *name;
	int mode;
};

extern struct mode_table flag_table[];

static int
mo_grant(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	const char *newparv[4];
	struct oper_conf oper;
	int addflags = 0, removeflags = 0;
	int dooper = 0, dodeoper = 0;
	int i, j;
	int dir;
	const char *p;
	int changes = 0;
	char desc[512];

	if(!IsOperGrant(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "grant");
		return 0;
	}

	target_p = find_named_person(parv[1]);
	if (target_p == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK,
				form_str(ERR_NOSUCHNICK), parv[1]);
		return 0;
	}

	if (!MyConnect(target_p))
	{
		sendto_one(source_p, form_str(ERR_USERNOTONSERV),
			   me.name, source_p->name, target_p->name);
		return 0;
	}

	for (i = 2; i < parc; i++)
	{
		p = parv[i];
		if (*p == '-')
			dir = MODE_DEL, p++;
		else if (*p == '+')
			dir = MODE_ADD, p++;
		else
			dir = MODE_ADD;
		if (!irccmp(p, "oper"))
		{
			if (dir == MODE_ADD)
				dooper = 1, dodeoper = 0;
			else
				dodeoper = 1, dooper = 0;
			continue;
		}
		for (j = 0; flag_table[j].name != NULL; j++)
		{
			if (!irccmp(p, flag_table[j].name))
			{
				if (dir == MODE_ADD)
					addflags |= flag_table[j].mode, removeflags &= ~flag_table[j].mode;
				else
					removeflags |= flag_table[j].mode, addflags &= ~flag_table[j].mode;
				break;
			}
		}
		if (flag_table[j].name == NULL)
		{
			sendto_one_notice(source_p, ":Unknown GRANT keyword '%s'", p);
			return 0;
		}
	}
	if (((addflags | removeflags) & source_p->operflags) != (addflags | removeflags))
	{
		sendto_one_notice(source_p, ":You may not change flags you do not have access to");
		return 0;
	}

	if (dodeoper)
	{
		if (!IsOper(target_p))
			sendto_one_notice(source_p, ":%s is not an oper",
					target_p->name);
		else
		{
			newparv[0] = target_p->name;
			newparv[1] = target_p->name;
			newparv[2] = "-o";
			newparv[3] = NULL;
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
					"%s is deopering %s",
					get_oper_name(source_p),
					get_client_name(target_p, HIDE_IP));
			sendto_one_notice(target_p, ":%s is deopering you",
					source_p->name);
			user_mode(target_p, target_p, 3, newparv);
			sendto_one_notice(source_p, ":Deopered %s",
					target_p->name);
			changes++;
		}
	}
	else if (dooper)
	{
		if (IsOper(target_p))
			sendto_one_notice(source_p, ":%s is already an oper",
					target_p->name);
		else
		{
			desc[0] = '\0';
			for (j = 0; flag_table[j].name != NULL; j++)
			{
				if ((addflags & flag_table[j].mode) == flag_table[j].mode)
				{
					if (desc[0] != '\0')
						strlcat(desc, " ", sizeof desc);
					strlcat(desc, "+", sizeof desc);
					strlcat(desc, flag_table[j].name, sizeof desc);
				}
			}
			if (desc[0] == '\0')
				strlcpy(desc, "<none>", sizeof desc);
			oper.name = "<grant>";
			oper.username = "";
			oper.host = "";
			oper.passwd = "";
			oper.flags = addflags;
			oper.umodes = 0;
			oper.snomask = 0;
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
					"%s is opering %s with flags %s",
					get_oper_name(source_p),
					get_client_name(target_p, HIDE_IP),
					desc);
			sendto_one_notice(target_p, ":%s is opering you",
					source_p->name);
			oper_up(target_p, &oper);
			sendto_one_notice(source_p, ":Opered %s with flags %s",
					target_p->name, desc);
			changes++;
		}
	}
	removeflags &= target_p->operflags & OPER_FLAGS;
	addflags &= ~target_p->operflags & OPER_FLAGS;
	if ((addflags | removeflags) != 0)
	{
		if (!IsOper(target_p))
		{
			sendto_one_notice(source_p, ":%s is not an oper",
					target_p->name);
		}
		else
		{
			target_p->operflags |= addflags;
			target_p->operflags &= ~removeflags;
			desc[0] = '\0';
			for (j = 0; flag_table[j].name != NULL; j++)
			{
				if ((addflags & flag_table[j].mode) == flag_table[j].mode)
				{
					if (desc[0] != '\0')
						strlcat(desc, " ", sizeof desc);
					strlcat(desc, "+", sizeof desc);
					strlcat(desc, flag_table[j].name, sizeof desc);
				}
				else if ((removeflags & flag_table[j].mode) == flag_table[j].mode)
				{
					if (desc[0] != '\0')
						strlcat(desc, " ", sizeof desc);
					strlcat(desc, "-", sizeof desc);
					strlcat(desc, flag_table[j].name, sizeof desc);
				}
			}
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
					"%s is changing oper flags on %s (%s)",
					get_oper_name(source_p),
					get_client_name(target_p, HIDE_IP),
					desc);
			sendto_one_notice(target_p, ":%s is changing oper flags on you: %s",
					source_p->name, desc);
			sendto_one_notice(source_p, ":Changed oper flags on %s: %s",
					target_p->name, desc);
			/* fix up any umodes/snomasks they may not have
			 * anymore */
			newparv[0] = target_p->name;
			newparv[1] = target_p->name;
			newparv[2] = "+";
			newparv[3] = NULL;
			user_mode(target_p, target_p, 3, newparv);
			changes++;
		}
	}
	if (!changes)
		sendto_one_notice(source_p, ":Oper flags on %s unchanged",
				target_p->name);

	return 0;
}
