## Process this file with automake to produce Makefile.in

include $(top_srcdir)/build-misc/common.mk

## We're not a GNU project, and don't quite meet GNU standards
AUTOMAKE_OPTIONS = foreign

DISTCHECK_CONFIGURE_FLAGS = --with-config=dist

## This one just recurses into the subdirectories

SUBDIRS = autoconf build-misc include src lib tools doc patches

EXTRA_DIST = AUTHORS NEWS README

ACLOCAL = @ACLOCAL@ -I autoconf

distclean-local:
	rm -rf debug-tree include/config.h.debug include/config.h

# Ensure the correct config.h is distributed
dist-hook:
	(cd $(distdir)/include && cmp config.h.dist config.h || cp config.h.dist config.h)

install-exec-hook:
	(${mkinstalldirs} ${DESTDIR}${prefix}/var/lib/dancer-ircd && \
	cd ${DESTDIR}${prefix}/var/lib/dancer-ircd && \
	([ -f kline.conf ] || [ -L kline.conf ] || touch kline.conf) && \
	([ -f dline.conf ] || [ -L dline.conf ] || touch dline.conf))
	(${mkinstalldirs} ${DESTDIR}${prefix}/var/log/dancer-ircd && \
	cd ${DESTDIR}${prefix}/var/log/dancer-ircd && \
	([ -f ircd.log ] || [ -L ircd.log ] || touch ircd.log))
	${mkinstalldirs} ${DESTDIR}${prefix}/var/run/dancer-ircd

sign:
	gpg --detach-sign $(distdir).tar.gz
	md5sum $(distdir).tar.gz | gpg --clearsign > $(distdir).tar.gz.md5sum
