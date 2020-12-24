/*
 * ssgrep.c: Search spreadsheets of selected strings
 *
 * Copyright (C) 2008 Jody Goldberg
 * Copyright (C) 2008-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <goffice/goffice.h>
#include <command-context-stderr.h>
#include <workbook-view.h>
#include <workbook.h>
#include <application.h>
#include <gutils.h>
#include <gnm-plugin.h>
#include <search.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <func.h>
#include <parse-util.h>
#include <sheet-object-cell-comment.h>
#include <gnumeric-conf.h>

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean ssgrep_locus_values = TRUE;
static gboolean ssgrep_locus_expressions = TRUE;
static gboolean ssgrep_locus_results = FALSE;
static gboolean ssgrep_locus_comments = TRUE;
static gboolean ssgrep_locus_scripts = TRUE;
static gboolean ssgrep_ignore_case = FALSE;
static gboolean ssgrep_match_words = FALSE;
static gboolean ssgrep_quiet = FALSE;
static gboolean ssgrep_count = FALSE;
static gboolean ssgrep_print_filenames = (gboolean)2;
static gboolean ssgrep_print_matching_filenames = FALSE;
static gboolean ssgrep_print_nonmatching_filenames = FALSE;
static gboolean ssgrep_print_locus = FALSE;
static gboolean ssgrep_print_type = FALSE;
static char *ssgrep_pattern = NULL;
static gboolean ssgrep_fixed_strings = FALSE;
static gboolean ssgrep_recalc = FALSE;
static gboolean ssgrep_invert_match = FALSE;
static gboolean ssgrep_string_table = FALSE;

static gboolean ssgrep_show_version = FALSE;
static char *ssgrep_pattern_file = NULL;

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
		"string-table-count", 'C',
		0, G_OPTION_ARG_NONE, &ssgrep_string_table,
		N_("Search only via the string table, display a count of the references."),
		NULL
	},

	{
		"pattern-file", 'f',
		0, G_OPTION_ARG_STRING, &ssgrep_pattern_file,
		N_("Get patterns from a file, one per line"),
		N_("FILE")
	},

	{
		"fixed-strings", 'F',
		0, G_OPTION_ARG_NONE, &ssgrep_fixed_strings,
		N_("Pattern is a set of fixed strings"),
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
		"files-with-matches", 'l',
		0, G_OPTION_ARG_NONE, &ssgrep_print_matching_filenames,
		N_("Print filenames with matches"),
		NULL
	},

	{
		"files-without-matches", 'L',
		0, G_OPTION_ARG_NONE, &ssgrep_print_nonmatching_filenames,
		N_("Print filenames without matches"),
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
		"search-results", 'R',
		0, G_OPTION_ARG_NONE, &ssgrep_locus_results,
		N_("Search results of expressions too"),
		NULL
	},

	{
		"print-type", 'T',
		0, G_OPTION_ARG_NONE, &ssgrep_print_type,
		N_("Print the location type of each match"),
		NULL
	},

	{
		"invert-match", 'v',
		0, G_OPTION_ARG_NONE, &ssgrep_invert_match,
		N_("Search for cells that do not match"),
		NULL
	},

	{
		"version", 'V',
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
		"recalc", 0,
		0, G_OPTION_ARG_NONE, &ssgrep_recalc,
		N_("Recalculate all cells"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

typedef struct {
	Workbook   *wb;
	GHashTable *targets;
	GHashTable *results;
	char const *lc_code;
} StringTableSearch;

static void
add_result (StringTableSearch *state, char const *clean, unsigned int n)
{
	gpointer prev;

	if (NULL == state->results)
		state->results = g_hash_table_new (g_str_hash, g_str_equal);
	else if (NULL != (prev = g_hash_table_lookup (state->results, clean)))
		n += GPOINTER_TO_UINT (prev);
	g_hash_table_replace (state->results, (gpointer) clean, GUINT_TO_POINTER (n));
}

static void
cb_check_strings (G_GNUC_UNUSED gpointer key, gpointer str, gpointer user_data)
{
	StringTableSearch *state = user_data;
	char *clean = g_utf8_strdown (key, -1);
	char const *orig = g_hash_table_lookup (state->targets, clean);
	if (NULL != orig)
		add_result (state, clean, go_string_get_ref_count (str));
	g_free (clean);
}

static void
cb_check_func (gpointer clean, gpointer orig, gpointer user_data)
{
	StringTableSearch *state = user_data;
	GnmFunc	*func = gnm_func_lookup (clean, state->wb);
	if (func && gnm_func_get_in_use (func))
		add_result (state, clean, 1);
}

static void
cb_find_target_in_module (gpointer clean, gpointer orig, gpointer user_data)
{
	StringTableSearch *state = user_data;
	unsigned n = 0;
	char const *ptr = state->lc_code;

	while (NULL != (ptr = strstr (ptr, clean))) {
		n++;
		ptr++;
	}

	if (n > 0)
		add_result (state, clean, n);
}

static void
cb_check_module (gpointer name, gpointer code, gpointer user_data)
{
	StringTableSearch *state = user_data;
	state->lc_code = g_utf8_strdown (code, -1);
	g_hash_table_foreach (state->targets, &cb_find_target_in_module, state);
	g_free ((gpointer)state->lc_code);
	state->lc_code = NULL;
}

static void
cb_dump_results (gpointer name, gpointer count)
{
	g_print ("\t%s : %u\n", (char const *)name, GPOINTER_TO_UINT (count));
}

static void
search_string_table (Workbook *wb, char const *file_name, GHashTable *targets)
{
	StringTableSearch	 state;
	GHashTable *modules;

	state.wb	= wb;
	state.targets	= targets;
	state.results	= NULL;
	go_string_foreach_base (&cb_check_strings, &state);
	g_hash_table_foreach (targets, &cb_check_func, &state);

	if (NULL != (modules = g_object_get_data (G_OBJECT (wb), "VBA")))
		g_hash_table_foreach (modules, &cb_check_module, &state);
	if (NULL != state.results) {
		g_print ("%s\n", file_name);
		g_hash_table_foreach (state.results, (GHFunc)&cb_dump_results, NULL);
		g_hash_table_destroy (state.results);
	}
}

static void
ssgrep (const char *arg, char const *uri, GOIOContext *ioc, GHashTable *targets, char const *pattern)
{
	WorkbookView *wbv;
	Workbook *wb;
	GnmSearchReplace *search;
	GPtrArray *cells;
	GPtrArray *matches;
	gboolean has_match;

	wbv = workbook_view_new_from_uri (uri, NULL, ioc, NULL);
	if (wbv == NULL) {
		ssgrep_error = TRUE;
		return;
	}
	wb = wb_view_get_workbook (wbv);

	if (ssgrep_locus_results) {
		if (ssgrep_recalc)
			workbook_recalc_all (wb);
		gnm_app_recalc ();
	}

	if (ssgrep_string_table) {
		search_string_table (wb, arg, targets);
		g_object_unref (wb);
		return;
	}

	search = (GnmSearchReplace*)
		g_object_new (GNM_SEARCH_REPLACE_TYPE,
			      "search-text", ssgrep_pattern,
			      "is-regexp", TRUE,
			      "invert", ssgrep_invert_match,
			      "ignore-case", ssgrep_ignore_case,
			      "match-words", ssgrep_match_words,
			      "search-strings", ssgrep_locus_values,
			      "search-other-values", ssgrep_locus_values,
			      "search-expressions", ssgrep_locus_expressions,
			      "search-expression-results", ssgrep_locus_results,
			      "search-comments", ssgrep_locus_comments,
			      "search-scripts", ssgrep_locus_scripts,
			      "sheet", workbook_sheet_by_index (wb, 0),
			      "scope", GNM_SRS_WORKBOOK,
			      NULL);

	cells = gnm_search_collect_cells (search);
	matches = gnm_search_filter_matching (search, cells);
	has_match = (matches->len > 0);

	if (has_match)
		ssgrep_any_matches = TRUE;

	if (ssgrep_quiet) {
		/* Nothing */
	} else if (ssgrep_print_nonmatching_filenames) {
		if (!has_match)
			g_print ("%s\n", arg);
	} else if (ssgrep_print_matching_filenames) {
		if (has_match)
			g_print ("%s\n", arg);
	} else if (ssgrep_count) {
		if (ssgrep_print_filenames)
			g_print ("%s:", arg);
		g_print ("%u\n", matches->len);
	} else {
		unsigned ui;
		for (ui = 0; ui < matches->len; ui++) {
			const GnmSearchFilterResult *item = g_ptr_array_index (matches, ui);
			char *txt = NULL;
			const char *locus_type = "";

			switch (item->locus) {
			case GNM_SRL_CONTENTS: {
				GnmCell const *cell =
					sheet_cell_get (item->ep.sheet,
							item->ep.eval.col,
							item->ep.eval.row);
				txt = gnm_cell_get_entered_text (cell);
				locus_type = _("cell");
				break;
			}

			case GNM_SRL_VALUE: {
				GnmCell const *cell =
					sheet_cell_get (item->ep.sheet,
							item->ep.eval.col,
							item->ep.eval.row);
				if (cell && cell->value)
					txt = value_get_as_string (cell->value);
				locus_type = _("result");
				break;
			}

			case GNM_SRL_COMMENT: {
				GnmComment *comment = sheet_get_comment (item->ep.sheet, &item->ep.eval);
				txt = g_strdup (cell_comment_text_get (comment));
				locus_type = _("comment");
				break;
			}
			default:
				; /* Probably should not happen.  */
			}

			if (ssgrep_print_filenames)
				g_print ("%s:", arg);

			if (ssgrep_print_type)
				g_print ("%s:", locus_type);

			if (ssgrep_print_locus)
				g_print ("%s!%s:",
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

/* simple stripped down hash of lower case target, only used for string table
 * searches */
static void
add_target (GHashTable *ssgrep_targets, char const *target)
{
	char *orig = g_strstrip (g_strdup (target));
	char *clean = g_utf8_strdown (orig, -1);
	g_hash_table_insert (ssgrep_targets, clean, orig);
}

int
main (int argc, char const **argv)
{
	GHashTable	*ssgrep_targets;
	GOErrorInfo	*plugin_errs;
	GOIOContext	*ioc;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	int		 i, N;
	const char *argv_stdin[] = { "fd://0", NULL };

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	gnm_conf_set_persistence (FALSE);

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

	gnm_init ();

	ssgrep_targets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	if (ssgrep_pattern_file) {
		char *uri = go_shell_arg_to_uri (ssgrep_pattern_file);
		GsfInput	 *input;
		GsfInputTextline *textline;
		GError		 *err = NULL;
		const unsigned char *line;
		GString *pat;

		input = go_file_open (uri, &err);
		g_free (uri);

		if (!input) {
			g_printerr (_("%s: Cannot read %s: %s\n"),
				    g_get_prgname (), ssgrep_pattern_file, err->message);
			g_error_free (err);
			return 1;
		}

		textline = (GsfInputTextline *)gsf_input_textline_new (input);
		g_object_unref (input);

		pat = g_string_new (NULL);
		while (NULL != (line = gsf_input_textline_ascii_gets (textline))) {
			if (pat->len)
				g_string_append_c (pat, '|');

			if (ssgrep_fixed_strings)
				go_regexp_quote (pat, line);
			else
				g_string_append (pat, line);

			add_target (ssgrep_targets, line);
		}

		ssgrep_pattern = g_string_free (pat, FALSE);

		g_object_unref (textline);

		i = 1;
		N = argc - i;
	} else {
		if (argc < 2) {
			g_printerr (_("%s: Missing pattern\n"), g_get_prgname ());
			return 1;
		}

		if (ssgrep_fixed_strings) {
			GString *pat = g_string_new (NULL);
			go_regexp_quote (pat, argv[1]);
			ssgrep_pattern = g_string_free (pat, FALSE);
		} else
			ssgrep_pattern = g_strdup (argv[1]);
		add_target (ssgrep_targets, argv[1]);

		i = 2;
		N = argc - i;
	}

	if (argv[i] == NULL) {
		argv = argv_stdin;
		i = 0;
		N = 1;
	}

	cc = gnm_cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}

	ioc = go_io_context_new (cc);
	go_io_context_set_num_files (ioc, N);
	go_component_set_default_command_context (cc);

	if (ssgrep_print_filenames == (gboolean)2)
		ssgrep_print_filenames = (N > 1);

	for (; argv[i]; i++) {
		const char *arg = argv[i];
		char *uri = go_shell_arg_to_uri (arg);
		go_io_context_processing_file (ioc, uri);
		ssgrep (arg, uri, ioc, ssgrep_targets, ssgrep_pattern);
		g_free (uri);
	}

	g_hash_table_destroy (ssgrep_targets);

	go_component_set_default_command_context (NULL);
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
