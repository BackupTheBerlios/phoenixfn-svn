## This is shared between the various sgml documents
## Parameters are $(name), which is the document name, and $(sources),
## which is the list of .sgml files

if DOCUMENTATION
pkgdoc_DATA = $(name).ps $(name).txt
noinst_DATA = $(name).html
endif

EXTRA_DIST = $(sources)
CLEANFILES += $(name).ps $(name).dvi $(name).txt $(name).tex $(name).aux $(name).log

if DOCUMENTATION
thisdocdir = $(pkgdocdir)/$(name).html
thisdoc_DATA = $(wildcard $(name).html/*)
endif

clean-local:
	rm -rf $(name).html

stylesheet = $(top_srcdir)/doc/sgml/stylesheet.dsl

$(name).ps: $(sources) $(stylesheet) Makefile

$(name).txt: $(sources) $(stylesheet) Makefile

$(name).html: $(sources) $(stylesheet) Makefile

%.txt: %.sgml
	jw -f docbook -b txt -d $(stylesheet)\#html $<

%.ps: %.sgml
	jw -f docbook -b ps -d $(stylesheet)\#print $<

%.html: %.sgml
	rm -rf $(name).html
	jw -f docbook -b html -d $(stylesheet)\#html -o $@ $<
