#! /bin/sh
#
# $Id$

prog="`basename $0`"

die() {
    echo "$prog: error: $*";
    exit 1;
}

test -d src || \
    die "this script must be run from the top-level source directory"

echo "bootstrapping..."
autoconf || die "autoconf failed"
autoheader || die "autoheader failed"

# vim: ft=sh ts=8 sw=4 et
