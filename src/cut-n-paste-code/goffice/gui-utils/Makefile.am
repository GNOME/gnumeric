noinst_LTLIBRARIES = libgoffice-gui-utils.la

libgoffice_gui_utils_la_SOURCES =	\
	go-action-combo-color.c 	\
	go-action-combo-color.h 	\
	go-action-combo-pixmaps.c 	\
	go-action-combo-pixmaps.h 	\
	go-action-combo-stack.c 	\
	go-action-combo-stack.h 	\
	go-action-combo-text.c 		\
	go-action-combo-text.h 

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)
