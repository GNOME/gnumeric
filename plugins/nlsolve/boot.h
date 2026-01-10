#ifndef GNM_NLSOLVE_BOOT_H_
#define GNM_NLSOLVE_BOOT_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-output.h>

void
nlsolve_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output);

#endif
