## This is included in every Makefile.am

INCLUDES = -I$(top_srcdir)/include
CLEANFILES = *.bb *.bbg *.gcov *.da

pkgdocdir = $(datadir)/doc/$(PACKAGE)
