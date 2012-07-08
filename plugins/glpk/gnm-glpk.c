#include <gnumeric-config.h>
#include "gnumeric.h"
#include <tools/gnm-solver.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "gutils.h"
#include "gnumeric-conf.h"
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-input-stdio.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#define SOLVER_PROGRAM "glpsol"
#define SOLVER_URL "http://www.gnu.org/software/glpk/"
#define PRIVATE_KEY "::glpk::"

typedef struct {
	GnmSubSolver *parent;
	char *result_filename;
	GnmSheetRange srinput;
} GnmGlpk;

static void
gnm_glpk_cleanup (GnmGlpk *lp)
{
	gnm_sub_solver_clear (lp->parent);
	if (lp->result_filename) {
		g_unlink (lp->result_filename);
		g_free (lp->result_filename);
		lp->result_filename = NULL;
	}
}

static void
gnm_glpk_final (GnmGlpk *lp)
{
	gnm_glpk_cleanup (lp);
	g_free (lp);
}

static gboolean
write_program (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (sol);
	GOFileSaver *fs;

	fs = go_file_saver_for_mime_type ("application/glpk");
	if (!fs) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("The GLPK exporter is not available."));
		return FALSE;
	}

	return gnm_solver_saveas (sol, wbc, fs,
				  "program-XXXXXX.cplex",
				  &subsol->program_filename,
				  err);
}

static void
gnm_glpk_read_solution (GnmGlpk *lp)
{
	GnmSubSolver *subsol = lp->parent;
	GnmSolver *sol = GNM_SOLVER (subsol);
	GsfInput *input;
	GsfInputTextline *tl;
	const char *line;
	unsigned rows, cols, c, r;
	int pstat, dstat;
	gnm_float val;
	GnmSolverResult *result;
	int width, height;
	gboolean has_integer;
	GSList *l;

	/*
	 * glpsol's output format is different if there are any integer
	 * constraint.  Go figure.
	 */
	has_integer = sol->params->options.assume_discrete;
	for (l = sol->params->constraints; !has_integer && l; l = l->next) {
		GnmSolverConstraint *c = l->data;
		has_integer = (c->type == GNM_SOLVER_INTEGER ||
			       c->type == GNM_SOLVER_BOOLEAN);
	}

	input = gsf_input_stdio_new (lp->result_filename, NULL);
	if (!input)
		return;

	tl = GSF_INPUT_TEXTLINE (gsf_input_textline_new (input));
	g_object_unref (input);

	width = range_width (&lp->srinput.range);
	height = range_height (&lp->srinput.range);
	result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);
	result->solution = value_new_array_empty (width, height);

	if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
		goto fail;
	if (sscanf (line, "%u %u", &rows, &cols) != 2 ||
	    cols != g_hash_table_size (subsol->cell_from_name))
		goto fail;

	if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
		goto fail;

	if (has_integer
	    ? sscanf (line, "%d %" GNM_SCANF_g, &pstat, &val) != 2
	    : sscanf (line, "%d %d %" GNM_SCANF_g, &pstat, &dstat, &val) != 3)
		goto fail;

	result->value = val;
	switch (pstat) {
	case 2:
	case 5:
		result->quality = GNM_SOLVER_RESULT_OPTIMAL;
		break;
	case 1: /* "Undefined" -- see #611407 */
	case 3:
	case 4:
		result->quality = GNM_SOLVER_RESULT_INFEASIBLE;
		break;
	case 6:
		result->quality = GNM_SOLVER_RESULT_UNBOUNDED;
		break;
	default:
		goto fail;
	}

	for (r = 1; r <= rows; r++) {
		if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
			goto fail;
		/* Ignore the line */
	}

	for (c = 0; c < cols; c++) {
		gnm_float pval, dval;
		unsigned cstat;
		int x, y;

		if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
			goto fail;

		if (has_integer
		    ? sscanf (line, "%" GNM_SCANF_g, &pval) != 1
		    : sscanf (line, "%u %" GNM_SCANF_g " %" GNM_SCANF_g,
			      &cstat, &pval, &dval) != 3)
			goto fail;

		x = c % width;
		y = c / width;
		value_array_set (result->solution, x, y,
				 value_new_float (pval));
	}

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
	g_object_set (subsol, "result", result, NULL);
	g_object_unref (result);

	g_object_unref (tl);
	return;

