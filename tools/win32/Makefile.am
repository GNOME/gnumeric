all:

debug::
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug

release::
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) release

debug_shell::
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug_shell

release_shell::
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug_shell

clean:
	rm -rf debug release

EXTRA_DIST = gnumeric.nsi.in build gtkrc moduleset.in jhbuildrc-debug jhbuildrc-release
.PHONY : debug release debug_shell release_shell
