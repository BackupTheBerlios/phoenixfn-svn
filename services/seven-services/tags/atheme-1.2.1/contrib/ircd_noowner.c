/*
 * Copyright (c) 2006 Jilles Tjoelker
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Module to disable owner (+q) mode.
 * This will stop Atheme setting this mode by itself, but it can still
 * be used via OperServ MODE etc.
 *
 * $Id: ircd_noowner.c 5804 2006-07-09 14:37:47Z jilles $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"ircd_noowner", FALSE, _modinit, _moddeinit,
	"$Id: ircd_noowner.c 5804 2006-07-09 14:37:47Z jilles $",
	"Atheme Development Group <http://www.atheme.org>"
);

boolean_t oldflag;

void _modinit(module_t *m)
{

	if (ircd == NULL)
	{
		slog(LG_ERROR, "Module %s must be loaded after a protocol module.", m->name);
		m->mflags = MODTYPE_FAIL;
		return;
	}
	oldflag = ircd->uses_owner;
	ircd->uses_owner = FALSE;
}

void _moddeinit()
{

	ircd->uses_owner = oldflag;
}
