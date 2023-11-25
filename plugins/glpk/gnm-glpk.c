#include <gnumeric-config.h>
#include <gnumeric.h>
#include "boot.h"
#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <gutils.h>
#include <gnumeric-conf.h>
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
	char *ranges_filename;
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
	if (lp->ranges_filename) {
		g_unlink (lp->ranges_filename);
		g_free (lp->ranges_filename);
		lp->ranges_filename = NULL;
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
parse_number (const char *s)
{
	if (strcmp (s, ".") == 0)
		return 0;
	return g_ascii_strtod (s, NULL);
}

typedef enum { GLPK_457, GLPK_458, GLPK_UNKNOWN } GlpkFileVersion;

static GlpkFileVersion
gnm_glpk_detect_version (GnmGlpk *lp,
			 GsfInputTextline *tl)
{
	GnmSubSolver *subsol = lp->parent;
	gsf_off_t cur = gsf_input_tell (GSF_INPUT (tl));
	GlpkFileVersion ver = GLPK_UNKNOWN;
	const char *line;
	unsigned cols, rows;

	if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
		goto out;
	if (gnm_sscanf (line, "%u %u", &rows, &cols) == 2 &&
	    cols == g_hash_table_size (subsol->cell_from_name)) {
		ver = GLPK_457;
		if (gnm_solver_debug ())
			g_printerr ("Detected version 4.57 file format\n");
		goto out;
	}

	if ((line[0] == 'c' || line[0] == 's') && line[1] == ' ') {
		ver = GLPK_458;
		if (gnm_solver_debug ())
			g_printerr ("Detected version 4.58 file format\n");
		goto out;
	}

out:
	// Extra seek due to gsf bug
	gsf_input_seek (GSF_INPUT (tl), cur + 1, G_SEEK_SET);
	gsf_input_seek (GSF_INPUT (tl), cur, G_SEEK_SET);
	return ver;
}

static gboolean
gnm_glpk_read_solution_457 (GnmGlpk *lp,
			    GsfInputTextline *tl,
			    GnmSolverResult *result,
			    GnmSolverSensitivity *sensitivity,
			    gboolean has_integer)
{
	GnmSubSolver *subsol = lp->parent;
	const char *line;
	unsigned cols, rows, c, r;
	int pstat, dstat;
	gnm_float val;

	if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
		goto fail;
	if (gnm_sscanf (line, "%u %u", &rows, &cols) != 2 ||
	    cols != g_hash_table_size (subsol->cell_from_name))
		goto fail;

	if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
		goto fail;

	if (has_integer
	    ? gnm_sscanf (line, "%d %" GNM_SCANF_g, &pstat, &val) != 2
	    : gnm_sscanf (line, "%d %d %" GNM_SCANF_g, &pstat, &dstat, &val) != 3)
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

	for (r = 0; r < rows; r++) {
		gnm_float pval, dval;
		unsigned rstat;
		unsigned cidx = r;

		if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
			goto fail;

		if (has_integer)
			continue;

		if (gnm_sscanf (line, "%u %" GNM_SCANF_g " %" GNM_SCANF_g,
			    &rstat, &pval, &dval) != 3)
			goto fail;

		sensitivity->constraints[cidx].shadow_price = dval;
	}

	for (c = 0; c < cols; c++) {
		gnm_float pval, dval;
		unsigned cstat;
		unsigned idx = c;

		if ((line = gsf_input_textline_utf8_gets (tl)) == NULL)
			goto fail;

		if (has_integer
		    ? gnm_sscanf (line, "%" GNM_SCANF_g, &pval) != 1
		    : gnm_sscanf (line, "%u %" GNM_SCANF_g " %" GNM_SCANF_g,
			      &cstat, &pval, &dval) != 3)
			goto fail;

		result->solution[idx] = pval;
		if (!has_integer)
			sensitivity->vars[idx].reduced_cost = dval;
	}

	// Success
	return FALSE;

fail:
	return TRUE;
}

#define READ_LINE(tl,line) do {					\
	line = gsf_input_textline_utf8_gets (tl);		\
        if (!line) goto fail;					\
	if (gnm_solver_debug ())				\
		g_printerr ("%s\n", line);			\
} while (line[0] == 'c' && (line[1] == 0 || line[1] == ' '))

