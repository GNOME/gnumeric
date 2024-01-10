SUBDIRS_FILE_FORMATS = excel lotus-123 oleo sc sylk xbase html dif qpro \
	plan-perfect applix openoffice mps
SUBDIRS_FUNCTIONS = fn-numtheory fn-christian-date fn-database fn-date \
	fn-eng fn-erlang fn-financial fn-flt \
	fn-hebrew-date fn-info fn-logical fn-complex fn-lookup \
	fn-math fn-r fn-stat fn-string fn-random fn-tsa fn-derivatives
SUBDIRS_LOADERS = excelplugins
SUBDIRS_DATA_SOURCES = sample_datasource
SUBDIRS_SOLVERS = nlsolve lpsolve glpk
SUBDIRS_EXAMPLES = uihello

if WITH_PSICONV
  SUBDIRS_FILE_FORMATS += psiconv
endif
if WITH_PARADOX
  SUBDIRS_FILE_FORMATS += paradox
endif

if WITH_GDA
  SUBDIRS_FUNCTIONS += gda
endif
if WITH_GNOMEDB
  SUBDIRS_FUNCTIONS += gnome-db
endif

# Do not build by default until the libtool problems with
# a static libpython are resolved.
if WITH_PYTHON
  SUBDIRS_LOADERS += python-loader
  SUBDIRS_EXAMPLES += gnome-glossary py-func
endif

if WITH_PERL
  SUBDIRS_LOADERS += perl-loader
  SUBDIRS_EXAMPLES += perl-func
endif

if PLUGIN_LIST_GIVEN
  SUBDIRS = @PLUGIN_LIST@
else
  SUBDIRS = $(SUBDIRS_FILE_FORMATS) $(SUBDIRS_FUNCTIONS) $(SUBDIRS_SOLVERS) \
	$(SUBDIRS_LOADERS) $(SUBDIRS_DATA_SOURCES) $(SUBDIRS_EXAMPLES)
endif
