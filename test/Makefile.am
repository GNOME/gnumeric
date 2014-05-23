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
# 5xxx: Importers
# 59xx:   Minor formats
#
# 6xxx: Exporters
# 69xx:   Minor formats
#
# 7xxx: Features
# 70xx:   Goal seek
# 71xx:   Solver
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
	t1012-burkhardt.pl			\
	t1013-crlibm.pl				\
	t1100-chitest.pl			\
	t1101-ftest.pl				\
	t1102-ttest.pl				\
	t1103-sumif.pl				\
	t1104-linest.pl				\
	t1105-gamma.pl				\
	t1800-operators.pl			\
	t1900-array.pl				\
	t1901-regress.pl			\
	t1902-array-intersection.pl		\
	t2000-names-insdel.pl			\
	t2001-func-help.pl			\
	t2002-nonascii-numbers.pl		\
	t2003-random-generators.pl		\
	t2800-style-optimizer.pl		\
	t5900-sc.pl				\
	t5901-qpro.pl				\
	t5902-applix.pl				\
	t5903-sylk.pl				\
	t5904-mps.pl				\
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
	t8000-valgrind.pl			\
	t8001-valgrind-xls.pl			\
	t8002-valgrind-gnumeric.pl		\
	t8003-valgrind-pdf.pl			\
	t8004-valgrind-xls-macro.pl		\
	t8005-valgrind-chart-fit.pl		\
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
	t9100-number-match.pl			\
	t9999-epilogue.pl

SUPPS = common.supp gui.supp t8003-valgrind-pdf.supp

import-db:
	for t in $(srcdir)/t5[0-9][0-9][0-9]-*.pl; do echo "*** $$t ***"; $(PERL) $$t create-db; done


EXTRA_DIST = GnumericTest.pm $(SUPPS) normalize-gnumeric $(TESTS)
.PHONY: import-db $(TESTS)
.NOTPARALLEL:
