# First digit after "t" the type of test:
#
# 1xxx: Evaluation
# 10xx:   Large groups of functions
# 18xx:   Operators
# 19xx:   Evaluation modes, regressions
#
# 5xxx: Importers
# 59xx:   Minor formats
#
# 6xxx: Exporters
# 69xx:   Minor formats
#
# 7xxx: Features
#
# 8xxx: Runs with valgrind and similar tools.
# 80xx:   Valgrind
# 81xx:   Stuff from tools/
#
# 90xx: Misc stuff.

TESTS = t[0-9][0-9][0-9][0-9]-*.pl

import-db:
	for t in t5[0-9][0-9][0-9]-*.pl; do echo "*** $$t ***"; $(PERL) $$t create-db; done


EXTRA_DIST = GnumericTest.pm common.supp normalize-gnumeric $(TESTS)
.PHONY: import-db $(TESTS)
.NOTPARALLEL:
