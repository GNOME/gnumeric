# Do not build by default until the libtool problems with
# a static libpython are resolved.
if WITH_PYTHON
PYTHON_DIR = python
PYTHON_LOADER_DIR = python-loader
PYFUNC_DIR = py-func
GNOME_GLOSSARY_DIR = gnome-glossary
else
PYTHON_DIR = 
PYTHON_LOADER_DIR = 
PYFUNC_DIR = 
GNOME_GLOSSARY_DIR = 
endif

if WITH_EXCEL
EXCEL_DIR=excel
else
EXCEL_DIR=
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

if WITH_GB
GB_DIR = gb
else
GB_DIR =
endif

if WITH_GDA
GDA_DIR=gda
else
GDA_DIR=
endif

if WITH_PSICONV
PSICONV_DIR=psiconv
else
PSICONV_DIR=
endif

SUBDIRS_FILE_FORMATS = $(EXCEL_DIR) lotus-123 oleo sc sylk xbase html dif \
	xml_sax applix mps $(GNOME_GLOSSARY_DIR) $(PSICONV_DIR)

SUBDIRS_FUNCTIONS = numtheory fn-date fn-eng fn-logical fn-string derivatives \
	$(PYTHON_DIR) $(PERL_DIR) $(GUILE_DIR) $(GB_DIR) \
	$(GDA_DIR) $(PYFUNC_DIR)

SUBDIRS_LOADERS = $(PYTHON_LOADER_DIR)

SUBDIRS_DATA_SOURCES = sample_datasource

SUBDIRS = $(SUBDIRS_FILE_FORMATS) $(SUBDIRS_FUNCTIONS) $(SUBDIRS_LOADERS) $(SUBDIRS_DATA_SOURCES)
