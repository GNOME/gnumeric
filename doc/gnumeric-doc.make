#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
gnumeric_docdir  = $(top_srcdir)/doc
sgml_ents = functions.xml

# include generated files to simplify installation
EXTRA_DIST +=					\
	topic.dat				\
	func.defs				\
	func-header.sgml func-footer.xml	\
	func-list.xml

$(srcdir)/functions.xml: $(srcdir)/func-list.xml $(srcdir)/func-header.xml $(srcdir)/func-footer.xml
	cd $(srcdir) && cat func-header.xml func-list.xml func-footer.xml > "$@"

$(srcdir)/func-list.xml: $(srcdir)/func.defs $(gnumeric_docdir)/make-func-list.pl
	cd $(srcdir) && perl $(gnumeric_docdir)/make-func-list.pl func.defs > "$@"

$(srcdir)/func.defs: 
	LC_ALL="$(locale)" ; export LC_ALL ; $(top_builddir)/src/gnumeric --dump-func-defs="$@"

include $(gnumeric_docdir)/xmldocs.make

dist-hook: app-dist-hook

