lang=de
locale=de_DE
gnumeric_helpdir = $(datadir)/gnome/help/gnumeric/$(lang)
gnumeric_help_DATA = topic.dat

dist-hook : app-dist-hook

EXTRA_DIST = topic.dat

docname ?= gnumeric

include $(srcdir)/../gnumeric-doc.make
