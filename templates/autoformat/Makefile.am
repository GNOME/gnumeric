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

ThreeD_DATA = \
	autoformat.3D.button.xml                \
	autoformat.3D.list.xml

Classical_DATA = \
	autoformat.Classical.cool.xml           \
	autoformat.Classical.simple.xml         \
	autoformat.Classical.trendy.xml

Colourful_DATA = \
	autoformat.Colourful.banana.xml         \
	autoformat.Colourful.black.xml          \
	autoformat.Colourful.blue.xml           \
	autoformat.Colourful.orange.xml         \
	autoformat.Colourful.vanilla.xml

Financial_DATA = \
	autoformat.Financial.basic.xml          \
	autoformat.Financial.desert.xml         \
	autoformat.Financial.ice.xml            \
	autoformat.Financial.modern.xml         \
	autoformat.Financial.purple.xml

General_DATA = \
	autoformat.General.advanced.xml         \
	autoformat.General.basic.xml            \
	autoformat.General.empty.xml            \
	autoformat.General.table.xml

List_DATA = \
	autoformat.List.basic.xml               \
	autoformat.List.green.xml               \
	autoformat.List.lila.xml                \
	autoformat.List.simple.xml

EXTRA_DIST = $(ThreeD_DATA) $(Classical_DATA) $(Colourful_DATA) \
             $(Financial_DATA) $(General_DATA) $(List_DATA) \
             $(Category_files)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(ThreeD_dir)
	$(mkinstalldirs) $(DESTDIR)$(Classical_dir)
	$(mkinstalldirs) $(DESTDIR)$(Colourful_dir)
	$(mkinstalldirs) $(DESTDIR)$(Financial_dir)
	$(mkinstalldirs) $(DESTDIR)$(General_dir)
	$(mkinstalldirs) $(DESTDIR)$(List_dir)

	$(INSTALL_DATA) $(srcdir)/3D.category $(DESTDIR)$(ThreeDdir)/.category
	$(INSTALL_DATA) $(srcdir)/Classical.category $(DESTDIR)$(Classicaldir)/.category
	$(INSTALL_DATA) $(srcdir)/Colourful.category $(DESTDIR)$(Colourfuldir)/.category
	$(INSTALL_DATA) $(srcdir)/Financial.category $(DESTDIR)$(Financialdir)/.category
	$(INSTALL_DATA) $(srcdir)/General.category $(DESTDIR)$(Generaldir)/.category
	$(INSTALL_DATA) $(srcdir)/List.category $(DESTDIR)$(Listdir)/.category

uninstall-local:
	rm -rf  $(DESTDIR)$(ThreeD_dir)
	rm -rf  $(DESTDIR)$(Classical_dir)
	rm -rf  $(DESTDIR)$(Colourful_dir)
	rm -rf  $(DESTDIR)$(Financial_dir)
	rm -rf  $(DESTDIR)$(General_dir)
	rm -rf  $(DESTDIR)$(List_dir)
