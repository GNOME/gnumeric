/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ssconvert.c: A wrapper application to convert spreadsheet formats
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *   Morten Welinder <terra@diku.dk>
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2002-2003 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include "gnumeric-paths.h"
#include "gnm-plugin.h"
#include "command-context-stderr.h"
#include "command-context.h"
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include <goffice/app/file.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context.h>
#include <gsf/gsf-utils.h>
#include <string.h>

static gboolean ssconvert_show_version = FALSE;
static gboolean ssconvert_list_exporters = FALSE;
static gboolean ssconvert_list_importers = FALSE;
static gboolean ssconvert_one_file_per_sheet = FALSE;
static char const *ssconvert_import_encoding = NULL;
static char const *ssconvert_import_id = NULL;
static char const *ssconvert_export_id = NULL;

#ifdef WIN32
#define POPT_STATIC
#endif
struct poptOption
ssconvert_popt_options[] = {
	{ NULL, '\0', POPT_ARG_INTL_DOMAIN, (char *)GETTEXT_PACKAGE, 0, NULL, NULL },

	{ "version", 'v', POPT_ARG_NONE, &ssconvert_show_version, 0,
	  N_("Display Gnumeric's version"), NULL  },

	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },

	{ "import-encoding", 'E', POPT_ARG_STRING, &ssconvert_import_encoding, 0,
	  N_("Optionally specify an encoding for imported content"), N_("ENCODING")  },
	{ "import-type", 'I', POPT_ARG_STRING, &ssconvert_import_id, 0,
	  N_("Optionally specify which importer to use"), "ID"  },
	{ "export-type", 'T', POPT_ARG_STRING, &ssconvert_export_id, 0,
	  N_("Optionally specify which exporter to use"), "ID"  },
	{ "list-exporters", '\0', POPT_ARG_NONE, &ssconvert_list_exporters, 0,
	  N_("List the available exporters"), NULL },
	{ "list-importers", '\0', POPT_ARG_NONE, &ssconvert_list_importers, 0,
	  N_("List the available importers"), NULL },
	{ "export-file-per-sheet", 'S', POPT_ARG_NONE, &ssconvert_one_file_per_sheet, 0,
	  N_("Export a file for each sheet if the exporter only supports one sheet at a time."), NULL },

	POPT_AUTOHELP
	POPT_TABLEEND
};

typedef GList *(*get_them_f)(void);
typedef gchar const *(*get_desc_f)(void *);

static void
list_them (get_them_f get_them,
	   get_desc_f get_his_id,
	   get_desc_f get_his_description)
{
	GList *ptr;
	unsigned tmp, len = 0;

	for (ptr = (*get_them) (); ptr ; ptr = ptr->next) {
		tmp = strlen ((*get_his_id) (ptr->data));
		if (len < tmp)
			len = tmp;
	}

	fputs ("ID", stderr);
	for (tmp = 2 ; tmp++ < len ;)
		fputc (' ', stderr);
	fputs ("  Description\n", stderr);
	for (ptr = (*get_them) (); ptr ; ptr = ptr->next) {
		tmp = strlen ((*get_his_id) (ptr->data));
		fputs ((*get_his_id) (ptr->data), stderr);
		while (tmp++ < len)
			fputc (' ', stderr);
		fprintf (stderr, " | %s\n",
			(*get_his_description) (ptr->data));
	}
}

static int
convert (char const **args, GOCmdContext *cc)
{
	int res = 0;
	GnmFileSaver *fs = NULL;
	GnmFileOpener *fo = NULL;
	char *outfile = go_shell_arg_to_uri (args[1]);

	if (ssconvert_export_id != NULL) {
		fs = gnm_file_saver_for_id (ssconvert_export_id);
		if (fs == NULL) {
			res = 1;
			fprintf (stderr, _("Unknown exporter '%s'.\n"
				 "Try --list-exporters to see a list of possibilities.\n"),
				 ssconvert_export_id);
		} else if (outfile == NULL &&
			   gnm_file_saver_get_extension	(fs) != NULL) {
			char *basename = g_path_get_basename (args[0]);
			if (basename != NULL) {
				char *t = strrchr (basename, '.');
				if (t != NULL) {
					GString *res = g_string_new (NULL);
					t = strrchr (args[0], '.');
					g_string_append_len (res, args[0], t - args[0] + 1);
					g_string_append (res, 
						gnm_file_saver_get_extension (fs));
					outfile = g_string_free (res, FALSE);
				}
				g_free (basename);
			}
		}
	} else {
		if (outfile != NULL) {
			fs = gnm_file_saver_for_file_name (outfile);
			if (fs == NULL) {
				res = 2;
				fprintf (stderr, _("Unable to guess exporter to use for '%s'.\n"
					 "Try --list-exporters to see a list of possibilities.\n"),
					 outfile);
			}
		}
	}
	if (outfile == NULL)
		fprintf (stderr, _("An output file name or an explicit export type is required.\n"
			 "Try --list-exporters to see a list of possibilities.\n"));
	
	if (ssconvert_import_id != NULL) {
		fo = gnm_file_opener_for_id (ssconvert_import_id);
		if (fo == NULL) {
			res = 1;
			fprintf (stderr, _("Unknown importer '%s'.\n"
				 "Try --list-importers to see a list of possibilities.\n"),
				 ssconvert_import_id);
		} 
	}

	if (fs != NULL) {
		IOContext *io_context = gnumeric_io_context_new (cc);
		char *uri = go_shell_arg_to_uri (args[0]);
		WorkbookView *wbv = wb_view_new_from_uri (uri, fo,
			io_context, ssconvert_import_encoding);
		g_free (uri);
		if (gnm_file_saver_get_save_scope (fs) !=
		    FILE_SAVE_WORKBOOK) {
			if (ssconvert_one_file_per_sheet) {
				g_warning ("TODO");
			} else
				fprintf (stderr, _("Selected exporter (%s) does not support saving multiple sheets in one file.\n"
						   "Only the current sheet will be saved."),
					 gnm_file_saver_get_id (fs));
		}
		res = !wb_view_save_as (wbv, fs, outfile, cc);
		g_object_unref (wb_view_workbook (wbv));
		g_object_unref (io_context);
	}
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

#ifdef G_OS_WIN32
	ssconvert_popt_options[2].arg = &gnumeric_lib_dir;
	ssconvert_popt_options[3].arg = &gnumeric_data_dir;
#endif

	ctx = poptGetContext (NULL, argc, argv, ssconvert_popt_options, 0);
	while (poptGetNextOpt (ctx) > 0)
		;

	if (ssconvert_show_version) {
		printf (_("ssconvert version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, gnumeric_data_dir, gnumeric_lib_dir);
		poptFreeContext (ctx);
		return 0;
	}

	gnm_common_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	plugin_db_activate_plugin_list (
		plugins_get_available_plugins (), &plugin_errs);

	if (ssconvert_list_exporters)
		list_them (&get_file_savers,
			(get_desc_f) &gnm_file_saver_get_id,
			(get_desc_f) &gnm_file_saver_get_description);
	else if (ssconvert_list_importers)
		list_them (&get_file_openers,
			(get_desc_f) &gnm_file_opener_get_id,
			(get_desc_f) &gnm_file_opener_get_description);
	else {
		char const **args = poptGetArgs (ctx);
		if (args && args[0])
			res = convert (args, cc);
		else {
			poptPrintUsage(ctx, stderr, 0);
			res = 1;
		}
	}

	poptFreeContext (ctx);
	g_object_unref (cc);
	gnm_shutdown ();

	return res;
}
