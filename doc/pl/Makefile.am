lang=pl
locale=pl_PL
gnumeric_helpdir = $(datadir)/gnome/help/gnumeric/$(lang)
gnumeric_help_DATA = topic.dat

dist-hook : app-dist-hook

CLEANFILES = omf_timestamp

EXTRA_DIST = topic.dat

docname ?= gnumeric

uninstall-hook:
	$(RM) $(docdir)/*.css $(docdir)/*.html $(docdir)/*.sgml

include $(srcdir)/../gnumeric-doc.make
