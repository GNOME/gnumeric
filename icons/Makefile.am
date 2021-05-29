SUBDIRS = 16x16 22x22 24x24 32x32 48x48 64x64 256x256

EXTRA_DIST = fake-index-theme win32-gnumeric.ico

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

fake_index_theme_target =
if WITH_WIN32
fake_index_theme_target += install-fake-index-theme

install-fake-index-theme: fake-index-theme
	install -D -m 0644 fake-index-theme $(datadir)/icons/hicolor/index.theme

winicondir = $(datadir)/pixmaps
winicon_DATA = win32-gnumeric.ico
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
