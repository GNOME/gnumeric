# Do not build by default until the libtool problems with
# a static libpython are resolved.
if WITH_PYTHON
PYTHON = python
else
PYTHON = 
endif

if WITH_PERL
PERL = perl
else
PERL =
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

SUBDIRS = sample excel lotus-123 oleo sc sylk ff-csv text xbase html dif ff-stf $(PYTHON) $(GUILE_DIR) $(GB_DIR)
