INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_stf.la
gnum_stf_la_LDFLAGS = -module -avoid-version
gnum_stf_la_SOURCES = \
	stf.h stf.c \
	dialog-stf.h dialog-stf.c \
	dialog-stf-main-page.c dialog-stf-csv-page.c \
	dialog-stf-fixed-page.c dialog-stf-format-page.c \
	dialog-stf-preview.h dialog-stf-preview.c \
	stf-util.h stf-util.c \
	stf-separated.h stf-separated.c \
	stf-fixed.h stf-fixed.c 

gnumeric_glade_DATA = stf.glade	
gnumeric_glade_msgs = stf.glade.h

EXTRA_DIST = $(gnumeric_glade_DATA) $(gnumeric_glade_msgs)

