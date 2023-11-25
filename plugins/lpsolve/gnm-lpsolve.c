#include <gnumeric-config.h>
#include <gnumeric.h>
#include "boot.h"
#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <gnumeric-conf.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define SOLVER_PROGRAM "lp_solve"
#define SOLVER_URL "http://sourceforge.net/projects/lpsolve/"
#define PRIVATE_KEY "::lpsolve::"
#define SOLVER_INF GNM_const(1e30)

typedef struct {
	GnmSubSolver *parent;
	GnmSolverResult *result;
	GnmSolverSensitivity *sensitivity;
	enum { SEC_UNKNOWN, SEC_VALUES,
	       SEC_LIMITS, SEC_DUAL_LIMITS } section;
} GnmLPSolve;

static void
gnm_lpsolve_cleanup (GnmLPSolve *lp)
{
	gnm_sub_solver_clear (lp->parent);

	if (lp->result) {
		g_object_unref (lp->result);
		lp->result = NULL;
	}

	if (lp->sensitivity) {
		g_object_unref (lp->sensitivity);
		lp->sensitivity = NULL;
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
	int n;
	GnmSolver *sol;

	g_return_val_if_fail (lp->result == NULL, NULL);

	sol = GNM_SOLVER (lp->parent);
	n = sol->input_cells->len;

	lp->result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);
	lp->result->solution = g_new0 (gnm_float, n);

	lp->sensitivity = gnm_solver_sensitivity_new (sol);

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
	g_clear_object (&lp->sensitivity);
}


static char **
my_strsplit (const char *line)
{
	GPtrArray *res = g_ptr_array_new ();

	while (1) {
		const char *end;

		while (g_ascii_isspace (*line))
			line++;

		if (!*line)
			break;

		end = line;
		while (*end && !g_ascii_isspace (*end))
			end++;

		g_ptr_array_add (res, g_strndup (line, end - line));
		line = end;
	}
	g_ptr_array_add (res, NULL);

	return (char **)g_ptr_array_free (res, FALSE);
}

static gnm_float
fixup_inf (gnm_float v)
{
	if (v <= -SOLVER_INF)
		return go_ninf;
	if (v >= +SOLVER_INF)
		return go_pinf;
	return v;
}


