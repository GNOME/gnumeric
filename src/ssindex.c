/*
 * ssindex.c: A wrapper application to index spreadsheets
 *
 * Author:
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2004 Jody Goldberg
 * Copyright (C) 2008-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <gutils.h>
#include <gnumeric-paths.h>
#include <goffice/goffice.h>
#include <command-context-stderr.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <expr-name.h>
#include <value.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <hlink.h>
#include <validation.h>
#include <sheet-object-graph.h>
#include <gnm-plugin.h>
#include <gnumeric-conf.h>

#include <gsf/gsf-utils.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-stdio.h>

static gboolean ssindex_show_version = FALSE;
static gboolean ssindex_list_mime_types = FALSE;
static gboolean ssindex_run_indexer = FALSE;
static char *ssindex_import_encoding = NULL;

static GOptionEntry const ssindex_options [] = {
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &ssindex_show_version,
		N_("Display program version"),
		NULL
	},

	{
		"list-mime-types", 'm',
		0, G_OPTION_ARG_NONE, &ssindex_list_mime_types,
		N_("List MIME types which ssindex is able to read"),
		NULL
	},

	{
		"index", 'i',
		0, G_OPTION_ARG_NONE, &ssindex_run_indexer,
		N_("Index the given files"),
		NULL
	},

	{
		"encoding", 'E',
		0, G_OPTION_ARG_NONE, &ssindex_import_encoding,
		N_("Optionally specify an encoding for imported content"),
		N_("ENCODING")
	},

	/* ---------------------------------------- */

	{ NULL }
};


typedef struct {
	GOIOContext	   *context;
	WorkbookView const *wb_view;
	Workbook const	   *wb;
	Sheet		   *sheet;
	GsfXMLOut	   *output;
} IndexerState;

static void
ssindex_hlink (IndexerState *state, GnmHLink const *lnk)
{
	gchar const *str;

	str = gnm_hlink_get_target (lnk);
	if (str)
		gsf_xml_out_simple_element (state->output, "data", str);

	str = gnm_hlink_get_tip (lnk);
	if (str)
		gsf_xml_out_simple_element (state->output, "data", str);
}

static void
ssindex_validation (IndexerState *state, GnmValidation const *valid)
{
	if (valid->title) {
		const char *str = valid->title->str;
		if (str && *str)
			gsf_xml_out_simple_element (state->output, "data", str);
	}

	if (valid->msg) {
		const char *str = valid->msg->str;
		if (str && *str)
			gsf_xml_out_simple_element (state->output, "data", str);
	}
}

static void
cb_index_cell (G_GNUC_UNUSED gpointer ignore,
	       GnmCell const *cell, IndexerState *state)
{
	if (cell->value != NULL && VALUE_IS_STRING (cell->value)) {
		char const *str = value_peek_string (cell->value);
		if (str != NULL && *str)
			gsf_xml_out_simple_element (state->output, "data", str);
	}
}

static void
cb_index_styles (GnmStyle *style, IndexerState *state)
{
	if (gnm_style_is_element_set (style, MSTYLE_HLINK)) {
		GnmHLink const *lnk = gnm_style_get_hlink (style);
		if (lnk != NULL)
			ssindex_hlink (state, lnk);
	}

	if (gnm_style_is_element_set (style, MSTYLE_VALIDATION)) {
		GnmValidation const *valid = gnm_style_get_validation (style);
		if (valid)
			ssindex_validation (state, valid);
	}

	/* Input Msg? */
}

static void
ssindex_chart (IndexerState *state, GogObject *obj)
{
	GSList *ptr;

	/* TODO */
	for (ptr = obj->children ; ptr != NULL ; ptr = ptr->next)
		ssindex_chart (state, ptr->data);
}

static void
cb_index_name (G_GNUC_UNUSED gconstpointer key,
	       GnmNamedExpr const *nexpr, IndexerState *state)
{
	gsf_xml_out_simple_element (state->output,
				    "data", expr_name_name (nexpr));
}


