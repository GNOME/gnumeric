SUBDIRS_FILE_FORMATS = excel lotus-123 oleo sc sylk xbase html dif qpro \
	plan-perfect applix openoffice

if ENABLE_SOLVER
  SUBDIRS_FILE_FORMATS += mps
endif
if WITH_PSICONV
  SUBDIRS_FILE_FORMATS += psiconv
endif
if WITH_PARADOX
  SUBDIRS_FILE_FORMATS += paradox
endif

SUBDIRS_FUNCTIONS = numtheory fn-database fn-date fn-eng fn-erlang \
	fn-financial fn-info fn-logical fn-complex fn-lookup fn-math \
	fn-r fn-stat fn-string fn-random fn-tsa derivatives

if WITH_GDA
  SUBDIRS_FUNCTIONS += gda
endif
if WITH_GNOMEDB
  SUBDIRS_FUNCTIONS += gnome-db
endif

SUBDIRS_LOADERS =
SUBDIRS_EXAMPLES = uihello

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

if WITH_CORBA
  SUBDIRS_LOADERS += corba
endif

SUBDIRS_DATA_SOURCES = sample_datasource


if PLUGIN_LIST_GIVEN
  SUBDIRS = @PLUGIN_LIST@
else
  SUBDIRS = $(SUBDIRS_FILE_FORMATS) $(SUBDIRS_FUNCTIONS) \
	$(SUBDIRS_LOADERS) $(SUBDIRS_DATA_SOURCES) $(SUBDIRS_EXAMPLES)
endif
