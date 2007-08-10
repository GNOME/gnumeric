#ifndef GNUMERIC_LIBGNUMERIC_H
#define GNUMERIC_LIBGNUMERIC_H

#include "gutils.h"

extern int	 gnumeric_debugging;
extern int	 dependency_debugging;
extern int	 expression_sharing_debugging;
extern int	 print_debugging;
extern gboolean	 initial_workbook_open_complete;
extern char	*x_geometry;

char const **gnm_pre_parse_init     (int argc, gchar const **argv);
void	     gnm_pre_parse_shutdown (void);
void	     gnm_init		    (gboolean fast);
void	     gnm_shutdown	    (void);

/* Internal */
int gnm_dump_func_defs (char const* filename, int dump_type); /* changes as needed */

#endif /* GNUMERIC_LIBGNUMERIC_H */