/**
 * Other things we could index
 * - The names of external refernces
 * - functions used
 * - plugins used
 **/
static int
ssindex (char const *file, GOIOContext *ioc)
{
	int i, res = 0;
	GSList	   *objs, *ptr;
	char	   *str = go_shell_arg_to_uri (file);
	IndexerState state;
	GsfOutput  *gsf_stdout;
	Workbook   *wb;

	state.wb_view = workbook_view_new_from_uri (str, NULL,
		ioc, ssindex_import_encoding);
	g_free (str);

	if (state.wb_view == NULL)
		return 1;

	state.sheet = NULL;

	gsf_stdout = gsf_output_stdio_new_FILE ("<stdout>", stdout, TRUE);
	state.output = gsf_xml_out_new (gsf_stdout);
	gsf_xml_out_start_element (state.output, "gnumeric");
	state.wb = wb = wb_view_get_workbook (state.wb_view);

	workbook_foreach_name (wb, TRUE, (GHFunc)cb_index_name, &state);

	for (i = 0; i < workbook_sheet_count (wb); i++) {
		state.sheet = workbook_sheet_by_index (wb, i);
		gsf_xml_out_simple_element (state.output,
			"data", state.sheet->name_unquoted);

		/* cell content */
		sheet_cell_foreach (state.sheet,
			(GHFunc)&cb_index_cell, &state);

		/* now the objects */
		objs = sheet_objects_get (state.sheet, NULL, G_TYPE_NONE);
		for (ptr = objs ; ptr != NULL ; ptr = ptr->next) {
			GObject *obj = ptr->data;
			char *str = NULL;
			if (gnm_object_has_readable_prop (obj, "text",
							  G_TYPE_STRING, &str) &&
			    str) {
				gsf_xml_out_simple_element (state.output,
							    "data", str);
				g_free (str);
			} else if (GNM_IS_SO_GRAPH (obj))
				ssindex_chart (&state,
					       (GogObject *)sheet_object_graph_get_gog (GNM_SO (obj)));
		}
		g_slist_free (objs);

		/* Various stuff in styles.  */
		sheet_style_foreach (state.sheet,
				     (GFunc)cb_index_styles, &state);

		/* Local names.  */
		gnm_sheet_foreach_name (state.sheet,
					(GHFunc)cb_index_name, &state);
	}

	gsf_xml_out_end_element (state.output); /* </gnumeric> */
	gsf_output_close (gsf_stdout);
	g_object_unref (gsf_stdout);

	g_object_unref (wb);

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

	gnm_conf_set_persistence (FALSE);

	ocontext = g_option_context_new (_("INFILE..."));
	g_option_context_add_main_entries (ocontext, ssindex_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (ssindex_show_version) {
		g_printerr (_("ssindex version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	} else if (!ssindex_run_indexer && !ssindex_list_mime_types) {
		g_printerr (_("Usage: %s [OPTION...] %s\n"),
			    g_get_prgname (),
			    _("INFILE..."));
		return 1;
	}

	gnm_init ();

	cc = gnm_cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}
	go_component_set_default_command_context (cc);

	if (ssindex_run_indexer) {
		GOIOContext *ioc = go_io_context_new (cc);
		int i;

		go_io_context_set_num_files (ioc, argc - 1);

		for (i = 1; i < argc; i++) {
			char const *file = argv[i];
			go_io_context_processing_file (ioc, file);
			res |= ssindex (file, ioc);
		}
		g_object_unref (ioc);
	} else if (ssindex_list_mime_types) {
		GList *o;
		for (o = go_get_file_openers (); o != NULL ; o = o->next) {
			GSList const *mime = go_file_opener_get_mimes (o->data);
			for (; mime != NULL ; mime = mime->next)
				g_print ("%s\n", (char const *)mime->data);
		}
	}

	go_component_set_default_command_context (NULL);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
