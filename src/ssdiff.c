/*
 * ssdiff.c: A diff program for spreadsheets.
 *
 * Author:
 *   Morten Welinder <terra@gnome.org>
 *
 * Copyright (C) 2012 Morten Welinder (terra@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include <goffice/goffice.h>
#include "libgnumeric.h"
#include "gutils.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "gnm-plugin.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"

static gboolean ssdiff_show_version = FALSE;

static const GOptionEntry ssdiff_options [] = {
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &ssdiff_show_version,
		N_("Display program version"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

typedef struct GnmDiffHandler_ {
	GOIOContext *ioc;
	struct {
		char *url;
		Workbook *wb;
		WorkbookView *wbv;
	} old, new;
} GnmDiffHandler;

static gint
cell_ordering (gconstpointer a_, gconstpointer b_)
{
	GnmCell const *a = *(GnmCell **)a_;
	GnmCell const *b = *(GnmCell **)b_;

	if (a->pos.row != b->pos.row)
		return a->pos.row - b->pos.row;

	return a->pos.col - b->pos.col;
}

static gboolean
compare_cells (GnmCell const *co, GnmCell const *cn)
{
	gboolean has_expr = gnm_cell_has_expr (co);
	gboolean has_value = co->value != NULL;

	if (has_expr != gnm_cell_has_expr (cn))
		return TRUE;
	if (has_expr) {
		char *eo = gnm_cell_get_entered_text (co);
		char *en = gnm_cell_get_entered_text (cn);
		gboolean changed = !g_str_equal (co, cn);
		g_free (eo);
		g_free (en);
		return changed;
	}

	if (has_value != (cn->value != NULL))
		return TRUE;
	if (has_value)
		return !value_equal (co->value, cn->value);

	return FALSE;
}
      

static void
diff_sheets_cells (GnmDiffHandler *state, Sheet *old_sheet, Sheet *new_sheet)
{
	GPtrArray *old_cells = sheet_cells (old_sheet, NULL);
	GPtrArray *new_cells = sheet_cells (new_sheet, NULL);
	size_t io = 0, in = 0;

	/* Make code below simpler.  */
	g_ptr_array_add (old_cells, NULL);
	g_ptr_array_add (new_cells, NULL);

	while (TRUE) {
		GnmCell const *co = g_ptr_array_index (old_cells, io);
		GnmCell const *cn = g_ptr_array_index (new_cells, in);

		if (co && cn) {
			int order = cell_ordering (&co, &cn);
			if (order < 0)
				cn = NULL;
			else if (order > 0)
				co = NULL;
			else {
				if (compare_cells (co, cn)) {
					g_printerr ("Contents of %s!%s has changed.\n",
						    old_sheet->name_quoted,
						    cell_name (co));
				}

				io++, in++;
				continue;
			}			
		}

		if (co) {
			g_printerr ("Cell %s!%s removed.\n",
				    old_sheet->name_quoted, cell_name (co));
			io++;
		} else if (cn) {
			g_printerr ("Cell %s!%s added.\n",
				    new_sheet->name_quoted, cell_name (cn));
			in++;
		} else
			break;
	}

	g_ptr_array_free (old_cells, TRUE);
	g_ptr_array_free (new_cells, TRUE);
}

static void
diff_sheets (GnmDiffHandler *state, Sheet *old_sheet, Sheet *new_sheet)
{
	/* Compare sheet attributes and sizes */

	diff_sheets_cells (state, old_sheet, new_sheet);

	/* Compare style */
}

static int
diff (char const *oldfilename, char const *newfilename, GOIOContext *ioc)
{
	GnmDiffHandler state;
	int res = 0;
	int i, count;

	memset (&state, 0, sizeof (state));
	state.ioc = ioc;

	state.old.url = go_shell_arg_to_uri (oldfilename);
	state.new.url = go_shell_arg_to_uri (newfilename);

	state.old.wbv = workbook_view_new_from_uri (state.old.url, NULL,
						    ioc, NULL);
	if (!state.old.wbv)
		goto error;
	state.old.wb = wb_view_get_workbook (state.old.wbv);

	state.new.wbv = workbook_view_new_from_uri (state.new.url, NULL,
						    ioc, NULL);
	if (!state.new.wbv)
		goto error;
	state.new.wb = wb_view_get_workbook (state.new.wbv);

	count = workbook_sheet_count (state.old.wb);
	for (i = 0; i < count; i++) {
		Sheet *old_sheet = workbook_sheet_by_index (state.old.wb, i);
		Sheet *new_sheet = workbook_sheet_by_name (state.new.wb,
							   old_sheet->name_unquoted);
		if (new_sheet)
			diff_sheets (&state, old_sheet, new_sheet);
		else {
			g_printerr ("Sheet %s removed.\n",
				    old_sheet->name_quoted);
		}
	}

	count = workbook_sheet_count (state.new.wb);
	for (i = 0; i < count; i++) {
		Sheet *new_sheet = workbook_sheet_by_index (state.new.wb, i);
		Sheet *old_sheet = workbook_sheet_by_name (state.old.wb,
							   new_sheet->name_unquoted);
		if (old_sheet)
			; /* Nothing */
		else {
			g_printerr ("Sheet %s added.\n",
				    new_sheet->name_quoted);
		}
	}

out:
	g_free (state.old.url);
	g_free (state.new.url);
	if (state.old.wb)
		g_object_unref (state.old.wb);
	if (state.new.wb)
		g_object_unref (state.new.wb);
	return res;

error:
	res = 1;
	goto out;
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

	ocontext = g_option_context_new (_("OLDFILE NEWFILE"));
	g_option_context_add_main_entries (ocontext, ssdiff_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (char ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (ssdiff_show_version) {
		g_print (_("ssdiff version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
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
	go_component_set_default_command_context (cc);

	if (argc == 3) {
		GOIOContext *ioc = go_io_context_new (cc);
		res = diff (argv[1], argv[2], ioc);
		g_object_unref (ioc);
	} else {
		g_printerr (_("Usage: %s [OPTION...] %s\n"),
			    g_get_prgname (),
			    _("OLDFILE NEWFILE"));
		res = 1;
	}

	go_component_set_default_command_context (NULL);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
