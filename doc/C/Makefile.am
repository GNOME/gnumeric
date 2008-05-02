SUBDIRS = figures
lang = C
locale = C
entities =  \
     about-authors.xml \
     about-history.xml \
     about-license.xml \
     analysis-complexNumbers.xml \
     analysis-goalseek.xml \
     analysis-overview.xml \
     analysis-scenarios.xml \
     analysis-simulation.xml \
     analysis-solver.xml \
     analysis-statistical.xml \
     appendix-glossary.xml \
     appendix-keybindings.xml \
     bugs.xml \
     compiling.xml \
     configuration-localization.xml \
     configuration-overview.xml \
     configuration-plugins.xml \
     configuration-preferences.xml \
     configuration-toolbars.xml \
     data-commentNlink.xml \
     data-delete.xml \
     data-entry-advanced.xml \
     data-entry-external.xml \
     data-entry.xml \
     data-format.xml \
     data-generate.xml \
     data-insert.xml \
     data-modify.xml \
     data-filter.xml \
     data-move-copy.xml \
     data-overview.xml \
     data-selections.xml \
     data-types.xml \
     documenting.xml \
     extending-functions.xml \
     extending-overview.xml \
     extending-plugins.xml \
     extending-python.xml \
     files-formats.xml \
     files-email.xml \
     files-opening.xml \
     files-overview.xml \
     files-saving.xml \
     files-ssconvert.xml \
     files-textopen.xml \
     files-textsave.xml \
     getting-involved.xml \
     graphics-drawings.xml \
     graphics-images.xml \
     graphics-overview.xml \
     graphics-plots.xml \
     graphics-widgets.xml \
     gui-menus.xml \
     gui-mouse.xml \
     gui-other-elements.xml \
     gui-overview.xml \
     gui-toolbars.xml \
     installing.xml \
     legal.xml \
     manual-usage.xml \
     morehelp.xml \
     printing.xml \
     quick-start.xml \
     welcome.xml \
     workbooks.xml \
     worksheets.xml
#	functions.xml	# added in gnumeric-doc.make

figdir = figures

include $(top_srcdir)/doc/gnumeric-doc.make

manpagedir   = $(mandir)/man1
manpage_DATA = gnumeric.1

if ENABLE_SSCONVERT
manpage_DATA += ssconvert.1
endif

if ENABLE_SSINDEX
manpage_DATA += ssindex.1
endif

if ENABLE_SSGREP
manpage_DATA += ssgrep.1
endif

# from gnumeric-doc.make
dist-hook: validate

EXTRA_DIST += $(manpage_DATA) ssconvert.1 ssindex.1 gnumeric-docbook-2-htmlhelp.xsl

if ENABLE_PDFDOCS
EXTRA_DIST += gnumeric.pdf
endif
