# Do not build by default until the libtool problems with
# a static libpython are resolved.
if WITH_PYTHON
PYTHON_DIR = python
else
PYTHON_DIR = 
endif

if WITH_PERL
PERL_DIR = perl
else
PERL_DIR =
endif

if GUILE
GUILE_DIR = guile
else
GUILE_DIR =
endif

if WITH_GB
GB_DIR = gb
else
GB_DIR =
endif

SUBDIRS = excel lotus-123 oleo sc sylk text ff-csv xbase html dif plan-perfect \
	$(PYTHON_DIR) $(PERL_DIR) $(GUILE_DIR) $(GB_DIR) \
	sample numtheory gda xml2
