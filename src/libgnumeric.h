#ifndef GNUMERIC_LIBGNUMERIC_H
#define GNUMERIC_LIBGNUMERIC_H

#ifdef WIN32
#define POPT_STATIC
#endif
#include <popt.h>

extern int	 gnumeric_debugging;
extern int	 dependency_debugging;
extern int	 expression_sharing_debugging;
extern int	 immediate_exit_flag;
extern int	 print_debugging;
extern gboolean	 initial_workbook_open_complete;
extern char	*x_geometry;
extern char	*gnumeric_lib_dir;
extern char	*gnumeric_data_dir;
extern char	*gnumeric_icon_dir;
extern char	*gnumeric_locale_dir;

void gnm_pre_parse_init (char const* gnumeric_binary);
void gnm_common_init	(gboolean fast);
int  gnm_dump_func_defs (char const* filename, gboolean def_or_state);
void gnm_shutdown	(void);

#endif /* GNUMERIC_LIBGNUMERIC_H */
