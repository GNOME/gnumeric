/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ssindex.c: A wrapper application to index spreadsheets
 *
 * Author:
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2004 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include "gutils.h"
#include "gnumeric-paths.h"
#include <goffice/app/go-plugin.h>
#include "command-context-stderr.h"
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include <goffice/app/file.h>
#include "workbook.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "mstyle.h"
#include "sheet-style.h"
#include "hlink.h"
#include "sheet-object-graph.h"
#include "gnm-plugin.h"

#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/graph/gog-object.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-stdio.h>
#include <string.h>

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
	IOContext	   *context;
	WorkbookView const *wb_view;
	Workbook const	   *wb;
	Sheet		   *sheet;
	GsfXMLOut	   *output;
} IndexerState;

static void
cb_index_cell (G_GNUC_UNUSED gpointer ignore,
	       GnmCell const *cell, IndexerState *state)
{
	if (cell->value != NULL && VALUE_IS_STRING (cell->value)) {
		char const *str = value_peek_string (cell->value);
		if (str != NULL && *str)
			gsf_xml_out_simple_element (state->output,
				"data", value_peek_string (cell->value));
	}
}

static void
cb_index_styles (GnmStyle *style, gconstpointer dummy, IndexerState *state)
{
	if (gnm_style_is_element_set (style, MSTYLE_HLINK)) {
		gchar const *str;
		GnmHLink const *lnk = gnm_style_get_hlink (style);
		if (lnk != NULL) {
			if (NULL != (str = gnm_hlink_get_target (lnk)))
				gsf_xml_out_simple_element (state->output, "data", str);
			if (NULL != (str = gnm_hlink_get_tip (lnk)))
				gsf_xml_out_simple_element (state->output, "data", str);
		}
	}
}

static void
ssindex_chart (IndexerState *state, GogObject *obj)
{
	GSList *ptr;

	/* TODO */
	for (ptr = obj->children ; ptr != NULL ; ptr = ptr->next)
		ssindex_chart (state, ptr->data);
}

/**
 * Other things we could index
 * - The names of external refernces
 * - functions used
 * - plugins used
 **/
static int
ssindex (char const *file, IOContext *ioc)
{
	int i, res = 0;
	GParamSpec *pspec;
	GSList	   *objs, *ptr;
	char	   *str = go_shell_arg_to_uri (file);
	IndexerState state;
	GsfOutput  *gsf_stdout;
	Workbook   *wb;

	state.wb_view = wb_view_new_from_uri (str, NULL,
		ioc, ssindex_import_encoding);
	g_free (str);

	if (state.wb_view == NULL)
		return 1;

	gsf_stdout = gsf_output_stdio_new_FILE ("<stdout>", stdout, TRUE);
	state.output = gsf_xml_out_new (gsf_stdout);
	gsf_xml_out_start_element (state.output, "gnumeric");
	state.wb = wb = wb_view_get_workbook (state.wb_view);
	for (i = 0 ; i < workbook_sheet_count (wb); i++) {
		state.sheet = workbook_sheet_by_index (wb, i);
		gsf_xml_out_simple_element (state.output,
			"data", state.sheet->name_unquoted);

		/* cell content */
		sheet_cell_foreach (state.sheet,
			(GHFunc)&cb_index_cell, &state);

		/* now the objects */
		objs = sheet_objects_get (state.sheet, NULL, G_TYPE_NONE);
		for (ptr = objs ; ptr != NULL ; ptr = ptr->next) {
			pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (ptr->data), "text");
			if (pspec != NULL) {
				g_object_get (G_OBJECT (ptr->data), "text", &str, NULL);
				if (str != NULL) {
					gsf_xml_out_simple_element (state.output,
						"data", str);
					g_free (str);
				}
			} else if (IS_SHEET_OBJECT_GRAPH (ptr->data))
				ssindex_chart (&state,
					(GogObject *)sheet_object_graph_get_gog (ptr->data));
		}
		g_slist_free (objs);

		/* and finally the hyper-links */
		sheet_style_foreach (state.sheet,
			(GHFunc)&cb_index_styles, &state);
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
	ErrorInfo	*plugin_errs;
	int		 res = 0;
	GOCmdContext	*cc;
	GOptionContext *ocontext;
	GError *error = NULL;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

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

	gnm_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);

	if (ssindex_run_indexer) {
		IOContext *ioc = gnumeric_io_context_new (cc);
		int i;

		gnm_io_context_set_num_files (ioc, argc - 1);

		for (i = 1; i < argc; i++) {
			char const *file = argv[i];
			gnm_io_context_processing_file (ioc, file);
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

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
