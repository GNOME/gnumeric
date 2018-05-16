#ifndef GNUMERIC_LPSOLVE_BOOT_H
#define GNUMERIC_LPSOLVE_BOOT_H

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-output.h>
#include <tools/gnm-solver.h>

void
lpsolve_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output);

GnmSolver *lpsolve_solver_create (GnmSolverParameters *params);

gboolean lpsolve_solver_factory_functional (GnmSolverFactory *factory,
					    WBCGtk *wbcg);

GnmSolver *lpsolve_solver_factory (GnmSolverFactory *factory,
				   GnmSolverParameters *params);

#endif
