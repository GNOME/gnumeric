#ifndef GNUMERIC_MAIN_H
#define GNUMERIC_MAIN_H

#include <popt.h>

extern const struct poptOption gnumeric_popt_options [];
extern poptContext ctx;
extern int gnumeric_debugging;
extern int style_debugging;
extern int dependency_debugging;

void   gnumeric_arg_parse (int argc, char *argv []);

#endif
