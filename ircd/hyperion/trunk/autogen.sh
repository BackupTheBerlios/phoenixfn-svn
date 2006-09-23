#!/bin/sh
# If autotools without version exist, assume they're the right
# version, otherwise use versioned names -- jilles
which aclocal >/dev/null 2>&1 || AM_VERSION=19
which autoconf >/dev/null 2>&1 || AC_VERSION=259
aclocal${AM_VERSION} -I autoconf
echo You can safely ignore any warnings about underquoted definitions above.
automake${AM_VERSION}
autoconf${AC_VERSION}
./configure "$@"
