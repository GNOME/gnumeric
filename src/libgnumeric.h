#ifndef GNUMERIC_LIBGNUMERIC_H
#define GNUMERIC_LIBGNUMERIC_H

#include "gnumeric.h"

#ifndef GNM_VAR_DECL
#  ifdef WIN32
#    ifdef GNUMERIC_INTERNAL
#      define GNM_VAR_DECL __declspec(dllexport)
#    else
#      define GNM_VAR_DECL extern __declspec(dllimport)
#    endif
#  else
#    define GNM_VAR_DECL extern
#  endif
#endif /* GNM_VAR_DECL */

char const **gnm_pre_parse_init     (int argc, gchar const **argv);
void	     gnm_pre_parse_shutdown (void);
void	     gnm_init		    (gboolean fast);
void	     gnm_shutdown	    (void);

GOptionGroup *gnm_get_option_group (void);

GNM_VAR_DECL gboolean	initial_workbook_open_complete;

/* Internal */
int gnm_dump_func_defs (char const* filename, int dump_type); /* changes as needed */

#endif /* GNUMERIC_LIBGNUMERIC_H */
