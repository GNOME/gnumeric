#ifndef GNUMERIC_LIBGNUMERIC_H
#define GNUMERIC_LIBGNUMERIC_H

#include <popt.h>

typedef enum {GnmApplication, GnmComponent} GnmProgramType;

extern struct poptOption const gnumeric_popt_options [];
extern int gnumeric_debugging;
extern int dependency_debugging;
extern int expression_sharing_debugging;
extern int immediate_exit_flag;
extern int print_debugging;
extern gboolean initial_workbook_open_complete;
extern char *x_geometry;

void init_init (char const* gnumeric_binary);
void gnm_common_init	(gboolean fast);
int  gnm_dump_func_defs (char const* filename, gboolean def_or_state);
void gnm_shutdown	(void);

#endif
