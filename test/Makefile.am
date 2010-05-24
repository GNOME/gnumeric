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

TESTS = $(wildcard $(srcdir)/t[0-9][0-9][0-9][0-9]-*.pl)
SUPPS = common.supp commondots.supp gui.supp t8003-valgrind-pdf.supp

import-db:
	for t in $(srcdir)/t5[0-9][0-9][0-9]-*.pl; do echo "*** $$t ***"; $(PERL) $$t create-db; done


EXTRA_DIST = GnumericTest.pm $(SUPPS) normalize-gnumeric $(TESTS)
.PHONY: import-db $(TESTS)
.NOTPARALLEL:
