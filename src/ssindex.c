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

#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/graph/gog-object.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-stdio.h>
#include <string.h>
#include <stdio.h>

static gboolean ssindex_show_version = FALSE;
static gboolean ssindex_list_mime_types = FALSE;
static gboolean ssindex_run_indexer	= FALSE;
static char const *ssindex_import_encoding = NULL;

#ifdef WIN32
#define POPT_STATIC
#endif
const struct poptOption
ssindex_popt_options[] = {
	{ NULL, '\0', POPT_ARG_INTL_DOMAIN, (char *)GETTEXT_PACKAGE, 0, NULL, NULL },

	{ "version", 'v', POPT_ARG_NONE, &ssindex_show_version, 0,
	  N_("Display Gnumeric's version"), NULL  },

	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },

	{ "list-mime-types",	'm', POPT_ARG_NONE, &ssindex_list_mime_types, 0,
	  N_("Optionally specify an encoding for imported content"), NULL },
	{ "index",		'i', POPT_ARG_NONE, &ssindex_run_indexer, 0,
	  N_("Optionally specify an encoding for imported content"), NULL },
	{ "import-encoding", 'E', POPT_ARG_STRING, &ssindex_import_encoding, 0,
	  N_("Optionally specify an encoding for imported content"), N_("ENCODING")  },

	POPT_AUTOHELP
	POPT_TABLEEND
};

typedef struct {
	IOContext 	   *context;
	WorkbookView const *wb_view;
	Workbook const	   *wb;
	Sheet 		   *sheet;
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
	if (mstyle_is_element_set (style, MSTYLE_HLINK)) {
		guchar const *str;
		GnmHLink const *lnk = mstyle_get_hlink (style);
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

	state.wb_view = wb_view_new_from_uri (str, NULL,
		ioc, ssindex_import_encoding);
	g_free (str);

	if (state.wb_view == NULL)
		return 1;

	gsf_stdout = gsf_output_stdio_new_FILE ("<stdout>", stdout, TRUE);
	state.output = gsf_xml_out_new (gsf_stdout);
	gsf_xml_out_start_element (state.output, "gnumeric");
	state.wb = wb_view_workbook (state.wb_view);
	for (i = 0 ; i < workbook_sheet_count (state.wb); i++) {
		state.sheet = workbook_sheet_by_index (state.wb, i);
		gsf_xml_out_simple_element (state.output,
			"data", state.sheet->name_unquoted);

		/* cell content */
		sheet_foreach_cell (state.sheet,
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

	g_object_unref ((gpointer)state.wb);

	return res;
}

int
main (int argc, char const *argv [])
{
	ErrorInfo	*plugin_errs;
	int		 res = 0;
	GOCmdContext	*cc;
	poptContext ctx;

	gnm_pre_parse_init (argv[0]);

	ctx = poptGetContext (NULL, argc, argv, ssindex_popt_options, 0);
	while (poptGetNextOpt (ctx) > 0)
		;

	if (ssindex_show_version) {
		printf (_("ssindex version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, gnumeric_data_dir, gnumeric_lib_dir);
		poptFreeContext (ctx);
		return 0;
	} else if (!ssindex_run_indexer && !ssindex_list_mime_types) {
		poptPrintUsage(ctx, stderr, 0);
		return 1;
	}

	gnm_common_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);

	if (ssindex_run_indexer) {
		IOContext *ioc = gnumeric_io_context_new (cc);
		char const **args = poptGetArgs (ctx);
		int argc = 0;

		while (args != NULL && args[argc] != NULL)
			argc++;
		gnm_io_context_set_num_files (ioc, argc);

		for (; args != NULL && *args ; args++) {
			gnm_io_context_processing_file (ioc, *args);
			printf ("-> %s\n", *args);
			res |= ssindex (*args, ioc);
		}
		g_object_unref (ioc);
	} else if (ssindex_list_mime_types) {
		GList  *o;
		for (o = get_file_openers (); o != NULL ; o = o->next) {
			GSList const *mime = go_file_opener_get_mimes (o->data);
			for (; mime != NULL ; mime = mime->next)
				printf ("%s\n", (char const *)mime->data);
		}
	}

	poptFreeContext (ctx);
	g_object_unref (cc);
	gnm_shutdown ();

	return res;
}
