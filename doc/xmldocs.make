# To use this template:
#     1) Define: figs, docname, lang, omffile, entities although figs, 
#        omffile, and entities may be empty in your Makefile.am which 
#        will "include" this one 
#     2) Figures must go under figures/ and be in PNG format
#     3) You should only have one document per directory 
#
#        Note that this makefile forces the directory name under
#        $prefix/share/gnome/help/ to be the same as the XML filename
#        of the document.  This is required by GNOME. eg:
#        $prefix/share/gnome/help/fish_applet/C/fish_applet.xml
#                                 ^^^^^^^^^^^   ^^^^^^^^^^^
# Definitions:
#   figs         A list of screenshots which will be included in EXTRA_DIST
#                Note that these should reside in figures/ and should be .png
#                files, or you will have to make modifications below.
#   docname      This is the name of the XML file: <docname>.xml
#   lang         This is the document locale
#   omffile      This is the name of the OMF file.  Convention is to name
#                it <docname>-<locale>.omf.
#   entities     This is a list of XML entities which must be installed 
#                with the main XML file and included in EXTRA_DIST. 
# eg:
#   figs = \
#          figures/fig1.png            \
#          figures/fig2.png
#   docname = scrollkeeper-manual
#   lang = C
#   omffile=scrollkeeper-manual-C.omf
#   entities = fdl.xml
#   include $(top_srcdir)/help/xmldocs.make
#   dist-hook: app-dist-hook
#

docdir = $(gnumeric_datadir)/share/gnome/help/$(docname)/$(lang)

xml_files = $(entities) $(docname).xml

omf_dir=$(top_srcdir)/omf-install

EXTRA_DIST += $(xml_files) $(omffile) $(figs)

CLEANFILES = omf_timestamp

all: omf

omf: omf_timestamp

omf_timestamp: $(omffile)
	-for file in $(omffile); do \
	  scrollkeeper-preinstall $(docdir)/`awk 'BEGIN {RS = ">" } /identifier/ {print $$0}' $(srcdir)/$${file} | awk 'BEGIN {FS="\""} /url/ {print $$2}'` $(srcdir)/$${file} $(omf_dir)/$${file}; \
	done
	touch omf_timestamp

$(docname).xml: $(entities)
	-ourdir=`pwd`;  \
	cd $(srcdir);   \
	cp $(entities) $$ourdir

dist-hook: 
	-$(mkinstalldirs) $(distdir)/figures
	-if [ -e topic.dat ]; then \
		cp $(srcdir)/topic.dat $(distdir); \
	 fi

install-data-am: omf
	-$(mkinstalldirs) $(DESTDIR)$(docdir)/figures
	-for xml_file in $(xml_files); do \
	 cp $(srcdir)/$$xml_file $(DESTDIR)$(docdir); \
	done
	-for file in $(srcdir)/figures/*.png; do \
	  basefile=`echo $$file | sed -e  's,^.*/,,'`; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(docdir)/figures/$$basefile; \
	done
	# libbonoboui and libgnomeui want help in different places add a sym
	# link to get an install that works for both (It may already exist)
	-ln -s share/gnome $(DESTDIR)$(gnumeric_datadir)/gnome
	-if [ -e $(srcdir)/topic.dat ]; then \
		$(INSTALL_DATA) $(srcdir)/topic.dat $(DESTDIR)$(docdir); \
	 fi

uninstall-local:
	-for file in $(srcdir)/figures/*.png; do \
	  basefile=`echo $$file | sed -e  's,^.*/,,'`; \
	  rm -f $(docdir)/figures/$$basefile; \
	done
	-for file in $(xml_files); do \
	  rm -f $(DESTDIR)$(docdir)/$$file; \
	done
	rm $(gnumeric_datadir)/gnome
	-rmdir $(DESTDIR)$(docdir)/figures
	-rmdir $(DESTDIR)$(docdir)
