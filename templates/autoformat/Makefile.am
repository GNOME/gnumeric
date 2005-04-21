autoformat_categories = 3D Classical Colourful Financial General List
autoformats = \
	autoformat.3D.button.xml.in		\
	autoformat.3D.list.xml.in		\
	\
	autoformat.Classical.cool.xml.in	\
	autoformat.Classical.simple.xml.in	\
	autoformat.Classical.trendy.xml.in	\
	\
	autoformat.Colourful.banana.xml.in	\
	autoformat.Colourful.black.xml.in	\
	autoformat.Colourful.blue.xml.in	\
	autoformat.Colourful.orange.xml.in	\
	autoformat.Colourful.vanilla.xml.in	\
	\
	autoformat.Financial.basic.xml.in	\
	autoformat.Financial.desert.xml.in	\
	autoformat.Financial.ice.xml.in		\
	autoformat.Financial.modern.xml.in	\
	autoformat.Financial.purple.xml.in	\
	\
	autoformat.General.advanced.xml.in	\
	autoformat.General.basic.xml.in		\
	autoformat.General.empty.xml.in		\
	autoformat.General.table.xml.in		\
	\
	autoformat.List.basic.xml.in		\
	autoformat.List.green.xml.in		\
	autoformat.List.lila.xml.in		\
	autoformat.List.simple.xml.in

EXTRA_DIST = $(autoformats) $(autoformat_categories:=.category.in)

autoformatdir = $(gnumeric_datadir)/autoformat-templates

install-data-local:
	@echo "AutoFormats" ;						\
	for cat in $(autoformat_categories) ; do			\
		dir=$(DESTDIR)$(autoformatdir)/$$cat ;		\
		echo "-> $$cat" ;					\
		$(mkinstalldirs) $$dir ;				\
		sed -e 's/ _description *=/ description=/'		\
		    < $(srcdir)/$$cat.category.in			\
		    > $$dir/.category 	;				\
		for f in $(srcdir)/autoformat.$$cat.*.xml.in ; do	\
			dst=$$dir/`basename $$f | sed -e 's/\.in//'` ;	\
			echo "$$dst";					\
			sed 						\
				-e 's/ _author *=/ author=/'		\
				-e 's/ _name *=/ name=/' 		\
				-e 's/ _description *=/ description=/'	\
			    < $$f 					\
			    > $$dst ;					\
		done							\
	done

uninstall-local:
	for cat in $(autoformat_categories) ; do			\
		rm -rf $(DESTDIR)$(autoformatdir)/$$cat ;	\
	done
