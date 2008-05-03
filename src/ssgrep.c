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
#include "str.h"
#include "func.h"
#include "gutils.h"
#include "gnm-plugin.h"

#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean ssgrep_show_version = FALSE;
static char *ssgrep_keyword_file = NULL;

static GOptionEntry const ssgrep_options [] = {
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &ssgrep_show_version,
		N_("Display program version"),
		NULL
	},

	{
		"keyword-file", 'f',
		0, G_OPTION_ARG_STRING, &ssgrep_keyword_file,
		N_("Optionally specify which importer to use"),
		N_("KEYWORD_FILE")
	},
	/* ---------------------------------------- */

	{ NULL }
};

typedef struct {
	GHashTable *targets;
	gboolean    found;
	char const *file_name;
	Workbook   *wb;
} GrepState;

static void
cb_check_strings (G_GNUC_UNUSED gpointer key, gpointer str,
		  gpointer user_data)
{
	GrepState *state = user_data;
	char *clean = g_utf8_strdown (key, -1);
	char const *target = g_hash_table_lookup (state->targets, clean);

	if (NULL != target) {
		if (!state->found) {
			g_print ("%s\n", state->file_name);
			state->found = TRUE;
		}
		g_print ("\t%s : %d (string)\n", target, ((GnmString *)str)->ref_count);
	}

	g_free (clean);
}

static void
cb_check_func (gpointer clean, gpointer orig,
	       gpointer user_data)
{
	GrepState *state = user_data;
	GnmFunc	  *func = gnm_func_lookup (clean, state->wb);
	if (NULL != func) {
		if (!state->found) {
			g_print ("%s\n", state->file_name);
			state->found = TRUE;
		}
		g_print ("\t%s : %d (func)\n", (char const *)orig, func->ref_count);
	}
}

static int
ssgrep (char const *file_name, IOContext *ioc, GHashTable *targets)
{
	int res = 0;
	char		*str;
	WorkbookView	*wbv;
	GrepState	 state;

	str = go_shell_arg_to_uri (file_name);
	wbv = wb_view_new_from_uri (str, NULL, ioc, NULL);
	g_free (str);

	if (wbv == NULL)
		return 1;

	state.wb	= wb_view_get_workbook (wbv);
	state.targets	= targets;
	state.file_name = file_name;
	state.found	= FALSE;
	gnm_string_foreach (&cb_check_strings, &state);
	g_hash_table_foreach (targets, &cb_check_func, &state);

	g_object_unref (state.wb);

	return res;
}

static void
add_target (GHashTable *targets, char const *target)
{
	char *orig = g_strstrip (g_strdup (target));
	char *clean = g_utf8_strdown (orig, -1);
	g_hash_table_insert (targets, clean, orig);
}

int
main (int argc, char const **argv)
{
	ErrorInfo	*plugin_errs;
	IOContext	*ioc;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	GHashTable	*targets;
	int		 i, res;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);
	gnm_init (FALSE);

	ocontext = g_option_context_new (_("INFILE..."));
	g_option_context_add_main_entries (ocontext, ssgrep_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (ssgrep_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	} 

	targets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	if (ssgrep_keyword_file) {
		GsfInput     	 *input;
		GsfInputTextline *textline;
		GError       	 *err = NULL;
		unsigned char	 *line;

		if (NULL == (input = gsf_input_stdio_new (ssgrep_keyword_file, &err))) {
			g_return_val_if_fail (err != NULL, 1);

			g_printerr ("'%s' error: %s", ssgrep_keyword_file, err->message);
			g_error_free (err);
			return 1;
		}

		if (NULL == (textline = (GsfInputTextline *)gsf_input_textline_new (input))) {
			g_printerr ("Unable to create a textline");	/* unexpected */
			return 2;
		}

		while (NULL != (line = gsf_input_textline_ascii_gets (textline)))
			add_target (targets, line);
		g_object_unref (G_OBJECT (input));

		i = 1;
	} else if (argc > 2) {
		add_target (targets, argv[1]);
		i = 2;
	} else {
		g_hash_table_destroy (targets);
		targets = NULL;
	}

	if (NULL == targets) {
		g_printerr (_("Usage: %s [ keyword | -f keywordfile ] FILES...]\n"),
			    g_get_prgname ());
		return 1;
	}

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);

	ioc = gnumeric_io_context_new (cc);
	gnm_io_context_set_num_files (ioc, argc - 1);

	res = 0;
	for (; i < argc; i++) {
		char const *file_name = argv[i];
		gnm_io_context_processing_file (ioc, file_name);
		res |= ssgrep (file_name, ioc, targets);
	}

	g_hash_table_destroy (targets);

	g_object_unref (ioc);

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