static gboolean
gnm_glpk_read_solution_458 (GnmGlpk *lp,
			    GsfInputTextline *tl,
			    GnmSolverResult *result,
			    GnmSolverSensitivity *sensitivity,
			    gboolean has_integer)
{
	GnmSubSolver *subsol = lp->parent;
	const char *line;
	unsigned cols, rows, c, r;
	gnm_float val;
	char pstat, dstat;

	READ_LINE (tl, line);

	if (has_integer) {
		if (gnm_sscanf (line, "s %*s %u %u %c %" GNM_SCANF_g,
				&rows, &cols, &pstat, &val) != 4)
			goto fail;
	} else {
		if (gnm_sscanf (line, "s %*s %u %u %c %c %" GNM_SCANF_g,
				&rows, &cols, &pstat, &dstat, &val) != 5)
			goto fail;
	}
	if (cols != g_hash_table_size (subsol->cell_from_name))
		goto fail;

	result->value = val;
	switch (pstat) {
	case 'o':
		result->quality = GNM_SOLVER_RESULT_OPTIMAL;
		break;
	case 'f':
		result->quality = GNM_SOLVER_RESULT_FEASIBLE;
		break;
	case 'u':
	case 'i':
	case 'n':
		result->quality = GNM_SOLVER_RESULT_INFEASIBLE;
		break;
	default:
		goto fail;
	}

	for (r = 0; r < rows; r++) {
		gnm_float pval, dval;
		char rstat;
		unsigned r1, cidx = r;

		READ_LINE (tl, line);

		if ((has_integer
		     ? gnm_sscanf (line, "i %d %" GNM_SCANF_g,
			       &r1, &dval) != 2
		     : gnm_sscanf (line, "i %d %c %" GNM_SCANF_g " %" GNM_SCANF_g,
			       &r1, &rstat, &pval, &dval) != 4) ||
		    r1 != cidx + 1)
			goto fail;
		// rstat?

		sensitivity->constraints[cidx].shadow_price = dval;
	}

	for (c = 0; c < cols; c++) {
		gnm_float pval, dval;
		char cstat;
		unsigned c1, cidx = c;

		READ_LINE (tl, line);

		if ((has_integer
		     ? gnm_sscanf (line, "j %d %" GNM_SCANF_g,
				   &c1, &pval) != 2
		     : gnm_sscanf (line, "j %d %c %" GNM_SCANF_g " %" GNM_SCANF_g,
				   &c1, &cstat, &pval, &dval) != 4) ||
		    c1 != cidx + 1)
			goto fail;
		// cstat?

		result->solution[cidx] = pval;
	}

	// Success
	return FALSE;

fail:
	return TRUE;
}