static gboolean
cb_read_stdout (GIOChannel *channel, GIOCondition cond, GnmLPSolve *lp)
{
	GnmSolver *sol = GNM_SOLVER (lp->parent);
	const char obj_line_prefix[] = "Value of objective function:";
	size_t obj_line_len = sizeof (obj_line_prefix) - 1;
	const char val_header_line[] = "Actual values of the variables:";
	size_t val_header_len = sizeof (val_header_line) - 1;
	const char limit_header_line[] = "Objective function limits:";
	size_t limit_header_len = sizeof (limit_header_line) - 1;
	const char dual_limit_header_line[] = "Dual values with from - till limits:";
	size_t dual_limit_header_len = sizeof (dual_limit_header_line) - 1;
	gchar *line = NULL;

	do {
		GIOStatus status;
		gsize tpos;

		g_free (line);
		line = NULL;

		status = g_io_channel_read_line (channel,
						 &line, NULL, &tpos,
						 NULL);
		if (status != G_IO_STATUS_NORMAL)
			break;

		line[tpos] = 0;

		if (line[0] == 0)
			lp->section = SEC_UNKNOWN;
		else if (lp->section == SEC_UNKNOWN &&
			 !strncmp (line, obj_line_prefix, obj_line_len)) {
			GnmSolverResult *r;
			gnm_lpsolve_flush_solution (lp);
			r = gnm_lpsolve_start_solution (lp);
			r->quality = GNM_SOLVER_RESULT_FEASIBLE;
			r->value = gnm_ascii_strto (line + obj_line_len, NULL);
		} else if (lp->section == SEC_UNKNOWN &&
			   !strncmp (line, val_header_line, val_header_len)) {
			lp->section = SEC_VALUES;
		} else if (lp->section == SEC_UNKNOWN &&
			   !strncmp (line, limit_header_line, limit_header_len)) {
			lp->section = SEC_LIMITS;
		} else if (lp->section == SEC_UNKNOWN &&
			   !strncmp (line, dual_limit_header_line, dual_limit_header_len)) {
			lp->section = SEC_DUAL_LIMITS;
		} else if (lp->section == SEC_VALUES && lp->result) {
			GnmSolverResult *r = lp->result;
			gnm_float v;
			char *space = strchr (line, ' ');
			GnmCell *cell;
			int idx;

			if (!space) {
				lp->section = SEC_UNKNOWN;
				continue;
			}
			*space = 0;
			cell = gnm_sub_solver_find_cell (lp->parent, line);
			idx = gnm_solver_cell_index (sol, cell);
			if (idx < 0) {
				g_printerr ("Strange cell %s in output\n",
					    line);
				lp->section = SEC_UNKNOWN;
				continue;
			}

			v = gnm_ascii_strto (space + 1, NULL);
			r->solution[idx] = v;
		} else if (lp->section == SEC_LIMITS) {
			gnm_float low, high;
			GnmCell *cell;
			int idx;
			gchar **items;

			if (g_ascii_isspace (line[0]))
				continue;

			items = my_strsplit (line);

			if (g_strv_length (items) != 4)
				goto bad_limit;

			cell = gnm_sub_solver_find_cell (lp->parent, items[0]);
			idx = gnm_solver_cell_index (sol, cell);
			if (idx < 0)
				goto bad_limit;

			low = fixup_inf (gnm_ascii_strto (items[1], NULL));
			high = fixup_inf (gnm_ascii_strto (items[2], NULL));

			lp->sensitivity->vars[idx].low = low;
			lp->sensitivity->vars[idx].high = high;

			g_strfreev (items);

			continue;

		bad_limit:
			g_printerr ("Strange limit line in output: %s\n",
				    line);
			lp->section = SEC_UNKNOWN;
			g_strfreev (items);
		} else if (lp->section == SEC_DUAL_LIMITS) {
			gnm_float dual, low, high;
			GnmCell *cell;
			int idx, cidx;
			gchar **items;

			if (g_ascii_isspace (line[0]))
				continue;

			items = my_strsplit (line);

			if (g_strv_length (items) != 4)
				goto bad_dual;

			cell = gnm_sub_solver_find_cell (lp->parent, items[0]);
			idx = gnm_solver_cell_index (sol, cell);

			cidx = (idx == -1)
				? gnm_sub_solver_find_constraint (lp->parent, items[0])
				: -1;

			dual = fixup_inf (gnm_ascii_strto (items[1], NULL));
			low = fixup_inf (gnm_ascii_strto (items[2], NULL));
			high = fixup_inf (gnm_ascii_strto (items[3], NULL));

			if (idx >= 0) {
				lp->sensitivity->vars[idx].reduced_cost = dual;
			} else if (cidx >= 0) {
				lp->sensitivity->constraints[cidx].low = low;
				lp->sensitivity->constraints[cidx].high = high;
				lp->sensitivity->constraints[cidx].shadow_price = dual;
			} else {
				// Ignore
			}

			g_strfreev (items);
			continue;

		bad_dual:
			g_printerr ("Strange dual limit line in output: %s\n",
				    line);
			lp->section = SEC_UNKNOWN;
			g_strfreev (items);
		}
	} while (1);

	g_free (line);

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
			g_object_set (lp->parent,
				      "sensitivity", lp->sensitivity,
				      NULL);
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
	gchar *argv[6];
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
	argv[argc++] = (gchar *)"-S6";
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

GnmSolver *
lpsolve_solver_create (GnmSolverParameters *params)
{
	GnmSolver *res = g_object_new (GNM_SUB_SOLVER_TYPE,
				       "params", params,
				       NULL);
	GnmLPSolve *lp = g_new0 (GnmLPSolve, 1);

	lp->parent = GNM_SUB_SOLVER (res);

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_lpsolve_prepare), lp);
	g_signal_connect (res, "start", G_CALLBACK (gnm_lpsolve_start), lp);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_lpsolve_stop), lp);
	g_signal_connect (res, "child-exit", G_CALLBACK (gnm_lpsolve_child_exit), lp);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, lp,
				(GDestroyNotify)gnm_lpsolve_final);

	return res;
}


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
lpsolve_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	return lpsolve_solver_create (params);
}
