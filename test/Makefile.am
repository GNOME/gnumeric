# First digit after is the type of test:
#
# 1xxx: Evaluation
# 10xx:   Large groups of functions
# 18xx:   Operators
# 19xx:   Evaluation modes, regressions
#
# 5xxx: Importers
# 59xx:   Minor formats
#
# 8xxx: Runs with valgrind and similar tools.
# 80xx:   Valgrind
# 81xx:   Stuff from tools/

TESTS = $(wildcard t[0-9][0-9][0-9][0-9]-*.pl)

testsuite: $(TESTS)

importdb:
	for t in t5[0-9][0-9][0-9]-*.pl; do echo "*** $$t ***"; $(PERL) $$t create-db; done

$(TESTS):
	@echo "*** $@ ***" && $(PERL) $@


EXTRA_DIST = GnumericTest.pm; $(TESTS)
.PHONY: testsuite importdb $(TESTS) clean
.NOTPARALLEL:
