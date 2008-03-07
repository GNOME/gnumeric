iconsdir = $(gnumeric_icondir)

# keep in sync with gnumeric/src/libgnumeric.c:gnm_common_init
appicondir = $(datadir)/pixmaps
appicon_DATA = gnome-gnumeric.png \
               gnome-application-x-gnumeric.png \
               gnome-application-x-xls.png \
               gnome-application-x-applix-spreadsheet.png \
               gnome-application-vnd.lotus-1-2-3.png \
               gnome-application-x-generic-spreadsheet.png \
	       win32-gnumeric.ico

icons_DATA = \
	$(appicon_DATA)		\
	orient-horizontal.png	\
	orient-vertical.png	\
	right-down.png		\
	down-right.png	\
	stf-druid.png

plot_icondir = $(gnumeric_icondir)
plot_icon_DATA = \
	gnumeric-about.png

EXTRA_DIST = $(icons_DATA) $(plot_icon_DATA)