fail:
	g_object_unref (tl);
	g_object_unref (result);
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
}


static void
gnm_glpk_child_exit (GnmSubSolver *subsol, gboolean normal, int code,
		     GnmGlpk *lp)
{
	GnmSolver *sol = GNM_SOLVER (subsol);

	if (sol->status != GNM_SOLVER_STATUS_RUNNING)
		return;

	if (normal) {
		switch (code) {
		case 0: {
			GnmLocale *locale = gnm_push_C_locale ();
			gnm_glpk_read_solution (lp);
			gnm_pop_C_locale (locale);
			break;
		}
		default:
			break;
		}
	}

	if (sol->status == GNM_SOLVER_STATUS_RUNNING)
		gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
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
gnm_glpk_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		  GnmGlpk *lp)
{
	gboolean ok;
	int fd;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARING);
	ok = write_program (sol, wbc, err);
	if (!ok)
		goto fail;

	fd = g_file_open_tmp ("program-XXXXXX.out", &lp->result_filename, err);
	if (fd == -1) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to create file for solution"));
		goto fail;
	}

	close (fd);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_PREPARED);

	return TRUE;

fail:
	gnm_glpk_cleanup (lp);
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	return FALSE;
}

static gboolean
gnm_glpk_start (GnmSolver *sol, WorkbookControl *wbc, GError **err,
		GnmGlpk *lp)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (sol);
	gboolean ok;
	gchar *argv[7];
	int argc = 0;
	GnmSolverParameters *param = sol->params;
	const char *binary;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	binary = gnm_conf_get_plugin_glpk_glpsol_path ();
	if (binary == NULL || *binary == 0)
		binary = SOLVER_PROGRAM;

	argv[argc++] = (gchar *)binary;
	argv[argc++] = (gchar *)(param->options.automatic_scaling
				 ? "--scale"
				 : "--noscale");
	argv[argc++] = (gchar *)"--write";
	argv[argc++] = lp->result_filename;
	argv[argc++] = (gchar *)"--cpxlp";
	argv[argc++] = subsol->program_filename;
	argv[argc] = NULL;
	g_assert (argc < (int)G_N_ELEMENTS (argv));

	ok = gnm_sub_solver_spawn (subsol, argv,
				   cb_child_setup, NULL,
				   NULL, NULL,
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
gnm_glpk_stop (GnmSolver *sol, GError *err, GnmGlpk *lp)
{
	g_return_val_if_fail (sol->status != GNM_SOLVER_STATUS_RUNNING, FALSE);

	gnm_glpk_cleanup (lp);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_CANCELLED);

	return TRUE;
}

gboolean
glpk_solver_factory_functional (GnmSolverFactory *factory,
				WBCGtk *wbcg);

gboolean
glpk_solver_factory_functional (GnmSolverFactory *factory,
				WBCGtk *wbcg)
{
	const char *full_path = gnm_conf_get_plugin_glpk_glpsol_path ();
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
					     "Gnu Linear Programming Kit",
					     SOLVER_URL,
					     wbcg);
	if (path) {
		gnm_conf_set_plugin_glpk_glpsol_path (path);
		g_free (path);
		return TRUE;
	}

	return FALSE;
}


GnmSolver *
glpk_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params);

GnmSolver *
glpk_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	GnmSolver *res = g_object_new (GNM_SUB_SOLVER_TYPE,
				       "params", params,
				       NULL);
	GnmGlpk *lp = g_new0 (GnmGlpk, 1);

	lp->parent = GNM_SUB_SOLVER (res);
	gnm_sheet_range_from_value (&lp->srinput,
				    gnm_solver_param_get_input (params));
	if (!lp->srinput.sheet) lp->srinput.sheet = params->sheet;

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_glpk_prepare), lp);
	g_signal_connect (res, "start", G_CALLBACK (gnm_glpk_start), lp);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_glpk_stop), lp);
	g_signal_connect (res, "child-exit", G_CALLBACK (gnm_glpk_child_exit), lp);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, lp,
				(GDestroyNotify)gnm_glpk_final);

	return res;
}
