/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ssconvert.c: A wrapper application to convert spreadsheet formats
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *   Morten Welinder <terra@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2002-2003 Jody Goldberg
 * Copyright (C) 2006-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "position.h"
#include "parse-util.h"
#include "workbook.h"
#include "workbook-priv.h"
#include "workbook-control.h"
#include "sheet.h"
#include "dependent.h"
#include "expr-name.h"
#include "libgnumeric.h"
#include "gutils.h"
#include "gnumeric-paths.h"
#include "gnm-plugin.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "workbook-view.h"
#include <dialogs/dialogs.h>
#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

static gboolean ssconvert_show_version = FALSE;
static gboolean ssconvert_list_exporters = FALSE;
static gboolean ssconvert_list_importers = FALSE;
static gboolean ssconvert_one_file_per_sheet = FALSE;
static gboolean ssconvert_recalc = FALSE;
static gboolean ssconvert_solve = FALSE;
static char *ssconvert_range = NULL;
static char *ssconvert_import_encoding = NULL;
static char *ssconvert_import_id = NULL;
static char *ssconvert_export_id = NULL;
static char *ssconvert_export_options = NULL;
static char *ssconvert_merge_target = NULL;
static char **ssconvert_goal_seek = NULL;

static const GOptionEntry ssconvert_options [] = {
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &ssconvert_show_version,
		N_("Display program version"),
		NULL
	},

	/* ---------------------------------------- */

	{
		"import-encoding", 'E',
		0, G_OPTION_ARG_STRING, &ssconvert_import_encoding,
		N_("Optionally specify an encoding for imported content"),
		N_("ENCODING")
	},

	{
		"import-type", 'I',
		0, G_OPTION_ARG_STRING, &ssconvert_import_id,
		N_("Optionally specify which importer to use"),
		N_("ID")
	},

	{
		"list-importers", 0,
		0, G_OPTION_ARG_NONE, &ssconvert_list_importers,
		N_("List the available importers"),
		NULL
	},

	/* ---------------------------------------- */

	{
		"merge-to", 'M',
		0, G_OPTION_ARG_STRING, &ssconvert_merge_target,
		N_("Merge listed files (all same format) to make this file"),
		N_("file")
	},

	{
		"export-type", 'T',
		0, G_OPTION_ARG_STRING, &ssconvert_export_id,
		N_("Optionally specify which exporter to use"),
		N_("ID")
	},

	{
		"export-options", 'O',
		0, G_OPTION_ARG_STRING, &ssconvert_export_options,
		N_("Detailed instructions for the chosen exporter"),
		N_("string")
	},

	{
		"list-exporters", 0,
		0, G_OPTION_ARG_NONE, &ssconvert_list_exporters,
		N_("List the available exporters"),
		NULL
	},

	{
		"export-file-per-sheet", 'S',
		0, G_OPTION_ARG_NONE, &ssconvert_one_file_per_sheet,
		N_("Export a file for each sheet if the exporter only supports one sheet at a time"),
		NULL
	},

	{
		"recalc", 0,
		0, G_OPTION_ARG_NONE, &ssconvert_recalc,
		N_("Recalculate all cells before writing the result"),
		NULL
	},


	/* ---------------------------------------- */

	/* For now these are for INTERNAL GNUMERIC USE ONLY.  */
	{
		"export-range", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &ssconvert_range,
		N_("The range to export"),
		NULL
	},

	{
		"goal-seek", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING_ARRAY, &ssconvert_goal_seek,
		N_("Goal seek areas"),
		NULL
	},

	{
		"solve", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &ssconvert_solve,
		N_("Run the solver"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

static void
setup_range (GObject *obj, const char *key, Workbook *wb, const char *rtxt)
{
	GnmParsePos pp;
	const char *end;
	GnmRangeRef rr;

	pp.wb = wb;
	pp.sheet = workbook_sheet_by_index (wb, 0);
	pp.eval.col = 0;
	pp.eval.row = 0;

	end = rangeref_parse (&rr, rtxt, &pp, gnm_conventions_default);
	if (!end || end == rtxt || *end != 0) {
		g_printerr ("Invalid range specified.\n");
		exit (1);
	}

	g_object_set_data_full (obj, key,
				g_memdup (&rr, sizeof (rr)),
				g_free);
}

static int
handle_export_options (GOFileSaver *fs, GODoc *doc)
{
	guint sig = g_signal_lookup ("set-export-options",
				     G_TYPE_FROM_INSTANCE (fs));

	if (!ssconvert_export_options)
		return 0;

	if (g_signal_handler_find (fs, G_SIGNAL_MATCH_ID,
				   sig, 0, NULL, NULL, NULL)) {
		GError *err = NULL;
		gboolean fail =
			go_file_saver_set_export_options
			(fs, doc,
			 ssconvert_export_options,
			 &err);

		if (fail) {
			g_printerr ("ssconvert: %s\n", err
				    ? err->message
				    : _("Cannot parse export options."));
			return 1;
		}

		return 0;
	} else {
		g_printerr (_("The file saver does not take options"));
		return 1;
	}
}


typedef GList *(*get_them_f)(void);
typedef gchar const *(*get_desc_f)(void *);

static void
list_them (get_them_f get_them,
	   get_desc_f get_his_id,
	   get_desc_f get_his_description)
{
	GList *ptr;
	int len = 0;

	for (ptr = (*get_them) (); ptr ; ptr = ptr->next) {
		char const *id = (*get_his_id) (ptr->data);
		int tmp = strlen (id);
		if (len < tmp)
			len = tmp;
	}

	g_printerr ("%-*s | %s\n", len,
		    /* Translate these? */
		    "ID",
		    "Description");
	for (ptr = (*get_them) (); ptr ; ptr = ptr->next) {
		char const *id = (*get_his_id) (ptr->data);
		g_printerr ("%-*s | %s\n", len,
			    id,
			    (*get_his_description) (ptr->data));
	}
}

/*
 * Read the files we're going to merge and return a list of Workbooks.
 */
static GSList *
read_files_to_merge (const char *inputs[], GOFileOpener *fo,
		     GOIOContext *io_context, GOCmdContext *cc)
{
	GSList *wbs = NULL;

	while (*inputs) {
		const char *fname = *inputs;
		char *uri = go_shell_arg_to_uri (fname);
		WorkbookView *wbv =
			wb_view_new_from_uri (uri, fo, io_context,
					      ssconvert_import_encoding);
		g_free (uri);
		inputs++;

		if (go_io_error_occurred (io_context)) {
			go_slist_free_custom (wbs, g_object_unref);
			return NULL;
		}

		if (!wbv)
			continue;

		wbs = g_slist_prepend (wbs, wb_view_get_workbook (wbv));
	}

	return g_slist_reverse (wbs);
}

/*
 * Look at a set of workbooks, and pick a sheet size that would
 * be good for sheets in a workbook merging them all.
 */
static void
suggest_size (GSList *wbs, int *csuggest, int *rsuggest)
{
	GSList *l;
	int rmax = 0;
	int cmax = 0;

	for (l = wbs; l; l = l->next) {
		Workbook *wb = l->data;

		WORKBOOK_FOREACH_SHEET (wb, sheet, {
			int r = gnm_sheet_get_max_rows (sheet);
			int c = gnm_sheet_get_max_cols (sheet);
			if (r > rmax) rmax = r;
			if (c > cmax) cmax = c;
		});
	}

	gnm_sheet_suggest_size (&cmax, &rmax);
	*csuggest = cmax;
	*rsuggest = rmax;
}

static void
cb_fixup_name_wb (const char *name, GnmNamedExpr *nexpr, Workbook *wb)
{
	GnmParsePos newpos = nexpr->pos;

	if (!expr_name_is_active (nexpr))
		return;

	if (nexpr->pos.wb) {
		newpos.wb = wb;
		expr_name_set_pos (nexpr, &newpos);
	}
}


/* Append the sheets of workbook wb2 to workbook wb.  Resize sheets
   if necessary.  Fix workbook links in sheet if necessary.
   Merge names in workbook scope (conflicts result in an error). */
static gboolean
merge_single (Workbook *wb, Workbook *wb2,
	      int cmax, int rmax,
	      GOCmdContext *cc)
{
	/* Move names with workbook scope in wb2 over to wb */
	GSList *names = g_slist_sort (gnm_named_expr_collection_list (wb2->names),
				      (GCompareFunc)expr_name_cmp_by_name);
	GSList *p;

	for (p = names; p; p = p->next) {
		GnmNamedExpr *nexpr = p->data;
		const char *name = expr_name_name (nexpr);
		GnmNamedExpr *nexpr2;
		GnmParsePos pp;
		GnmParsePos newpos = nexpr->pos;

		if (!expr_name_is_active (nexpr))
			continue;

		if (nexpr->pos.wb != wb2 || nexpr->pos.sheet != NULL)
			continue;

		/* Check for clash with existing name */

		parse_pos_init (&pp, wb, NULL, 0, 0);
		nexpr2 = expr_name_lookup (&pp, name);
		if (nexpr2 /* FIXME: && nexpr2-is-not-the-same-as-nexpr */) {
			g_printerr (_("Name conflict during merge: '%s' appears twice at workbook scope.\n"),
				    name);
			g_slist_free (names);
			return TRUE;
		}

		/* Move name scope to workbook wb */
		newpos.wb = wb;
		expr_name_set_pos (nexpr, &newpos);
	}
	g_slist_free (names);

	while (workbook_sheet_count (wb2) > 0) {
		/* Remove sheet from incoming workbook */
		Sheet *sheet = workbook_sheet_by_index (wb2, 0);
		int loc = workbook_sheet_count (wb);
		GOUndo *undo;
		char *sheet_name;
		gboolean err;

		g_object_ref (sheet);
		workbook_sheet_delete (sheet);
		sheet->workbook = wb;

		/* Fix names that reference the old workbook */
		gnm_sheet_foreach_name (sheet, (GHFunc)cb_fixup_name_wb, wb);

		undo = gnm_sheet_resize (sheet, cmax, rmax, cc, &err);
		if (undo)
			g_object_unref (undo);

		/* Pick a free sheet name */
		sheet_name = workbook_sheet_get_free_name
			(wb, sheet->name_unquoted, FALSE, TRUE);
		g_object_set (sheet, "name", sheet_name, NULL);
		g_free (sheet_name);

		/* Insert and revive the sheet */
		workbook_sheet_attach_at_pos (wb, sheet, loc);
		dependents_revive_sheet (sheet);
		g_object_unref (sheet);
	}

	return FALSE;
}

/* Merge a collection of workbooks into one. */
static gboolean
merge (Workbook *wb, char const *inputs[],
       GOFileOpener *fo, GOIOContext *io_context, GOCmdContext *cc)
{
	GSList *wbs, *l;
	int result = 0;
	int cmax, rmax;

	wbs = read_files_to_merge (inputs, fo, io_context, cc);
	if (go_io_error_occurred (io_context)) {
		go_io_error_display (io_context);
		return TRUE;
	}

	suggest_size (wbs, &cmax, &rmax);

	for (l = wbs; l; l = l->next) {
		Workbook *wb2 = l->data;
		const char *uri = go_doc_get_uri (GO_DOC (wb2));

		g_printerr ("Adding sheets from %s\n", uri);

		result = merge_single (wb, wb2, cmax, rmax, cc);
		if (result)
			break;
	}

	go_slist_free_custom (wbs, g_object_unref);
	return result;
}

static char *
resolve_template (const char *template, Sheet *sheet)
{
	GString *s = g_string_new (NULL);
	while (1) {
		switch (*template) {
		done:
		case 0: {
			char *res = go_shell_arg_to_uri (s->str);
			g_string_free (s, TRUE);
			return res;
		}
		case '%':
			template++;
			switch (*template) {
			case 0:
				goto done;
			case 'n':
				g_string_append_printf (s, "%d", sheet->index_in_wb);
				break;
			case 's':
				g_string_append (s, sheet->name_unquoted);
				break;
			case '%':
				g_string_append_c (s, '%');
				break;
			}
			template++;
			break;
		default:
			g_string_append_c (s, *template);
			template++;
		}
	}
}

static void
run_solver (Sheet *sheet, WorkbookView *wbv)
{
	GnmSolverParameters *params = sheet->solver_parameters;
	GError *err = NULL;
	WorkbookControl *wbc;
	GnmSolver *sol = NULL;

	wbc = g_object_new (WORKBOOK_CONTROL_TYPE, NULL);
	wb_control_set_view (wbc, wbv, NULL);

	/* Pick a functional algorithm.  */
	if (!gnm_solver_factory_functional (params->options.algorithm,
					    NULL)) {
		GSList *l;
		for (l = gnm_solver_db_get (); l; l = l->next) {
			GnmSolverFactory *factory = l->data;
			if (params->options.model_type != factory->type)
				continue;
			if (gnm_solver_factory_functional (factory, NULL)) {
				gnm_solver_param_set_algorithm (params,
								factory);
				break;
			}
		}
	}

	if (!gnm_solver_param_valid (params, &err))
		goto done;

	sol = params->options.algorithm
		? gnm_solver_factory_create (params->options.algorithm, params)
		: NULL;
	if (!sol) {
		g_set_error (&err, go_error_invalid (), 0,
			     _("Failed to create solver"));
		goto done;
	}

	if (!gnm_solver_start (sol, wbc, &err))
		goto done;

	while (!gnm_solver_finished (sol)) {
		g_main_context_iteration (NULL, TRUE);
	}

	if (sol->status != GNM_SOLVER_STATUS_DONE) {
		g_set_error (&err, go_error_invalid (), 0,
			     _("Solver ran, but failed"));
		goto done;
	}

	gnm_solver_store_result (sol);

 done:
	if (sol)
		g_object_unref (sol);
	if (err) {
		g_printerr (_("Solver: %s\n"), err->message);
		g_error_free (err);
	}
}


static int
convert (char const *inarg, char const *outarg, char const *mergeargs[],
	 GOCmdContext *cc)
{
	int res = 0;
	GOFileSaver *fs = NULL;
	GOFileOpener *fo = NULL;
	char *infile = go_shell_arg_to_uri (inarg);
	char *outfile = outarg ? go_shell_arg_to_uri (outarg) : NULL;
	WorkbookView *wbv;
	GOIOContext *io_context = NULL;
	Workbook *wb = NULL;

	if (ssconvert_export_id != NULL) {
		fs = go_file_saver_for_id (ssconvert_export_id);
		if (fs == NULL) {
			res = 1;
			g_printerr (_("Unknown exporter '%s'.\n"
				      "Try --list-exporters to see a list of possibilities.\n"),
				    ssconvert_export_id);
			goto out;
		} else if (outfile == NULL &&
			   go_file_saver_get_extension (fs) != NULL) {
			char const *ext = gsf_extension_pointer (infile);
			if (*infile) {
				GString *res = g_string_new (NULL);
				g_string_append_len (res, infile, ext - infile);
				g_string_append (res, go_file_saver_get_extension(fs));
				outfile = g_string_free (res, FALSE);
			}
		}
	} else {
		if (outfile != NULL) {
			fs = go_file_saver_for_file_name (outfile);
			if (fs == NULL) {
				res = 2;
				g_printerr (_("Unable to guess exporter to use for '%s'.\n"
					      "Try --list-exporters to see a list of possibilities.\n"),
					    outfile);
				goto out;
			}
			g_print ("Using exporter %s\n",
				 go_file_saver_get_id (fs));
		}
	}

	if (outfile == NULL) {
		g_printerr (_("An output file name or an explicit export type is required.\n"
			      "Try --list-exporters to see a list of possibilities.\n"));
		res = 1;
		goto out;
	}

	if (ssconvert_import_id != NULL) {
		fo = go_file_opener_for_id (ssconvert_import_id);
		if (fo == NULL) {
			res = 1;
			g_printerr (_("Unknown importer '%s'.\n"
				      "Try --list-importers to see a list of possibilities.\n"),
				    ssconvert_import_id);
			goto out;
		}
	}

	if (!fs)
		goto out;

	io_context = go_io_context_new (cc);
	if (mergeargs == NULL) {
		wbv = wb_view_new_from_uri (infile, fo,
					    io_context,
					    ssconvert_import_encoding);
	} else {
		wbv = workbook_view_new (NULL);
	}

	if (wbv == NULL || go_io_error_occurred (io_context)) {
		go_io_error_display (io_context);
		res = 1;
		goto out;
	}

	wb = wb_view_get_workbook (wbv);

	res = handle_export_options (fs, GO_DOC (wb));
	if (res) {
		g_object_unref (wb);
		goto out;
	}

	if (mergeargs != NULL) {
		if (merge (wb, mergeargs, fo, io_context, cc))
			goto out;
	}

	if (ssconvert_goal_seek) {
		int i;
		Sheet *sheet = wb_view_cur_sheet (wbv);

		for (i = 0; ssconvert_goal_seek[i]; i++) {
			setup_range (G_OBJECT (sheet),
				     "ssconvert-goal-seek",
				     wb,
				     ssconvert_goal_seek[i]);
			dialog_goal_seek (NULL, sheet);
		}
	}

	if (ssconvert_solve) {
		Sheet *sheet = wb_view_cur_sheet (wbv);
		run_solver (sheet, wbv);
	}

	if (ssconvert_recalc)
		workbook_recalc_all (wb);
	else
		workbook_recalc (wb);

	if (ssconvert_range)
		setup_range (G_OBJECT (wb),
			     "ssconvert-range",
			     wb,
			     ssconvert_range);
	else if (ssconvert_one_file_per_sheet ||
		 (workbook_sheet_count (wb) > 1 &&
		  go_file_saver_get_save_scope (fs) != GO_FILE_SAVE_WORKBOOK)) {
		if (ssconvert_one_file_per_sheet) {
			GSList *ptr, *sheets;
			char *template;
			res = 0;

			template = strchr (outarg, '%')
				? g_strdup (outarg)
				: g_strconcat (outarg, ".%n", NULL);

			sheets = workbook_sheets (wb);
			for (ptr = sheets; ptr; ptr = ptr->next) {
				Sheet *sheet = ptr->data;
				char *tmpfile =	resolve_template (template, sheet);
				wb_view_sheet_focus (wbv, sheet);
				res = !wb_view_save_as (wbv, fs, tmpfile, cc);
				g_free (tmpfile);
				if (res)
					break;
			}

			g_free (template);
			g_slist_free (sheets);
			goto out;
		} else
			g_printerr (_("Selected exporter (%s) does not support saving multiple sheets in one file.\n"
				      "Only the current sheet will be saved.  To get around this limitation, use -S.\n"),
				    go_file_saver_get_id (fs));
	}
	res = !wb_view_save_as (wbv, fs, outfile, cc);

 out:
	if (wb)
		g_object_unref (wb);
	if (io_context)
		g_object_unref (io_context);
	g_free (infile);
	g_free (outfile);

	return res;
}

int
main (int argc, char const **argv)
{
	GOErrorInfo	*plugin_errs;
	int		 res = 0;
	GOCmdContext	*cc;
	GOptionContext *ocontext;
	GError *error = NULL;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("INFILE [OUTFILE]"));
	g_option_context_add_main_entries (ocontext, ssconvert_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (char ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (ssconvert_show_version) {
		g_print (_("ssconvert version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			 GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	gnm_init ();

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}

	if (ssconvert_list_exporters)
		list_them (&go_get_file_savers,
			   (get_desc_f) &go_file_saver_get_id,
			   (get_desc_f) &go_file_saver_get_description);
	else if (ssconvert_list_importers)
		list_them (&go_get_file_openers,
			   (get_desc_f) &go_file_opener_get_id,
			   (get_desc_f) &go_file_opener_get_description);
	else if (ssconvert_merge_target!=NULL && argc>=3) {
		res = convert (argv[1], ssconvert_merge_target, argv+1, cc);
	} else if (argc == 2 || argc == 3) {
		res = convert (argv[1], argv[2], NULL, cc);
	} else {
		g_printerr (_("Usage: %s [OPTION...] %s\n"),
			    g_get_prgname (),
			    _("INFILE [OUTFILE]"));
		res = 1;
	}

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
