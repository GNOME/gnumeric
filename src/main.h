#ifndef GNUMERIC_MAIN_H
#define GNUMERIC_MAIN_H

#include <popt-gnome.h>

extern const struct poptOption gnumeric_popt_options [];
extern poptContext ctx;
extern int gnumeric_debugging;
extern int style_debugging;
extern int dependency_debugging;
extern gboolean initial_workbook_open_complete;
extern char *x_geometry;

void   gnumeric_arg_parse (int argc, char *argv []);

/*
 * A necessary bogosity that is required to avoid bug #7948 with current
 * versions of guile
 */
gboolean has_gnumeric_been_compiled_with_guile_support (void);

#endif
