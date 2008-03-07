#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
if WITH_GNOME
  omffile = gnumeric-$(lang).omf
endif
gnumeric_docdir  = $(top_srcdir)/doc
entities += functions.xml

functions_xml_parts = func.defs func-header.xml func-footer.xml

functions.xml: $(gnumeric_docdir)/make-func-list.pl $(functions_xml_parts)
	(cat $(srcdir)/func-header.xml ;				\
	 $(PERL) $(gnumeric_docdir)/make-func-list.pl func.defs ;	\
	 cat $(srcdir)/func-footer.xml					\
	) >functions.tmp ;						\
	if xmllint -noent --format --encode "UTF-8" functions.tmp >functions.out ; then	\
	    mv functions.out $@; rm functions.tmp;					\
	fi

MOSTLYCLEANFILES = functions.out functions.tmp

func.defs: $(top_builddir)/src/gnumeric$(EXEEXT)
	LC_ALL="$(locale)" ; export LC_ALL ; $(top_builddir)/src/gnumeric --dump-func-defs="$@"

include $(top_srcdir)/xmldocs.make

# Include generated files to simplify installation.
# (Entities, including functions.xml, are shipped via xmldocs.make.)
EXTRA_DIST += $(functions_xml_parts)

noinst_DATA =

.PHONY : html validate chm pdf
html :
	-mkdir -p html
	xsltproc -o html/gnumeric.shtml					\
	    --param db.chunk.chunk_top 0 				\
	    --param db.chunk.max_depth 3				\
	    --stringparam db.chunk.basename	"gnumeric"		\
	    --stringparam db.chunk.extension	".shtml"		\
	    --stringparam db2html.css.file	"gnumeric-doc.css"	\
	    $(datadir)/xml/gnome/xslt/docbook/html/db2html.xsl		\
	    $(srcdir)/gnumeric.xml

validate :
	xmllint --valid --noout $(srcdir)/gnumeric.xml

chm :
	-rm -rf chm
	mkdir -p chm && cd chm && xsltproc -o . 	\
	    $(srcdir)/../gnumeric-docbook-2-htmlhelp.xsl\
	    $(srcdir)/../gnumeric.xml
	xmllint --valid --noout --html chm/*.html
	for f in chm/*.html; do				\
	    xmllint --format --html "$$f" > chm/.$$$$;	\
	    mv chm/.$$$$ "$$f" ;			\
	done
	cp -r $(srcdir)/figures	chm

if ENABLE_PDFDOCS
noinst_DATA += gnumeric.pdf
endif

if ENABLE_PDF_VIA_DBCONTEXT
gnumeric.pdf:
	env TEXINPUTS=$(srcdir):.: dbcontext -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=600' \
		-o gnumeric.tex $(srcdir)/gnumeric.xml
	env TEXMFCNF=$(srcdir): \
		TEXINPUTS=$(srcdir):/usr/share/texmf/tex/context/dbcontext/style: \
		texexec --pdf --mode=A4 --batch gnumeric.tex
endif

if ENABLE_PDF_VIA_DBLATEX
gnumeric.pdf:
	dblatex -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=0.6' \
		-o gnumeric.tex $(srcdir)/gnumeric.xml
	for runs in $$(seq 1 4); do \
		env TEXINPUTS=$(srcdir): \
			pdflatex -interaction nonstopmode gnumeric.tex ; \
	done
endif
