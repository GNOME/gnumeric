INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
noinst_LIBRARIES = libGnumericIDL.a
else
endif

GNUMERIC_GENERATED = 		\
	Gnumeric-common.c		\
	Gnumeric-skels.c		\
	Gnumeric-stubs.c		\
	Gnumeric.h

GNUMERIC_GRAPHS_GENERATED = 	\
	gnumeric-graphs-common.c	\
	gnumeric-graphs-skels.c		\
	gnumeric-graphs-stubs.c		\
	gnumeric-graphs.h	

libGnumericIDL_a_SOURCES = $(GNUMERIC_GENERATED) $(GNUMERIC_GRAPHS_GENERATED)

$(GNUMERIC_GENERATED): $(srcdir)/Gnumeric.idl 
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/Gnumeric.idl 
	touch my_gnumeric_idl

$(GNUMERIC_GRAPHS_GENERATED): $(srcdir)/gnumeric-graphs.idl
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/gnumeric-graphs.idl 
	touch my_gnumeric_graphs_idl

idldir   = $(gnumeric_datadir)/idl
idl_DATA = gnumeric-graphs.idl Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
CLEANFILES  += my_gnumeric_idl $(GNUMERIC_GENERATED) \
	       my_gnumeric_graphs_idl $(GNUMERIC_GRAPHS_GENERATED)

dist-hook :
	for file in $(libGnumericIDL_a_SOURCES); do \
	    rm -f $(distdir)/$$file ; \
	done
