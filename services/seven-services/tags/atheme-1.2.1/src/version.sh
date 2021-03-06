#!/bin/sh
#
# Copyright (c) 2005-2006 Atheme Development Group
# Rights to this code are documented in doc/LICENSE.
#
# This file generates version.c.
# Stolen from ircd-ratbox.
#
# $Id: version.sh 5748 2006-07-06 08:57:10Z nenolod $
#

spitshell=cat
package=atheme

echo "Extracting $package/src/version.c..."

if test -r version.c.last
then
   generation=`sed -n 's/^const char \*generation = \"\(.*\)\";/\1/p' < version.c.last`
   if test ! "$generation" ; then generation=0; fi
else
   generation=0
fi

generation=`expr $generation + 1`

uname=`uname`

osinfo=`uname -a`;

creation=`date | \
awk '{if (NF == 6) \
         { print $1 " "  $2 " " $3 " "  $6 " at " $4 " " $5 } \
else \
         { print $1 " "  $2 " " $3 " " $7 " at " $4 " " $5 " " $6 }}'`

buildid=`echo "\$Revision: 5748 $" | \
	awk '{ print $2 }'`;

$spitshell >version.c <<!SUB!THIS!
/*
 * Copyright (c) 2005-2006 Atheme Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains version information.
 * Autogenerated by version.sh.
 */

#include "serno.h"

const char *generation = "$generation";
const char *creation = "$creation";
const char *platform = "$uname";
const char *version = "$1";
const char *revision = SERNO;
const char *osinfo = "$osinfo";

const char *infotext[] =
{
  "Atheme IRC Services --",
  "Copyright (c) 2005-2006 Atheme Development Group",
  " ",
  "All rights reserved.",
  " ",
  "Redistribution and use in source and binary forms, with or without modification,",
  "are permitted provided that the following conditions are met:",
  " ",
  "      Redistributions of source code must retain the above copyright notice,",
  "      this list of conditions and the following disclaimer.",
  " ",
  "      Redistributions in binary form must reproduce the above copyright notice,",
  "      this list of conditions and the following disclaimer in the documentation",
  "      and/or other materials provided with the distribution.",
  " ",
  "      Neither the name of the author nor the names of its contributors may be",
  "      used to endorse or promote products derived from this software without",
  "      specific prior written permission.",
  " ",
  "Any derived works must guarantee the same basic freedom that this license",
  "does; it is essential for the open source community to grow through the",
  "open exchange of information. Therefore, all derived works are granted through",
  "THIS license a guarantee of no further restrictions, and any further",
  "restrictions on derived works can be considered null and void.",
  " ",
  "As an exception to the above rule, you may utilize any GPL-compatible license,",
  "including the GPL itself, as a substitution to the Atheme license. A list of",
  "GPL-compatible licenses can be viewed at http://www.fsf.org/licensing.",
  " ",
  "You may also jump to a newer revision of this license at your option, if",
  "such a license becomes available, or downgrade to an older version at your",
  "choosing; however this is not recommended as newer versions are designed",
  "to guarantee more software freedom.",
  " ",
  "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS AND",
  "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED",
  "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE",
  "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR",
  "ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES",
  "(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;",
  "LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON",
  "ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT",
  "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS",
  "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.",
  " ",
  "Currently Atheme's core group consists of the following developers,",
  "in nick-alphabetical order:",
  "  beu, Elfyn McBratney <elfyn.mcbratney@gmail.com>",
  "  gxti, Michael Tharp <gxti@partiallystapled.com>",
  "  jilles, Jilles Tjoelker <jilles@stack.nl>",
  "  nenolod, William Pitcock <nenolod@nenolod.net>",
  "  terminal, Theo Julienne <admin@ozweb.nu>",
  "  w00t, Robin Burchell <viroteck@viroteck.net>",
  " ",
  "The following people have contributed blood, sweat and tears to",
  "this Atheme release:",
  "  alambert, Alex Lambert <alambert@quickfire.org>",
  "  Dianora, Diane Bruce <db@db.net>",
  "  kog, Greg Feigenson <kog@epiphanic.org>",
  "  kuja, Jeff Katz <jeff@katzonline.net>",
  "  pfish, Patrick Fish <pofish@gmail.com>",
  "  Trystan, Trystan Scott Lee <trystan@nomadirc.net>",
  "  zparta, Jens Holmqvist <zparta@hispan.se>",
  " ",
  "Visit our website at http://www.atheme.org",
  0,
};
!SUB!THIS!
