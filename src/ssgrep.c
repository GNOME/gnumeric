/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ssgrep.c: Search spreadsheets of selected strings
 *
 * Author:
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2008 Jody Goldberg
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include <goffice/app/go-plugin.h>
#include "command-context-stderr.h"
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "search.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "parse-util.h"
#include "sheet-object-cell-comment.h"

#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean ssgrep_locus_values = TRUE;
static gboolean ssgrep_locus_expressions = TRUE;
static gboolean ssgrep_locus_results = FALSE;
static gboolean ssgrep_locus_comments = TRUE;
static gboolean ssgrep_ignore_case = FALSE;
static gboolean ssgrep_match_words = FALSE;
static gboolean ssgrep_quiet = FALSE;
static gboolean ssgrep_count = FALSE;
static gboolean ssgrep_print_filenames = (gboolean)2;
static gboolean ssgrep_print_locus = FALSE;
static char *ssgrep_pattern = NULL;
static gboolean ssgrep_recalc = FALSE;

static gboolean ssgrep_show_version = FALSE;
static char *ssgrep_keyword_file = NULL;

static gboolean ssgrep_error = FALSE;
static gboolean ssgrep_any_matches = FALSE;

static GOptionEntry const ssgrep_options [] = {
	{
		"count", 'c',
		0, G_OPTION_ARG_NONE, &ssgrep_count,
		N_("Only print a count of matches per file"),
		NULL
	},

	{
		"with-filename", 'H',
		0, G_OPTION_ARG_NONE, &ssgrep_print_filenames,
		N_("Print the filename for each match"),
		NULL
	},

	{
		"without-filename", 'h',
		G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &ssgrep_print_filenames,
		N_("Do not print the filename for each match"),
		NULL
	},

	{
		"ignore-case", 'i',
		0, G_OPTION_ARG_NONE, &ssgrep_ignore_case,
		N_("Ignore differences in letter case"),
		NULL
	},

	{
		"print-locus", 'n',
		0, G_OPTION_ARG_NONE, &ssgrep_print_locus,
		N_("Print the location of each match"),
		NULL
	},

	{
		"quiet", 'q',
		0, G_OPTION_ARG_NONE, &ssgrep_quiet,
		N_("Suppress all normal output"),
		NULL
	},

	{
		"version", 0,
		0, G_OPTION_ARG_NONE, &ssgrep_show_version,
		N_("Display program version"),
		NULL
	},

	{
		"word-regexp", 'w',
		0, G_OPTION_ARG_NONE, &ssgrep_match_words,
		N_("Match only whole words"),
		NULL
	},

	{
		"keyword-file", 'f',
		0, G_OPTION_ARG_STRING, &ssgrep_keyword_file,
		N_("Get keywords from a file, one per line"),
		N_("KEYWORD_FILE")
	},

	{
		"recalc", 0,
		0, G_OPTION_ARG_NONE, &ssgrep_recalc,
		N_("Recalculate all cells before matching values"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

static void
ssgrep (const char *arg, char const *uri, IOContext *ioc)
{
	WorkbookView *wbv;
	Workbook *wb;
	GnmSearchReplace *search;
	GPtrArray *cells;
	GPtrArray *matches;

	wbv = wb_view_new_from_uri (uri, NULL, ioc, NULL);
	if (wbv == NULL) {
		ssgrep_error = TRUE;
		return;
	}
	wb = wb_view_get_workbook (wbv);

	if (ssgrep_recalc && ssgrep_locus_results)
		workbook_recalc_all (wb);

	search = (GnmSearchReplace*)
		g_object_new (GNM_SEARCH_REPLACE_TYPE,
			      "search-text", ssgrep_pattern,
			      "is-regexp", TRUE,
			      "ignore-case", ssgrep_ignore_case,
			      "match-words", ssgrep_match_words,
			      "search-strings", ssgrep_locus_values,
			      "search-other-values", ssgrep_locus_values,
			      "search-expressions", ssgrep_locus_expressions,
			      "search-expression-results", ssgrep_locus_results,
			      "search-comments", ssgrep_locus_comments,
			      "sheet", workbook_sheet_by_index (wb, 0),
			      "scope", GNM_SRS_WORKBOOK,
			      NULL);

	cells = gnm_search_collect_cells (search);
	matches = gnm_search_filter_matching (search, cells);

	if (matches->len > 0)
		ssgrep_any_matches = TRUE;

	if (ssgrep_quiet) {
		/* Nothing */
	} else if (ssgrep_count) {
		if (ssgrep_print_filenames)
			g_print ("%s:", arg);
		g_print ("%u\n", matches->len);
	} else {
		unsigned ui;
		for (ui = 0; ui < matches->len; ui++) {
			const GnmSearchFilterResult *item = g_ptr_array_index (matches, ui);
			char *txt = NULL;
			const char *locus_prefix = "";

			switch (item->locus) {
			case GNM_SRL_CONTENTS: {
				GnmCell const *cell =
					sheet_cell_get (item->ep.sheet,
							item->ep.eval.col,
							item->ep.eval.row);
				txt = gnm_cell_get_entered_text (cell);
				break;
			}

			case GNM_SRL_VALUE: {
				GnmCell const *cell =
					sheet_cell_get (item->ep.sheet,
							item->ep.eval.col,
							item->ep.eval.row);
				if (cell && cell->value)
					txt = value_get_as_string (cell->value);
				break;
			}

			case GNM_SRL_COMMENT: {
				GnmComment *comment = sheet_get_comment (item->ep.sheet, &item->ep.eval);
				txt = g_strdup (cell_comment_text_get (comment));
				locus_prefix = _("Comment of ");
				break;
			}
			default:
				; /* Probably should not happen.  */
			}

			if (ssgrep_print_filenames)
				g_print ("%s:", arg);

			if (ssgrep_print_locus)
				g_print ("%s%s!%s:",
					 locus_prefix,
					 item->ep.sheet->name_quoted,
					 cellpos_as_string (&item->ep.eval));

			if (txt) {
				g_print ("%s\n", txt);
				g_free (txt);
			} else
				g_print ("\n");
		}
	}

	gnm_search_filter_matching_free (matches);
	gnm_search_collect_cells_free (cells);
	g_object_unref (search);
	g_object_unref (wb);
}

int
main (int argc, char const **argv)
{
	ErrorInfo	*plugin_errs;
	IOContext	*ioc;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	int		 i, N;
	const char *argv_stdin[] = { "fd://1", NULL };

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("PATTERN INFILE..."));
	g_option_context_add_main_entries (ocontext, ssgrep_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, g_get_prgname ());
		g_error_free (error);
		return 1;
	}

	if (ssgrep_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	} 

	if (ssgrep_keyword_file) {
		char *uri = go_shell_arg_to_uri (ssgrep_keyword_file);
		GsfInput     	 *input;
		GsfInputTextline *textline;
		GError       	 *err = NULL;
		const unsigned char *line;
		GString *pat;

		input = go_file_open (uri, &err);
		g_free (uri);

		if (!input) {
			g_printerr (_("%s: Cannot read %s: %s\n"),
				    g_get_prgname (), ssgrep_keyword_file, err->message);
			g_error_free (err);
			return 1;
		}

		textline = (GsfInputTextline *)gsf_input_textline_new (input);
		g_object_unref (G_OBJECT (input));

		pat = g_string_new (NULL);
		while (NULL != (line = gsf_input_textline_ascii_gets (textline))) {
			if (pat->len)
				g_string_append_c (pat, '|');
			g_string_append (pat, line);
		}

		ssgrep_pattern = g_string_free (pat, FALSE);

		g_object_unref (G_OBJECT (textline));

		i = 1;
		N = argc - i;
	} else {
		if (argc < 2) {
			g_printerr (_("%s: Missing pattern\n"), g_get_prgname ());
			return 1;
		}
		ssgrep_pattern = g_strdup (argv[1]);
		i = 2;
		N = argc - i;
	}

	if (argv[i] == NULL) {
		argv = argv_stdin;
		i = 0;
		N = 1;
	}

	gnm_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);

	ioc = gnumeric_io_context_new (cc);
	gnm_io_context_set_num_files (ioc, N);

	if (ssgrep_print_filenames == (gboolean)2)
		ssgrep_print_filenames = (N > 1);

	for (; argv[i]; i++) {
		const char *arg = argv[i];
		char *uri = go_shell_arg_to_uri (arg);
		gnm_io_context_processing_file (ioc, uri);
		ssgrep (arg, uri, ioc);
		g_free (uri);
	}

	g_object_unref (ioc);

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	/* This special case matches what "man grep" says.  */
	if (ssgrep_quiet && ssgrep_any_matches)
		return 0;

	if (ssgrep_error)
		return 2;

	return ssgrep_any_matches ? 0 : 1;
}
