if BONOBO
gnumeric = gnumeric-bonobo
else
gnumeric = gnumeric
endif

gnumeric_helpdir = $(datadir)/gnome/help/gnumeric/C

gnumeric_help_DATA = 	\
	gnumeric.html	\
	topic.dat

SGML_FILES =			\
	analysis-tools.sgml	\
	autofill.sgml		\
	cell_refer.sgml		\
	dndselection.sgml	\
	editing.sgml		\
	files.sgml		\
	formats.sgml		\
	formulas.sgml		\
	func-header.sgml	\
	func-footer.sgml	\
	functions.sgml		\
	gnumeric.sgml		\
	number-format.sgml	\
	selections.sgml		\
	text-entry.sgml		\
	worksheet.sgml

PERL_FILES = 			\
	make-index.pl		\
	make-docbook.pl	

EXTRA_DIST = \
	topic.dat		\
	func.defs		\
	functions.sgml		\
	func-list.sgml		\
	func-index.sgml		\
	$(SGML_FILES)		\
	$(PERL_FILES)

all: 

gnumeric.html: gnumeric/gnumeric.html
	-cd $(srcdir) && cp gnumeric/gnumeric.html .

gnumeric/gnumeric.html: $(SGML_FILES)
	-cd $(srcdir) && db2html gnumeric.sgml

functions.sgml: func-list.sgml func-index.sgml func-header.sgml func-footer.sgml
	cd $(srcdir) && cat func-header.sgml func-index.sgml func-list.sgml func-footer.sgml > functions.sgml

func-list.sgml: func.defs make-docbook.pl
	cd $(srcdir) && perl make-docbook.pl func.defs > func-list.sgml

func-index.sgml: func.defs make-index.pl
	cd $(srcdir) && perl make-index.pl func.defs > func-index.sgml

make-func.defs: 
	gnumericdir=`cd $(top_builddir)/src && pwd`; \
	cd $(srcdir) && $$gnumericdir/gnumeric --dump-func-defs=func.defs

dist-hook:
	mkdir $(distdir)/gnumeric
	-cp $(srcdir)/gnumeric/*.html gnumeric/*.css $(distdir)/gnumeric
	-cp $(srcdir)/gnumeric.html $(distdir)
	mkdir $(distdir)/images
	-cp $(srcdir)/images/*.gif $(srcdir)/images/*.jpg $(distdir)/images

install-data-local: gnumeric.html
	$(mkinstalldirs) $(DESTDIR)$(gnumeric_helpdir)/images
	-for file in $(srcdir)/gnumeric/*.html $(srcdir)/gnumeric/*.css; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(gnumeric_helpdir)/$$basefile; \
	done
	-for file in $(srcdir)/images/*.jpg $(srcdir)/images/*.gif; do \
	$(INSTALL_DATA) $$file $(DESTDIR)$(gnumeric_helpdir)/images;\
	done

gnumeric.ps: gnumeric.sgml
	-db2ps $<

gnumeric.rtf: gnumeric.sgml	
	-db2rtf $<

