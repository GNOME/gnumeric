all:

debug:: moduleset
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug

release:: moduleset
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) release

debug_shell:: moduleset
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug shell

release_shell:: moduleset
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) release shell

clean:
	rm -rf debug release

EXTRA_DIST = gnumeric.nsi.in build gtkrc moduleset.in jhbuildrc.py
.PHONY : debug release debug_shell release_shell
