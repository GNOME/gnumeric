SUFFIXES = .xml.in .xml
.xml.in.xml:
	sed -e 's/ _author *=/ author=/' -e 's/ _name *=/ name=/' \
	-e 's/ _description *=/ description=/' < $< > $@

autoformat_templatesdir = $(gnumeric_autoformatdir)

ThreeDdir        = $(autoformat_templatesdir)/3D
Classicaldir     = $(autoformat_templatesdir)/Classical
Colourfuldir     = $(autoformat_templatesdir)/Colourful
Financialdir     = $(autoformat_templatesdir)/Financial
Generaldir       = $(autoformat_templatesdir)/General
Listdir          = $(autoformat_templatesdir)/List

Category_files = \
	3D.category Classical.category Colourful.category \
	Financial.category General.category List.category

ThreeD_xml_in_files = \
	autoformat.3D.button.xml.in                \
	autoformat.3D.list.xml.in

Classical_xml_in_files = \
	autoformat.Classical.cool.xml.in           \
	autoformat.Classical.simple.xml.in         \
	autoformat.Classical.trendy.xml.in

Colourful_xml_in_files = \
	autoformat.Colourful.banana.xml.in         \
	autoformat.Colourful.black.xml.in          \
	autoformat.Colourful.blue.xml.in           \
	autoformat.Colourful.orange.xml.in         \
	autoformat.Colourful.vanilla.xml.in

Financial_xml_in_files = \
	autoformat.Financial.basic.xml.in          \
	autoformat.Financial.desert.xml.in         \
	autoformat.Financial.ice.xml.in            \
	autoformat.Financial.modern.xml.in         \
	autoformat.Financial.purple.xml.in

General_xml_in_files = \
	autoformat.General.advanced.xml.in         \
	autoformat.General.basic.xml.in            \
	autoformat.General.empty.xml.in            \
	autoformat.General.table.xml.in

List_xml_in_files = \
	autoformat.List.basic.xml.in               \
	autoformat.List.green.xml.in               \
	autoformat.List.lila.xml.in                \
	autoformat.List.simple.xml.in

ThreeD_DATA      = $(ThreeD_xml_in_files:.xml.in=.xml)
Classical_DATA   = $(Classical_xml_in_files:.xml.in=.xml)
Colourful_DATA   = $(Colourful_xml_in_files:.xml.in=.xml)
Financial_DATA   = $(Financial_xml_in_files:.xml.in=.xml)
General_DATA     = $(General_xml_in_files:.xml.in=.xml)
List_DATA        = $(List_xml_in_files:.xml.in=.xml)

EXTRA_DIST = $(ThreeD_xml_in_files) $(Classical_xml_in_files) \
	     $(Colourful_xml_in_files) $(Financial_xml_in_files) \
	     $(General_xml_in_files) $(List_xml_in_files) \
             $(Category_files)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(ThreeDdir)
	$(mkinstalldirs) $(DESTDIR)$(Classicaldir)
	$(mkinstalldirs) $(DESTDIR)$(Colourfuldir)
	$(mkinstalldirs) $(DESTDIR)$(Financialdir)
	$(mkinstalldirs) $(DESTDIR)$(Generaldir)
	$(mkinstalldirs) $(DESTDIR)$(Listdir)

	$(INSTALL_DATA) $(srcdir)/3D.category $(DESTDIR)$(ThreeDdir)/.category
	$(INSTALL_DATA) $(srcdir)/Classical.category $(DESTDIR)$(Classicaldir)/.category
	$(INSTALL_DATA) $(srcdir)/Colourful.category $(DESTDIR)$(Colourfuldir)/.category
	$(INSTALL_DATA) $(srcdir)/Financial.category $(DESTDIR)$(Financialdir)/.category
	$(INSTALL_DATA) $(srcdir)/General.category $(DESTDIR)$(Generaldir)/.category
	$(INSTALL_DATA) $(srcdir)/List.category $(DESTDIR)$(Listdir)/.category

uninstall-local:
	rm -rf  $(DESTDIR)$(ThreeDdir)
	rm -rf  $(DESTDIR)$(Classicaldir)
	rm -rf  $(DESTDIR)$(Colourfuldir)
	rm -rf  $(DESTDIR)$(Financialdir)
	rm -rf  $(DESTDIR)$(Generaldir)
	rm -rf  $(DESTDIR)$(Listdir)
