#include <gnumeric-config.h>
#include "gnumeric.h"
#include <tools/gnm-solver.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "gnumeric-conf.h"
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define SOLVER_PROGRAM "lp_solve"
#define SOLVER_URL "http://sourceforge.net/projects/lpsolve/"
#define PRIVATE_KEY "::lpsolve::"

typedef struct {
	GnmSubSolver *parent;
	GnmSolverResult *result;
	GnmSheetRange srinput;
	enum { SEC_UNKNOWN, SEC_VALUES } section;
} GnmLPSolve;

static void
gnm_lpsolve_cleanup (GnmLPSolve *lp)
{
	gnm_sub_solver_clear (lp->parent);

	if (lp->result) {
		g_object_unref (lp->result);
		lp->result = NULL;
	}
}

static void
gnm_lpsolve_final (GnmLPSolve *lp)
{
	gnm_lpsolve_cleanup (lp);
	g_free (lp);
}

static gboolean
write_program (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (sol);
	GOFileSaver *fs;

	fs = go_file_saver_for_mime_type ("application/lpsolve");
	if (!fs) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("The LPSolve exporter is not available."));
		return FALSE;
	}

	return gnm_solver_saveas (sol, wbc, fs,
				  "program-XXXXXX.lp",
				  &subsol->program_filename,
				  err);
}

static GnmSolverResult *
gnm_lpsolve_start_solution (GnmLPSolve *lp)
{
	g_return_val_if_fail (lp->result == NULL, NULL);

	lp->result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);
	lp->result->solution = value_new_array_empty
		(range_width (&lp->srinput.range),
		 range_height (&lp->srinput.range));

	return lp->result;
}

static void
gnm_lpsolve_flush_solution (GnmLPSolve *lp)
{
	if (lp->result) {
		g_object_set (lp->parent, "result", lp->result, NULL);
		g_object_unref (lp->result);
		lp->result = NULL;
	}
}


static gboolean
cb_read_stdout (GIOChannel *channel, GIOCondition cond, GnmLPSolve *lp)
{
	const char obj_line_prefix[] = "Value of objective function:";
	size_t obj_line_len = sizeof (obj_line_prefix) - 1;
	const char val_header_line[] = "Actual values of the variables:";
	size_t val_header_len = sizeof (val_header_line) - 1;

	do {
		GIOStatus status;
		gchar *line = NULL;
		gsize tpos;

		status = g_io_channel_read_line (channel,
						 &line, NULL, &tpos,
						 NULL);
		if (status != G_IO_STATUS_NORMAL)
			break;

		line[tpos] = 0;

		if (line[0] == 0 || g_ascii_isspace (line[0]))
			lp->section = SEC_UNKNOWN;
		else if (lp->section == SEC_UNKNOWN &&
			 !strncmp (line, obj_line_prefix, obj_line_len)) {
			GnmSolverResult *r;
			gnm_lpsolve_flush_solution (lp);
			r = gnm_lpsolve_start_solution (lp);
			r->quality = GNM_SOLVER_RESULT_FEASIBLE;
			r->value = g_ascii_strtod (line + obj_line_len, NULL);
		} else if (lp->section == SEC_UNKNOWN &&
			   !strncmp (line, val_header_line, val_header_len)) {
			lp->section = SEC_VALUES;
		} else if (lp->section == SEC_VALUES && lp->result) {
			GnmSolverResult *r = lp->result;
			int x, y;
			double v;
			char *space = strchr (line, ' ');
			GnmCell *cell;

			if (!space) {
				lp->section = SEC_UNKNOWN;
				continue;
			}
			*space = 0;
			cell = gnm_sub_solver_find_cell (lp->parent, line);
			if (!cell) {
				g_printerr ("Strange cell %s in output\n",
					    line);
				lp->section = SEC_UNKNOWN;
				continue;
			}

			v = g_ascii_strtod (space + 1, NULL);
			x = cell->pos.col - lp->srinput.range.start.col;
			y = cell->pos.row - lp->srinput.range.start.row;
			if (x >= 0 &&
			    x < value_area_get_width (r->solution, NULL) &&
			    y >= 0 &&
			    y < value_area_get_height (r->solution, NULL))
				value_array_set (r->solution, x, y,
						 value_new_float (v));
		}
		g_free (line);
	} while (1);

	return TRUE;
}


