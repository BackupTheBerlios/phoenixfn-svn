#!/usr/bin/env bash
# For the lazy people, this does all the auto* stuff needed before
# ./configure && make will work
# (This is a maintainer script; it should never have to be run on
#  a distributed tarball)

set -e

${ACLOCAL:-aclocal} -I autoconf
${AUTOHEADER:-autoheader}
${AUTOCONF:-autoconf}

# If it exists and is executable, recheck and regenerate
test -x config.status && ./config.status --recheck
test -x config.status && ./config.status

# Exit true if we got this far
exit 0
