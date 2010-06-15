SUBDIRS = 16x16 22x22 24x24 32x32 48x48 scalable

# keep in sync with gnumeric/src/libgnumeric.c:gnm_common_init
mimeicondir = $(datadir)/pixmaps
mimeicon_DATA = gnome-application-x-gnumeric.png \
               gnome-application-x-xls.png \
               gnome-application-x-applix-spreadsheet.png \
               gnome-application-vnd.lotus-1-2-3.png \
               gnome-application-x-generic-spreadsheet.png \
	       win32-gnumeric.ico

iconsdir = $(gnumeric_icondir)
icons_DATA = \
	$(mimeicon_DATA)	\
	orient-horizontal.png	\
	orient-vertical.png	\
	right-down.png		\
	down-right.png		\
	gnumeric-about.png	\
	stf-druid.png

EXTRA_DIST = $(icons_DATA) $(mimeicon_DATA) fake-index-theme

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

fake_index_theme_target =
if WITH_WIN32
fake_index_theme_target += install-fake-index-theme

install-fake-index-theme: fake-index-theme
	install -m 0644 fake-index-theme $(datadir)/icons/hicolor/index.theme
endif

install-data-hook: update-icon-cache $(fake_index_theme_target)
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi
