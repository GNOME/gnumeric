#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
gnumeric_docdir  = $(top_srcdir)/doc
entities = functions.xml

functions.xml: func-list.xml $(srcdir)/func-header.xml $(srcdir)/func-footer.xml
	cd $(srcdir) && cat $(srcdir)/func-header.xml func-list.xml $(srcdir)/func-footer.xml > "$@"

func-list.xml: func.defs $(gnumeric_docdir)/make-func-list.pl
	cd $(srcdir) && perl $(gnumeric_docdir)/make-func-list.pl func.defs > "$@"

func.defs: 
	LC_ALL="$(locale)" ; export LC_ALL ; $(top_builddir)/src/gnumeric --dump-func-defs="$@"

# include generated files to simplify installation
dist-hook::
	mkdir $(distdir)/figures

include $(gnumeric_docdir)/xmldocs.make
