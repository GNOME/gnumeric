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
	functions.sgml		\
	gnumeric.sgml		\
	intro.sgml		\
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
