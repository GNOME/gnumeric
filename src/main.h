#ifndef GNUMERIC_MAIN_H
#define GNUMERIC_MAIN_H

#include <popt.h>

extern const struct poptOption gnumeric_popt_options [];
extern int gnumeric_debugging;
extern int dependency_debugging;
extern int expression_sharing_debugging;
extern gboolean initial_workbook_open_complete;
extern char *x_geometry;

poptContext gnumeric_arg_parse (int argc, char *argv []);

#endif
