#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
gnumeric_docdir  = $(top_srcdir)/doc
sgml_ents = functions.sgml

gnumeric_helpdir = $(datadir)/gnome/help/gnumeric/$(lang)
gnumeric_help_DATA = topic.dat

# include generated files to simplify installation
EXTRA_DIST +=					\
	topic.dat				\
	func.defs				\
	func-header.sgml func-footer.sgml	\
	func-list.sgml

include $(gnumeric_docdir)/sgmldocs.make

$(srcdir)/functions.sgml : $(srcdir)/func-list.sgml $(srcdir)/func-header.sgml $(srcdir)/func-footer.sgml
	cd $(srcdir) && rm -f $@ ; cat func-header.sgml func-list.sgml func-footer.sgml > $@

$(srcdir)/func-list.sgml : $(srcdir)/func.defs $(gnumeric_docdir)/make-func-list.pl
	cd $(srcdir) && rm -f $@ ; perl $(gnumeric_docdir)/make-func-list.pl func.defs > $@

func.defs :
	LC_ALL="$(locale)" ; export LC_ALL ; rm -f $@ ; $(top_builddir)/src/gnumeric --dump-func-defs=$(srcdir)/func.defs
