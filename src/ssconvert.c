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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include <src/gnumeric-paths.h>
#include <src/plugin.h>
#include <src/command-context-stderr.h>
#include <src/io-context.h>
#include <src/workbook-view.h>

#include <libgnome/gnome-program.h>
#include <libgnome/gnome-init.h>
#include <string.h>

char const *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char const *gnumeric_data_dir = GNUMERIC_DATADIR;
static gboolean ssconvert_show_version = FALSE;
static gboolean ssconvert_list_exporters = FALSE;
static char const *ssconvert_import_encoding = NULL;
static char const *ssconvert_export_id = NULL;

#warning remove from POTFILES.skip when 1.3 branches
const struct poptOption
gnumeric_popt_options[] = {
	{ "version", 'v', POPT_ARG_NONE, &ssconvert_show_version, 0,
	  N_("Display Gnumeric's version"), NULL  },

	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },

	{ "import-encoding", 'E', POPT_ARG_STRING, &ssconvert_import_encoding, 0,
	  N_("Optionally specify an encoding for imported content"), N_("ENCODING")  },
	{ "export-type", 'T', POPT_ARG_STRING, &ssconvert_export_id, 0,
	  N_("Optionally specify which exporter to use"), "ID"  },
	{ "list-exporters", '\0', POPT_ARG_NONE, &ssconvert_list_exporters, 0,
	  N_("List the available exporters"), NULL },

	{ NULL, '\0', 0, NULL, 0 }
};

int
main (int argc, char *argv [])
{
	GnomeProgram	*program;
	GnmCmdContext	*cc;
	ErrorInfo	*plugin_errs;
	int		 res = 0;
	poptContext ctx;

	init_init (argv[0]);
	program = gnome_program_init (PACKAGE, VERSION,
		LIBGNOME_MODULE, argc, argv,
		GNOME_PARAM_APP_PREFIX,		GNUMERIC_PREFIX,
		GNOME_PARAM_APP_SYSCONFDIR,	GNUMERIC_SYSCONFDIR,
		GNOME_PARAM_APP_DATADIR,	GNUMERIC_DATADIR,
		GNOME_PARAM_APP_LIBDIR,		GNUMERIC_LIBDIR,
		GNOME_PARAM_POPT_TABLE,		gnumeric_popt_options,
		NULL);

	g_object_get (G_OBJECT (program),
		GNOME_PARAM_POPT_CONTEXT,	&ctx,
		NULL);

	if (ssconvert_show_version) {
		printf (_("ssconvert version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return 0;
	}

	gnm_common_init ();

	cc = cmd_context_stderr_new ();
	plugins_init (cc);
	if (ctx) {
		char const **args = poptGetArgs (ctx);
		plugin_db_activate_plugin_list (
			plugins_get_available_plugins (), &plugin_errs);

		if (ssconvert_list_exporters) {
			GList *ptr;
			unsigned tmp, len = 0;

			for (ptr = get_file_savers (); ptr ; ptr = ptr->next) {
				tmp = strlen (gnm_file_saver_get_id (ptr->data));
				if (len < tmp)
					len = tmp;
			}

			fputs ("ID", stderr);
			for (tmp = 2 ; tmp++ < len ;)
				fputc (' ', stderr);
			fputs ("  Description\n", stderr);
			for (ptr = get_file_savers (); ptr ; ptr = ptr->next) {
				tmp = strlen (gnm_file_saver_get_id (ptr->data));
				fputs (gnm_file_saver_get_id (ptr->data), stderr);
				while (tmp++ < len)
					fputc (' ', stderr);
				fprintf (stderr, " | %s\n",
					gnm_file_saver_get_description (ptr->data));
			}
		} else if (args && args[0]) {
			GnmFileSaver *fs = NULL;
			char *outfile = (char *)args[1];

			if (ssconvert_export_id != NULL) {
				fs = gnm_file_saver_for_id (ssconvert_export_id);
				if (fs == NULL) {
					res = 1;
					fprintf (stderr, "Unknown exporter '%s'.\n"
						 "Try --list-exports to see a list of possibilities.\n",
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
						fprintf (stderr, "Unable to guess exporter to use for '%s'.\n"
							 "Try --list-exports to see a list of possibilities.\n",
							 outfile);
					}
				}
			}
			if (outfile == NULL)
				fprintf (stderr, "An output file name or an explicit export type is required.\n"
					 "Try --list-exports to see a list of possibilities.\n");

			if (fs != NULL) {
				IOContext    *io_context = gnumeric_io_context_new (cc);
				WorkbookView *wbv = wb_view_new_from_file (args[0], NULL,
					io_context, ssconvert_import_encoding);
				if (gnm_file_saver_get_save_scope (fs) != FILE_SAVE_WORKBOOK)
					fprintf (stderr, "Selected exporter (%s) does not support saving multiple sheets in one file.\n"
						 "Only the first will be saved.",
						 gnm_file_saver_get_id (fs));
				res = !wb_view_save_as (wbv, fs, outfile, cc);
				g_object_unref (wb_view_workbook (wbv));
				g_object_unref (io_context);
			}
		} else
			poptPrintUsage(ctx, stderr, 0);
	}
	g_object_unref (cc);
	gnm_shutdown ();

	return res;
}
