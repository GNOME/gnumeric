#ifndef GNUMERIC_NLSOLVE_BOOT_H
#define GNUMERIC_NLSOLVE_BOOT_H

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-output.h>

void
nlsolve_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output);

#endif
