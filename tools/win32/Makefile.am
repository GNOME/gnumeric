export start module target

all: moduleset gnumeric.nsi

debug:: all
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug

release:: all
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) release

DISTCLEANFILES = debug release

EXTRA_DIST = gnumeric.nsi.in gtkrc moduleset.in \
	build jhbuildrc.py


if WITH_WIN32
install-data-hook:
	install gnumeric.nsi ${prefix}

uninstall-local:
	rm $(prefix)/gnumeric.nsi
endif

.PHONY : debug release all clean