static void
gnm_lpsolve_child_exit (GnmSubSolver *subsol, gboolean normal, int code,
			GnmLPSolve *lp)
{
	GnmSolver *sol = GNM_SOLVER (subsol);
	GnmSolverStatus new_status = GNM_SOLVER_STATUS_DONE;

	if (sol->status != GNM_SOLVER_STATUS_RUNNING)
		return;

	if (normal) {
		GnmSolverResult *r;

		switch (code) {
		case 0: /* Optimal */
			gnm_sub_solver_flush (subsol);
			if (lp->result)
				lp->result->quality = GNM_SOLVER_RESULT_OPTIMAL;
			gnm_lpsolve_flush_solution (lp);
			break;

		case 2: /* Infeasible */
			r = gnm_lpsolve_start_solution (lp);
			r->quality = GNM_SOLVER_RESULT_INFEASIBLE;
			gnm_lpsolve_flush_solution (lp);
			break;

		case 3: /* Unbounded */
			r = gnm_lpsolve_start_solution (lp);
			r->quality = GNM_SOLVER_RESULT_UNBOUNDED;
			gnm_lpsolve_flush_solution (lp);
			break;

		case 1: /* Suboptimal */
		case 4: /* Degenerate */
			gnm_sub_solver_flush (subsol);
			gnm_lpsolve_flush_solution (lp);
			break;

		default:
		case 5: /* Numfailure */
		case 6: /* Userabort */
		case 7: /* Timeout */
		case 8: /* Running (eh?) */
		case 9: /* Presolved (eh?) */
			new_status = GNM_SOLVER_STATUS_ERROR;
			break;
		}
	} else {
		/* Something bad.  */
		new_status = GNM_SOLVER_STATUS_ERROR;
	}

	gnm_solver_set_status (sol, new_status);
}

static void
cb_child_setup (gpointer user)
{
	const char *lcvars[] = {
		"LC_ALL",
		"LC_MESSAGES",
		"LC_CTYPE",
		"LC_NUMERIC"
	};
	unsigned ui;

	g_unsetenv ("LANG");
	for (ui = 0; ui < G_N_ELEMENTS (lcvars); ui++) {
		const char *v = lcvars[ui];
		if (g_getenv (v))
			g_setenv (v, "C", TRUE);
	}
}

static gboolean
gnm_lpsolve_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		     GnmLPSolve *lp)
{
	gboolean ok;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARING);
	ok = write_program (sol, wbc, err);
	if (ok)
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARED);
	else {
		gnm_lpsolve_cleanup (lp);
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	}

	return ok;
}

static gboolean
gnm_lpsolve_start (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		   GnmLPSolve *lp)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (sol);
	gboolean ok;
	gchar *argv[5];
	int argc = 0;
	GnmSolverParameters *param = sol->params;
	const char *binary;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	binary = gnm_conf_get_plugin_lpsolve_lpsolve_path ();
	if (binary == NULL || *binary == 0)
		binary = SOLVER_PROGRAM;

	argv[argc++] = (gchar *)binary;
	argv[argc++] = (gchar *)"-i";
	argv[argc++] = (gchar *)(param->options.automatic_scaling
				 ? "-s1"
				 : "-s0");
	argv[argc++] = subsol->program_filename;
	argv[argc] = NULL;
	g_assert (argc < (int)G_N_ELEMENTS (argv));

	ok = gnm_sub_solver_spawn (subsol, argv,
				   cb_child_setup, NULL,
				   (GIOFunc)cb_read_stdout, lp,
				   NULL, NULL,
				   err);

	if (!ok && err &&
	    g_error_matches (*err, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT)) {
		g_clear_error (err);
		g_set_error (err, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT,
			     _("The %s program was not found.  You can either "
			       "install it or use another solver. "
			       "For more information see %s"),
			     SOLVER_PROGRAM,
			     SOLVER_URL);
	}

	return ok;
}

static gboolean
gnm_lpsolve_stop (GnmSolver *sol, GError *err, GnmLPSolve *lp)
{
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_RUNNING, FALSE);

	gnm_lpsolve_cleanup (lp);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_CANCELLED);

	return TRUE;
}

gboolean
lpsolve_solver_factory_functional (GnmSolverFactory *factory,
				   WBCGtk *wbcg);

gboolean
lpsolve_solver_factory_functional (GnmSolverFactory *factory,
				   WBCGtk *wbcg)
{
	const char *full_path = gnm_conf_get_plugin_lpsolve_lpsolve_path ();
	char *path;

	if (full_path && *full_path)
		return g_file_test (full_path, G_FILE_TEST_IS_EXECUTABLE);

	path = g_find_program_in_path (SOLVER_PROGRAM);
	if (path) {
		g_free (path);
		return TRUE;
	}

	if (!wbcg)
		return FALSE;

	path = gnm_sub_solver_locate_binary (SOLVER_PROGRAM,
					     "LP Solve",
					     SOLVER_URL,
					     wbcg);
	if (path) {
		gnm_conf_set_plugin_lpsolve_lpsolve_path (path);
		g_free (path);
		return TRUE;
	}

	return FALSE;
}


GnmSolver *
lpsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params);

GnmSolver *
lpsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	GnmSolver *res = g_object_new (GNM_SUB_SOLVER_TYPE,
				       "params", params,
				       NULL);
	GnmLPSolve *lp = g_new0 (GnmLPSolve, 1);

	lp->parent = GNM_SUB_SOLVER (res);
	gnm_sheet_range_from_value (&lp->srinput,
				    gnm_solver_param_get_input (params));
	if (!lp->srinput.sheet) lp->srinput.sheet = params->sheet;

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_lpsolve_prepare), lp);
	g_signal_connect (res, "start", G_CALLBACK (gnm_lpsolve_start), lp);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_lpsolve_stop), lp);
	g_signal_connect (res, "child-exit", G_CALLBACK (gnm_lpsolve_child_exit), lp);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, lp,
				(GDestroyNotify)gnm_lpsolve_final);

	return res;
}
