#ifndef GNUMERIC_LIBGNUMERIC_H
#define GNUMERIC_LIBGNUMERIC_H

#include "gutils.h"

extern int	 gnumeric_debugging;
extern int	 dependency_debugging;
extern int	 expression_sharing_debugging;
extern int	 print_debugging;
extern gboolean	 initial_workbook_open_complete;
extern char	*x_geometry;

void gnm_pre_parse_init (char const* gnumeric_binary);
void gnm_common_init	(gboolean fast);
int  gnm_dump_func_defs (char const* filename,
			 int dump_type); /* changes as needed */
void gnm_shutdown	(void);

#endif /* GNUMERIC_LIBGNUMERIC_H */
