noinst_LTLIBRARIES = libgoffice-gui-utils.la

BUILT_SOURCES =				\
	go-marshalers.h			\
	go-marshalers.c

libgoffice_gui_utils_la_SOURCES =	\
	$(BUILT_SOURCES)		\
	go-combo-box.c			\
	go-combo-box.h			\
	go-color-group.c		\
	go-color-group.h		\
	go-color-palette.c		\
	go-color-palette.h		\
	go-combo-color.c		\
	go-combo-color.h		\
	go-combo-pixmaps.h		\
	go-combo-pixmaps.c		\
	\
	go-action-combo-color.c 	\
	go-action-combo-color.h 	\
	go-action-combo-pixmaps.c 	\
	go-action-combo-pixmaps.h 	\
	go-action-combo-stack.c 	\
	go-action-combo-stack.h 	\
	go-action-combo-text.c 		\
	go-action-combo-text.h
#	go-dock-band.c			\
#	go-dock-band.h			\
#	go-dock-item-grip.c		\
#	go-dock-item-grip.h		\
#	go-dock-item.c			\
#	go-dock-item.h			\
#	go-dock-layout.c		\
#	go-dock-layout.h		\
#	go-dock.c			\
#	go-dock.h

go-marshalers.h : go-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=go_ > $@
go-marshalers.c : go-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=go_ > $@.tmp
	echo '/* This file has been automatically generated.  Do not edit.  */' >$@
	echo '#include "'$*.h'"' >>$@
	cat $@.tmp >>$@
	rm -f $@.tmp

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

EXTRA_DIST = go-marshalers.list
