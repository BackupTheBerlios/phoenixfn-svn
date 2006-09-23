/*
 * Oper extban type: matches opers
 * -- jilles
 *
 * $Id$
 */

#include "stdinc.h"
#include "modules.h"
#include "client.h"
#include "ircd.h"

static int _modinit(void);
static void _moddeinit(void);
static int eb_oper(const char *data, struct Client *client_p, struct Channel *chptr, long mode_type);

DECLARE_MODULE_AV1(extb_oper, _modinit, _moddeinit, NULL, NULL, NULL, "$Revision$");

static int
_modinit(void)
{
	extban_table['o'] = eb_oper;

	return 0;
}

static void
_moddeinit(void)
{
	extban_table['o'] = NULL;
}

static int eb_oper(const char *data, struct Client *client_p,
		struct Channel *chptr, long mode_type)
{

	(void)chptr;
	(void)data;
	(void)mode_type;
	/* perhaps use data somehow? (opernick/flags?) */
	return IsOper(client_p) ? EXTBAN_MATCH : EXTBAN_NOMATCH;
}