static void
gnm_glpk_read_solution (GnmGlpk *lp)
{
	GnmSubSolver *subsol = lp->parent;
	GnmSolver *sol = GNM_SOLVER (subsol);
	GsfInput *input;
	GsfInputTextline *tl = NULL;
	const char *line;
	GnmSolverResult *result = NULL;
	GnmSolverSensitivity *sensitivity = NULL;
	enum { SEC_UNKNOWN, SEC_ROWS, SEC_COLUMNS } state;
	gboolean has_integer;
	GSList *l;

	input = gsf_input_stdio_new (lp->result_filename, NULL);
	if (!input)
		goto fail;
	tl = GSF_INPUT_TEXTLINE (gsf_input_textline_new (input));
	g_object_unref (input);

	result = g_object_new (GNM_SOLVER_RESULT_TYPE, NULL);
	result->solution = g_new0 (gnm_float, sol->input_cells->len);

	sensitivity = gnm_solver_sensitivity_new (sol);

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

	switch (gnm_glpk_detect_version (lp, tl)) {
	case GLPK_457:
		if (gnm_glpk_read_solution_457 (lp, tl, result, sensitivity,
						has_integer))
			goto fail;
		break;
	case GLPK_458:
		if (gnm_glpk_read_solution_458 (lp, tl, result, sensitivity,
						has_integer))
			goto fail;
		break;
	default:
		goto fail;
	}

	g_object_unref (tl);
	tl = NULL;

	// ----------------------------------------

	if (!lp->ranges_filename)
		goto done;

	input = gsf_input_stdio_new (lp->ranges_filename, NULL);
	if (!input)
		goto fail;
	tl = GSF_INPUT_TEXTLINE (gsf_input_textline_new (input));
	g_object_unref (input);

	state = SEC_UNKNOWN;
	// We are reading a file intended for human consumption.
	// That is unfortunately because it implies rounding, for example.
	// The information does not appear to be available elsewhere.

	while ((line = gsf_input_textline_utf8_gets (tl)) != NULL) {
		gchar **items, **items2 = NULL;
		int len, len2 = 0;

		if (g_str_has_prefix (line, "   No. Row name")) {
			state = SEC_ROWS;
			continue;
		} else if (g_str_has_prefix (line, "   No. Column name")) {
			state = SEC_COLUMNS;
			continue;
		} else if (g_ascii_isalpha (line[0])) {
			state = SEC_UNKNOWN;
			continue;
		}

		if (state == SEC_UNKNOWN)
			continue;

		items = my_strsplit (line);
		len = g_strv_length (items);

		if (len == 10 && g_ascii_isdigit (items[0][0])) {
			line = gsf_input_textline_utf8_gets (tl);
			if (line) {
				items2 = my_strsplit (line);
				len2 = g_strv_length (items2);
			}
		}

		if (len == 10 && len2 == 6 && state == SEC_COLUMNS) {
			gnm_float low = parse_number (items[7]);
			gnm_float high = parse_number (items2[3]);
			GnmCell const *cell = gnm_sub_solver_find_cell (lp->parent, items[1]);
			int idx = gnm_solver_cell_index (sol, cell);
			if (idx >= 0) {
				sensitivity->vars[idx].low = low;
				sensitivity->vars[idx].high = high;
			}
		}

		if (len == 10 && len2 == 6 && state == SEC_ROWS) {
			gnm_float low = parse_number (items[6]);
			gnm_float high = parse_number (items2[2]);
			int cidx = gnm_sub_solver_find_constraint (lp->parent, items[1]);
			if (cidx >= 0) {
				sensitivity->constraints[cidx].low = low;
				sensitivity->constraints[cidx].high = high;
			}
		}

		g_strfreev (items);
		g_strfreev (items2);

	}

	g_object_unref (tl);

	// ----------------------------------------
done:
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_DONE);
	g_object_set (subsol, "result", result, NULL);
	g_object_unref (result);
	g_object_set (subsol, "sensitivity", sensitivity, NULL);
	g_object_unref (sensitivity);

	return;

fail:
	if (tl)
		g_object_unref (tl);
	if (result)
		g_object_unref (result);
	if (sensitivity)
		g_object_unref (sensitivity);
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

	if (sol->params->options.sensitivity_report) {
		fd = g_file_open_tmp ("program-XXXXXX.ran", &lp->ranges_filename, err);
		if (fd == -1) {
			g_set_error (err, G_FILE_ERROR, 0,
				     _("Failed to create file for sensitivity report"));
			goto fail;
		}
		close (fd);
	}

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
	gchar *argv[9];
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
	if (lp->ranges_filename) {
		argv[argc++] = (gchar *)"--ranges";
		argv[argc++] = lp->ranges_filename;
	}
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
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_RUNNING, FALSE);

	gnm_glpk_cleanup (lp);

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_CANCELLED);

	return TRUE;
}

GnmSolver *
glpk_solver_create (GnmSolverParameters *params)
{
	GnmSolver *res = g_object_new (GNM_SUB_SOLVER_TYPE,
				       "params", params,
				       NULL);
	GnmGlpk *lp = g_new0 (GnmGlpk, 1);

	lp->parent = GNM_SUB_SOLVER (res);

	g_signal_connect (res, "prepare", G_CALLBACK (gnm_glpk_prepare), lp);
	g_signal_connect (res, "start", G_CALLBACK (gnm_glpk_start), lp);
	g_signal_connect (res, "stop", G_CALLBACK (gnm_glpk_stop), lp);
	g_signal_connect (res, "child-exit", G_CALLBACK (gnm_glpk_child_exit), lp);

	g_object_set_data_full (G_OBJECT (res), PRIVATE_KEY, lp,
				(GDestroyNotify)gnm_glpk_final);

	return res;
}

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
glpk_solver_factory (GnmSolverFactory *factory, GnmSolverParameters *params)
{
	return glpk_solver_create (params);
}
