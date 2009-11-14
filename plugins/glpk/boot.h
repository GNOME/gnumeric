#ifndef GNUMERIC_GLPK_BOOT_H
#define GNUMERIC_GLPK_BOOT_H

#include "gnumeric.h"
#include <goffice/goffice.h>
#include <gsf/gsf-output.h>

void
glpk_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		WorkbookView const *wb_view, GsfOutput *output);

#endif
