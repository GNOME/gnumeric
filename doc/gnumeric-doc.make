#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
omffile = gnumeric-$(lang).omf
gnumeric_docdir  = $(top_srcdir)/doc
entities += functions.xml

functions.xml: func.defs $(gnumeric_docdir)/make-func-list.pl func-header.xml func-footer.xml
	cat $(srcdir)/func-header.xml				>  $$$$.functmp ;	\
	perl $(gnumeric_docdir)/make-func-list.pl func.defs	>> $$$$.functmp ;	\
	cat $(srcdir)/func-footer.xml				>> $$$$.functmp ;	\
	if xmllint --format --encode "UTF-8" $$$$.functmp > "$@" ; then	\
	    rm $$$$.functmp;				\
	fi

func.defs: $(top_builddir)/src/gnumeric
	LC_ALL="$(locale)" ; export LC_ALL ; $(top_builddir)/src/gnumeric --dump-func-defs="$@"

include $(gnumeric_docdir)/xmldocs.make

# include generated files to simplify installation
EXTRA_DIST +=		\
	func.defs	\
	functions.xml	\
	func-header.xml func-footer.xml

#	functions.xml	# an entity, shipped via xmldocs.make

.PHONY : html
html :
	-mkdir -p html
	xsltproc -o html/	\
	--stringparam yelp_chunk_method exslt	\
	--stringparam yelp_max_chunk_depth 4	\
	--stringparam yelp_generate_navbar 1 	\
	$(datadir)/sgml/docbook/yelp/yelp-customization.xsl gnumeric.xml
