#ifndef GNUMERIC_MAIN_H
#define GNUMERIC_MAIN_H

extern const struct poptOption gnumeric_popt_options [];
extern poptContext ctx;
extern int gnumeric_debugging;

void   gnumeric_arg_parse (int argc, char *argv []);

#endif
