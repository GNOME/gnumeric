# Do not build by default until the libtool problems with
# a static libpython are resolved.
if WITH_PYTHON
PYTHON_LOADER_DIR = python-loader
PYFUNC_DIR = py-func
GNOME_GLOSSARY_DIR = gnome-glossary
else
PYTHON_LOADER_DIR = 
PYFUNC_DIR = 
GNOME_GLOSSARY_DIR = 
endif

if WITH_MONO
MONO_DIR=mono
else
MONO_DIR=
endif

if WITH_CORBA
CORBA_DIR = corba
else
CORBA_DIR =
endif

if WITH_GNOMEDB
GNOMEDB_DIR = gnome-db
else
GNOMEDB_DIR =
endif

if WITH_PERL
PERL_DIR = perl
else
PERL_DIR =
endif

if WITH_GUILE
GUILE_DIR = guile
else
GUILE_DIR =
endif

if WITH_GDA
GDA_DIR=gda
else
GDA_DIR=
endif

if ENABLE_SOLVER
MPS_DIR=mps
else
MPS_DIR=
endif

if WITH_PSICONV
PSICONV_DIR=psiconv
else
PSICONV_DIR=
endif

if WITH_PARADOX
PARADOX_DIR=paradox
else
PARADOX_DIR=
endif

SUBDIRS_FILE_FORMATS = excel lotus-123 oleo sc sylk xbase html dif qpro \
	plan-perfect xml_sax applix $(MPS_DIR) openoffice $(PSICONV_DIR) $(PARADOX_DIR)

SUBDIRS_FUNCTIONS = numtheory fn-database fn-date fn-eng fn-erlang fn-financial fn-info \
	fn-logical fn-complex fn-lookup fn-math fn-stat fn-string fn-random \
	derivatives \
	$(PERL_DIR) $(GUILE_DIR) \
	$(GDA_DIR) $(GNOMEDB_DIR)

SUBDIRS_LOADERS = $(PYTHON_LOADER_DIR) $(CORBA_DIR)

SUBDIRS_DATA_SOURCES = sample_datasource

SUBDIRS_EXAMPLES = $(GNOME_GLOSSARY_DIR) $(PYFUNC_DIR)
# SUBDIRS_EXAMPLES += uiplugins
## Don't put comment on an assignment line!  It can trigger an automake bug.

if PLUGIN_LIST_GIVEN
  SUBDIRS = @PLUGIN_LIST@
else
  SUBDIRS = $(SUBDIRS_FILE_FORMATS) $(SUBDIRS_FUNCTIONS) $(SUBDIRS_LOADERS) $(SUBDIRS_DATA_SOURCES) $(SUBDIRS_EXAMPLES)
endif
