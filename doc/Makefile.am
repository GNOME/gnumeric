gnumeric_helpdir = $(datadir)/gnome/help/gnumeric/C

gnumeric_help_DATA = 	\
	gnumeric.html	\
	topic.dat

SGML_FILES =			\
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
	translating.sgml	\
	worksheet.sgml

TEXT_FILES =			\
	Design			\
	Future-Roadmap		\
	saving.txt

EXTRA_DIST = \
	$(SGML_FILES)		\
	$(TEXT_FILES)

all: gnumeric.html

gnumeric.html: gnumeric/gnumeric.html
	-cp gnumeric/gnumeric.html .

gnumeric/gnumeric.html: $(SGML_FILES)
	-db2html gnumeric.sgml

functions.sgml: func-list.sgml func-index.sgml func-header.sgml func-footer.sgml
	cat $(srcdir)/func-header.sgml func-index.sgml func-list.sgml $(srcdir)/func-footer.sgml > functions.sgml

func-list.sgml: func.defs $(srcdir)/make-docbook.pl
	perl $(srcdir)/make-docbook.pl func.defs > func-list.sgml

func-index.sgml: func.defs $(srcdir)/make-index.pl
	perl $(srcdir)/make-index.pl func.defs > func-index.sgml

func.defs: $(top_builddir)/src/gnumeric
	$(top_builddir)/src/gnumeric --dump-func-defs=func.defs

dist-hook:
	mkdir $(distdir)/gnumeric
	-cp gnumeric/*.html gnumeric/*.css $(distdir)/gnumeric

install-data-local: gnumeric.html
	$(srcdir)/../mkinstalldirs $(gnumeric_helpdir)/images
	-for file in $(srcdir)/gnumeric/*.html $(srcdir)/gnumeric/*.css; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $(srcdir)/$$file $(gnumeric_helpdir)/$$basefile; \
	done
	-for file in $(srcdir)/images/*.jpg; do \
	$(INSTALL_DATA) $(srcdir)/$$file $(gnumeric_helpdir)/images;\
	done

gnumeric.ps: gnumeric.sgml
	-db2ps $<

gnumeric.rtf: gnumeric.sgml	
	-db2rtf $<
