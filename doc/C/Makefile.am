# Note: all xml files and media files are handled from ../Makefile.am

noinst_DATA=
EXTRA_DIST=

if ENABLE_PDFDOCS
noinst_DATA += gnumeric.pdf
endif

if ENABLE_PDF_VIA_DBCONTEXT
gnumeric.pdf:
	env TEXINPUTS=$(srcdir):.: dbcontext -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=600' \
		-o gnumeric.tex $(srcdir)/gnumeric.xml
	sed -i -e 's/\xE2\xA8\x89/\\ensuremath{\\times}/' gnumeric.tex
	sed -i -e 's/\\begin{verbatim}\([^_]*\)\\end{verbatim}/\\texttt{\1}/g' gnumeric.tex
	sed -i -e 's/\\texttt{\\emph{\\small{\(.*\)}}}:/\\texttt{\\emph{\1}}:/g' gnumeric.tex
	sed -i -e 's/ *>{} */\\ensuremath{{}>{}}/g' gnumeric.tex
	sed -i -e 's/ *<{} */\\ensuremath{{}<{}}/g' gnumeric.tex
	sed -i -e 's/\\makeglossary/\\makeglossary\n\\setlength{\\headheight}{36pt}/g' gnumeric.tex
	env TEXMFCNF=$(srcdir): \
		TEXINPUTS=$(srcdir):/usr/share/texmf/tex/context/dbcontext/style: \
		texexec --pdf --mode=A4 --verbose gnumeric.tex
endif

if ENABLE_PDF_VIA_DBLATEX
gnumeric.pdf:
	dblatex -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=0.6' \
		-P latex.encoding=utf8 \
		-P latex.output.revhistory=0 \
		-o gnumeric.tex $(srcdir)/gnumeric.xml
	sed -i -e 's/\xE2\xA8\x89/\\ensuremath{\\times}/' gnumeric.tex
	sed -i -e 's/\\begin{verbatim}\([^_]*\)\\end{verbatim}/\\texttt{\1}/g' gnumeric.tex
	sed -i -e 's/\texttt{\emph{\small{\(.*\)}}}:/\texttt{\emph{\1}}:/g' gnumeric.tex
	sed -i -e 's/ *>{} */\\ensuremath{{}>{}}/g' gnumeric.tex
	sed -i -e 's/ *<{} */\\ensuremath{{}<{}}/g' gnumeric.tex
	sed -i -e 's/\\makeglossary/\\makeglossary\n\\setlength{\\headheight}{36pt}\n\\input{textdefs.tex}/g' gnumeric.tex
	for runs in $$(seq 1 4); do \
		env TEXINPUTS=$(srcdir): \
			pdflatex -interaction nonstopmode gnumeric.tex ; \
	done
endif

manpagedir   = $(mandir)/man1
manpage_DATA = gnumeric.1 ssconvert.1 ssindex.1 ssgrep.1 ssdiff.1

winhelp_DATA =
winhelpdir=$(gnumeric_datadir)/$(subdir)
if WITH_WIN32
winhelp_DATA += gnumeric.chm gnumeric.hhmap

gnumeric.hhmap: chm-stamp
	-(cd chm && wine 'C:\Program Files\HTML Help Workshop\hhc.exe' gnumeric.hhp)
	cp chm/gnumeric.hhmap .

gnumeric.chm: gnumeric.hhmap
	cp chm/gnumeric.chm .

endif

EXTRA_DIST += $(manpage_DATA) \
	gnumeric-docbook-2-htmlhelp.xsl

if ENABLE_PDFDOCS
EXTRA_DIST += gnumeric.pdf
endif
