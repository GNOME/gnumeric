# First digit after "t" the type of test:
#
# 1xxx: Evaluation
# 10xx:   Large groups of functions
# 11xx:   Specific functions
# 18xx:   Operators
# 19xx:   Evaluation modes, regressions
#
# 2xxx: Internal sstest
#
# 3xxx: Introspection tests
#
# 5xxx: Importers
# 5800:   csv
# 59xx:   Minor formats with import-db
#
# 6xxx: Exporters
# 69xx:   Minor formats
#
# 7xxx: Features
# 70xx:   Goal seek
# 71xx:   Solver
# 72xx:   Analysis tools
#
# 8xxx: Runs with valgrind and similar tools.
# 80xx:   Valgrind
# 81xx:   Stuff from tools/
# 82xx:   G_SLICE debug and similar
#
# 90xx: Misc stuff.

TESTS_EXTENSIONS=.pl
TESTS =	t1000-statfuns.pl			\
	t1001-mathfuns.pl			\
	t1002-finfuns.pl			\
	t1003-engfuns.pl			\
	t1004-textfuns.pl			\
	t1005-logfuns.pl			\
	t1006-datefuns.pl			\
	t1007-dbfuns.pl				\
	t1008-infofuns.pl			\
	t1009-lookfuns.pl			\
	t1010-bitwise.pl			\
	t1011-yalta2008.pl			\
	t1012-burkardt.pl			\
	t1013-crlibm.pl				\
	t1014-gsl.pl				\
	t1015-complex.pl			\
	t1016-database.pl			\
	t1017-countif.pl			\
	t1018-ifs-funcs.pl			\
	t1019-numtheory.pl			\
	t1100-chitest.pl			\
	t1101-ftest.pl				\
	t1102-ttest.pl				\
	t1103-sumif.pl				\
	t1104-linest.pl				\
	t1105-gamma.pl				\
	t1106-vdb.pl				\
	t1107-cronbach.pl			\
	t1108-ilog.pl				\
	t1109-unique.pl				\
	t1800-operators.pl			\
	t1900-array.pl				\
	t1901-regress.pl			\
	t1902-array-intersection.pl		\
	t1903-intersection-tests.pl		\
	t2000-names-insdel.pl			\
	t2001-func-help.pl			\
	t2002-nonascii-numbers.pl		\
	t2003-random-generators.pl		\
	t2004-insdel-colrow.pl			\
	t2005-recalc.pl				\
	t2006-cond-format-deps.pl		\
	t2800-style-optimizer.pl		\
	t5800-csv-date.pl			\
	t5801-csv-number.pl			\
	t5900-sc.pl				\
	t5901-qpro.pl				\
	t5902-applix.pl				\
	t5903-sylk.pl				\
	t5904-mps.pl				\
	t5905-guppi.pl				\
	t5906-dif.pl				\
	t6000-statfuns.pl			\
	t6001-mathfuns.pl			\
	t6002-objs.pl				\
	t6003-lookfuns.pl			\
	t6004-finfuns.pl			\
	t6050-vba-roundtrip.pl			\
	t6100-statfuns-ods.pl			\
	t6101-mathfuns-ods.pl			\
	t6102-objs-ods.pl			\
	t6103-lookfuns-ods.pl			\
	t6104-finfuns-ods.pl			\
	t6150-ods-syntax.pl			\
	t6151-gnumeric-syntax.pl		\
	t6152-xlsx-syntax.pl			\
	t6160-ods-deterministic.pl		\
	t6161-xlsx-deterministic.pl		\
	t6162-gnumeric-deterministic.pl		\
	t6163-ods-strict-deterministic.pl	\
	t6500-strings.pl			\
	t6501-numbers.pl			\
	t6502-styles.pl				\
	t6503-page-setup.pl			\
	t6504-formula.pl			\
	t6505-colrow.pl				\
	t6506-cell-comments.pl			\
	t6507-split-panes.pl			\
	t6508-sheet-formatting.pl		\
	t6509-validation.pl			\
	t6510-auto-filter.pl			\
	t6511-solver.pl				\
	t6512-format.pl				\
	t6513-merges.pl				\
	t6514-rich-text.pl			\
	t6515-cond-format.pl			\
	t6516-graph.pl				\
	t6517-names.pl				\
	t6518-objects.pl			\
	t6519-selection.pl			\
	t6520-sheet-tab.pl			\
	t6521-sheet-names.pl			\
	t6522-hlink.pl				\
	t6590-samples.pl			\
	t6900-sylk.pl				\
	t6910-graph-image-svg.pl		\
	t6920-cplex.pl				\
	t6921-lpsolve.pl			\
	t7000-goal-seek.pl			\
	t7100-solver-blend.pl			\
	t7101-solver-afiro.pl			\
	t7110-solver-Misra1a.pl			\
	t7111-solver-Chwirut2.pl		\
	t7112-solver-Chwirut1.pl		\
	t7113-solver-Lanczos3.pl		\
	t7114-solver-Gauss1.pl			\
	t7115-solver-Gauss2.pl			\
	t7116-solver-DanWood.pl			\
	t7117-solver-Misra1b.pl			\
	t7120-solver-Kirby2.pl			\
	t7121-solver-Hahn1.pl			\
	t7122-solver-Nelson.pl			\
	t7123-solver-MGH17.pl			\
	t7124-solver-Lanczos1.pl		\
	t7125-solver-Lanczos2.pl		\
	t7126-solver-Gauss3.pl			\
	t7127-solver-Misra1c.pl			\
	t7128-solver-Misra1d.pl			\
	t7129-solver-Roszman1.pl		\
	t7130-solver-ENSO.pl			\
	t7140-solver-MGH09.pl			\
	t7141-solver-Thurber.pl			\
	t7142-solver-BoxBOD.pl			\
	t7143-solver-Rat42.pl			\
	t7144-solver-MGH10.pl			\
	t7145-solver-Eckerle4.pl		\
	t7146-solver-Rat43.pl			\
	t7147-solver-Bennett5.pl		\
	t7200-regression.pl			\
	t7201-anova.pl				\
	t8000-valgrind.pl			\
	t8001-valgrind-xls.pl			\
	t8002-valgrind-gnumeric.pl		\
	t8003-valgrind-pdf.pl			\
	t8004-valgrind-xls-macro.pl		\
	t8005-valgrind-chart-fit.pl		\
	t8006-valgrind-ods-no-styles.pl		\
	t8010-valgrind-ods.pl			\
	t8011-valgrind-xlsx.pl			\
	t8050-valgrind-sstest.pl		\
	t8100-null-false.pl			\
	t8101-gfree.pl				\
	t8102-glade-visible.pl			\
	t8103-gtk.pl				\
	t8104-glade-parsing.pl			\
	t8200-gslice1.pl			\
	t8201-gslice2.pl			\
	t8202-gslice3.pl			\
	t9000-ssindex.pl			\
	t9001-ssconvert-resize.pl		\
	t9002-ssdiff-self.pl			\
	t9003-ssdiff-xml.pl			\
	t9004-ssdiff-highlight.pl		\
	t9005-ssconvert-merge.pl		\
	t9006-ssconvert-split.pl		\
	t9007-ssconvert-sheet.pl		\
	t9010-ssgrep.pl				\
	t9100-number-match.pl			\
	t9999-epilogue.pl

SUPPS = common.supp gui.supp t8003-valgrind-pdf.supp

INTROSPECTION_TSTS = \
	t3000-introspection-basic.pl		\
	t3001-introspection-simple.pl		\
	t3002-introspection-io.pl		\
	t3003-introspection-plugins.pl		\
	t3004-introspection-overrides.pl

INTROSPECTION_SUPPS = \
	$(INTROSPECTION_TSTS:.pl=.py)

# Activate only when we have both introspection and the "gi" module
if HAVE_INTROSPECTION
if HAVE_PYTHON_GI
  TESTS += $(INTROSPECTION_TSTS)
endif
endif


import-db:
	for t in $(srcdir)/t59[0-9][0-9]-*.pl; do echo "*** $$t ***"; $(PERL) $$t create-db; done

EXTRA_DIST = GnumericTest.pm \
	$(SUPPS) $(TESTS) \
	$(INTROSPECTION_SUPPS) $(INTROSPECTION_TSTS) \
	normalize-gnumeric \
	ods-ext-schema.patch 

.PHONY: import-db
.NOTPARALLEL:
