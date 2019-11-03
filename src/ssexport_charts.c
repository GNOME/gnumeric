/*
 * ssexport_charts.c: A wrapper application to export charts as SVGs/etc.
 *
 * Based on:
 *
 * ssconvert.c .
 *
 * Author:
 *   Jon Kåre Hellan <hellan@acm.org>
 *   Morten Welinder <terra@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Shlomi Fish ( https://www.shlomifish.org/ )
 *
 * Copyright (C) 2002-2003 Jody Goldberg
 * Copyright (C) 2006-2018 Morten Welinder (terra@gnome.org)
 * Shlomi Fish puts his changes under https://creativecommons.org/choose/zero/ .
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <position.h>
#include <parse-util.h>
#include <application.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <workbook-control.h>
#include <sheet.h>
#include <dependent.h>
#include <expr-name.h>
#include <libgnumeric.h>
#include <gutils.h>
#include <value.h>
#include <ranges.h>
#include <commands.h>
#include <gnumeric-paths.h>
#include <gnm-plugin.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <workbook-view.h>
#include <gnumeric-conf.h>
#include <gui-clipboard.h>
#include <tools/analysis-tools.h>
#include <dialogs/dialogs.h>
#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>
#include <sheet-object-graph.h>
#include <string.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

// Sheets that an exporter should export.
#define SHEET_SELECTION_KEY "sheet-selection"

// Sheets user has specified as export options
#define SSCONVERT_SHEET_SET_KEY "ssconvert-sheets"

static gboolean show_version = FALSE;
static gboolean is_verbose = FALSE;
static gboolean list_exporters = FALSE;
static gboolean list_importers = FALSE;
static char *use_clipboard = NULL;
static char *ssconvert_range = NULL;
static char *import_encoding = NULL;
static char *ssconvert_import_id = NULL;
char svgstring[4] = "svg";
static char *ssconvert_export_id = svgstring;
static char *ssconvert_export_options = NULL;
static char **ssconvert_tool_test = NULL;

static const GOptionEntry ssconvert_options [] = {
	{
		"version", 0,
		0, G_OPTION_ARG_NONE, &show_version,
		N_("Display program version"),
		NULL
	},

	{
		"verbose", 'v',
		0, G_OPTION_ARG_NONE, &is_verbose,
		N_("Be somewhat more verbose during conversion"),
		NULL
	},

	/* ---------------------------------------- */

	{
		"import-encoding", 'E',
		0, G_OPTION_ARG_STRING, &import_encoding,
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
		0, G_OPTION_ARG_NONE, &list_importers,
		N_("List the available importers"),
		NULL
	},

	/* ---------------------------------------- */

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
		0, G_OPTION_ARG_NONE, &list_exporters,
		N_("List the available exporters"),
		NULL
	},

	/* ---------------------------------------- */

	// For now these are for INTERNAL GNUMERIC USE ONLY.  They are used
	// by the test suite.
	{
		"clipboard", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &use_clipboard,
		N_("Output via the clipboard"),
		NULL
	},

	{
		"export-range", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &ssconvert_range,
		N_("The range to export"),
		NULL
	},

	{
		"tool-test", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING_ARRAY, &ssconvert_tool_test,
		N_("Tool test specs"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

static GnmRangeRef const *
setup_range (GObject *obj, const char *key, Workbook *wb, const char *rtxt)
{
	GnmParsePos pp;
	const char *end;
	GnmRangeRef rr, *rrc;

	pp.wb = wb;
	pp.sheet = workbook_sheet_by_index (wb, 0);
	pp.eval.col = 0;
	pp.eval.row = 0;

	end = rangeref_parse (&rr, rtxt, &pp, gnm_conventions_default);
	if (!end || end == rtxt || *end != 0) {
		g_printerr ("Invalid range specified.\n");
		exit (1);
	}

	rrc = g_memdup (&rr, sizeof (rr));
	g_object_set_data_full (obj, key, rrc, g_free);

	return rrc;
}

struct cb_handle_export_options {
	GOFileSaver *fs;
	Workbook const *wb;
};

static gboolean
cb_handle_export_options (const char *key, const char *value,
			  GError **err, gpointer user_)
{
	struct cb_handle_export_options *user = user_;
	return gnm_file_saver_common_export_option (user->fs, user->wb,
						    key, value, err);
}

static int
handle_export_options (GOFileSaver *fs, Workbook *wb)
{
	GError *err = NULL;
	gboolean fail;
	guint sig;

	if (!ssconvert_export_options)
		return 0;

	sig = g_signal_lookup ("set-export-options", G_TYPE_FROM_INSTANCE (fs));
	if (g_signal_handler_find (fs, G_SIGNAL_MATCH_ID,
				   sig, 0, NULL, NULL, NULL))
		fail = go_file_saver_set_export_options
			(fs, GO_DOC (wb),
			 ssconvert_export_options,
			 &err);
	else {
		struct cb_handle_export_options data;
		data.fs = fs;
		data.wb = wb;
		fail = go_parse_key_value (ssconvert_export_options, &err,
					   cb_handle_export_options, &data);
	}

	if (fail) {
		g_printerr ("ssconvert: %s\n", err
			    ? err->message
			    : _("Cannot parse export options."));
		return 1;
	}

	return 0;
}

typedef gchar const *(*get_desc_f)(const void *);

static int
by_his_id (gconstpointer a, gconstpointer b, gpointer user)
{
	get_desc_f get_his_id = user;
	return strcmp (get_his_id (a), get_his_id (b));
}

static void
list_them (GList *them,
	   get_desc_f get_his_id,
	   get_desc_f get_his_description)
{
	GList *them_copy = g_list_copy (them);
	GList *ptr;
	guint len = 0;
	gboolean interactive;

	them_copy = g_list_sort_with_data (them_copy, by_his_id, get_his_id);

	for (ptr = them_copy; ptr; ptr = ptr->next) {
		GObject *obj = ptr->data;
		char const *id;

		g_object_get (obj, "interactive-only", &interactive, NULL);
		if (interactive)
			continue;

		id = get_his_id (obj);
		if (!id) id = "";
		len = MAX (len, strlen (id));
	}

	g_printerr ("%-*s | %s\n", len,
		    /* Translate these? */
		    _("ID"),
		    _("Description"));
	for (ptr = them_copy; ptr ; ptr = ptr->next) {
		GObject *obj = ptr->data;
		char const *id;

		g_object_get (obj, "interactive-only", &interactive, NULL);
		if (interactive)
			continue;

		id = get_his_id (obj);
		if (!id) id = "";
		g_printerr ("%-*s | %s\n", len,
			    id,
			    (*get_his_description) (ptr->data));
	}

	g_list_free (them_copy);
}

static char *
resolve_template (const char *template, Sheet *sheet, unsigned n)
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
				g_string_append_printf (s, "%u", n);
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

static int
do_split_save (WorkbookView *wbv,
	       const char *outarg, GOCmdContext *cc)
{
	Workbook *wb = wb_view_get_workbook (wbv);
    unsigned graph_idx = 0;
    double resolution = 100.0;
	char *template;
	GPtrArray *sheets;
	unsigned ui;
	int res = 0;
	GPtrArray *sheet_sel =
		g_object_get_data (G_OBJECT (wb), SHEET_SELECTION_KEY);
	const gboolean fs_sheet_selection = FALSE;
    const GOImageFormat format = go_image_get_format_from_name (ssconvert_export_id);
	template = strchr (outarg, '%')
		? g_strdup (outarg)
		: g_strconcat (outarg, ".%n", NULL);

	sheets = g_object_get_data (G_OBJECT (wb),
				    SSCONVERT_SHEET_SET_KEY);
	if (sheets)
		g_ptr_array_ref (sheets);
	else {
		int i;
		sheets = g_ptr_array_new ();
		for (i = 0; i < workbook_sheet_count (wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			g_ptr_array_add (sheets, sheet);
		}
	}

	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		int oldn = sheet->index_in_wb;

		g_ptr_array_set_size (sheet_sel, 0);
		g_ptr_array_add (sheet_sel, sheet);

		if (!fs_sheet_selection) {
			/*
			 * HACK: (bug 694408).
			 *
			 * We don't have a good way of specifying the
			 * sheet.  Move it to the front and select
			 * it.  That will at least make cvs and txt
			 * exporters reliably find it.
			 */
			workbook_sheet_move (sheet, -oldn);
			wb_view_sheet_focus (wbv, sheet);
		}

        {
            char *tmpfile;
            GSList *l, *graphs = sheet_objects_get (sheet, NULL, GNM_SO_GRAPH_TYPE);
            for (l = graphs; l; l = l->next) {
                GsfOutput *dst;
                SheetObject *sog = l->data;
                GogGraph * graph;
                if (!GNM_IS_SO_GRAPH(sog)) {
                    continue;
                }
                graph = sheet_object_graph_get_gog (sog);
                tmpfile =	resolve_template (template, sheet, graph_idx);
                ++graph_idx;
                dst = go_file_create (tmpfile, NULL);
                g_assert(dst);
                if (!gog_graph_export_image (graph, format, dst, resolution, resolution)) {
                    res = 1;
                }
                gsf_output_close (dst);
                g_object_unref (dst);
                g_free (tmpfile);
            }
            g_slist_free (graphs);
        }
        if (!fs_sheet_selection)
            workbook_sheet_move (sheet, +oldn);
		if (res)
			break;
	}

	g_free (template);
	g_ptr_array_unref (sheets);

	return res;
}

static int
convert (char const *inarg, char const *outarg, GOCmdContext *cc)
{
	int res = 0;
	GOFileOpener *fo = NULL;
	char *infile = go_shell_arg_to_uri (inarg);
	char *out_dirname = outarg ? go_shell_arg_to_uri (outarg) : NULL;
	WorkbookView *wbv;
	GOIOContext *io_context = NULL;
	Workbook *wb = NULL;
	GPtrArray *sheet_sel = NULL;
	GnmRangeRef const *range = NULL;

	if (ssconvert_export_id != NULL) {
	} else {
		if (out_dirname != NULL) {
#if 0
            gchar * dn = go_filename_from_uri(out_dirname);
            if (g_mkdir_with_parents(dn, 0775)) {
				res = 2;
				g_printerr (_("Unable to mkdir '%s'.\n"
					      "Try --list-exporters to see a list of possibilities.\n"),
					    out_dirname);
                g_free(dn);
				goto out;
            }
            g_free(dn);
#endif
		}
	}

	if (out_dirname == NULL) {
		g_printerr (_("An output file name or an explicit export type is required.\n"
			      "Try --list-exporters to see a list of possibilities.\n"));
		res = 1;
		goto out;
	}

	io_context = go_io_context_new (cc);
    wbv = workbook_view_new_from_uri (infile, fo,
        io_context,
        import_encoding);

	if (go_io_error_occurred (io_context)) {
		go_io_error_display (io_context);
		res = 1;
		goto out;
	} else if (wbv == NULL) {
		g_printerr (_("Loading %s failed\n"), infile);
		res = 1;
		goto out;
	}

	wb = wb_view_get_workbook (wbv);

	res = handle_export_options (NULL, wb);
	if (res)
		goto out;

	gnm_app_recalc ();

	if (ssconvert_range)
		range = setup_range (G_OBJECT (wb),
				     "ssconvert-range",
				     wb,
				     ssconvert_range);

    {
		Sheet *def_sheet = NULL;

		if (range && range->a.sheet)
			def_sheet = range->a.sheet;
		else if (range)
			def_sheet = wb_view_cur_sheet (wbv);

		sheet_sel = g_ptr_array_new ();
		if (def_sheet)
			g_ptr_array_add (sheet_sel, def_sheet);
		g_object_set_data (G_OBJECT (wb),
				   SHEET_SELECTION_KEY, sheet_sel);
	}

    res = do_split_save (wbv, outarg, cc);

	if (sheet_sel) {
		g_object_set_data (G_OBJECT (wb),
				   SHEET_SELECTION_KEY, NULL);
		g_ptr_array_free (sheet_sel, TRUE);
	}

 out:
	if (wb)
		g_object_unref (wb);
	if (io_context)
		g_object_unref (io_context);
	g_free (infile);
	g_free (out_dirname);

	return res;
}

static int
clipboard_export (const char *inarg, char const *outarg, GOCmdContext *cc)
{
	GOFileOpener *fo = NULL;
	GOIOContext *io_context = NULL;
	WorkbookView *wbv;
	Workbook *wb = NULL;
	char *infile = go_shell_arg_to_uri (inarg);
	char *outfile = go_shell_arg_to_uri (outarg);
	int res = 0;
	GnmRangeRef const *range;
	GnmRange r;
	WorkbookControl *wbc = NULL;
	GBytes *data = NULL;
	GsfOutput *dst;

	io_context = go_io_context_new (cc);
	wbv = workbook_view_new_from_uri (infile, fo,
					  io_context,
					  import_encoding);

	if (go_io_error_occurred (io_context)) {
		go_io_error_display (io_context);
		res = 1;
		goto out;
	} else if (wbv == NULL) {
		g_printerr (_("Loading %s failed\n"), infile);
		res = 1;
		goto out;
	}

	wb = wb_view_get_workbook (wbv);

	range = setup_range (G_OBJECT (wb),
			     "ssconvert-range",
			     wb,
			     ssconvert_range);
	range_init_rangeref (&r, range);
	if (range->a.sheet)
		wb_view_sheet_focus (wbv, range->a.sheet);

	gnm_app_clipboard_cut_copy (wbc, FALSE,
				    wb_view_cur_sheet_view (wbv),
				    &r, FALSE);

	data = gui_clipboard_test (use_clipboard);
	if (!data) {
		g_printerr ("Failed to get clipboard data.\n");
		res = 1;
		goto out;
	}

	dst = go_file_create (outfile, NULL);
	if (!dst) {
		g_printerr ("Failed to write to %s\n", outfile);
		res = 1;
		goto out;
	}

	gsf_output_write (dst, g_bytes_get_size (data),
			  g_bytes_get_data (data, NULL));
	gsf_output_close (dst);
	g_object_unref (dst);

 out:
	if (data)
		g_bytes_unref (data);
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
	gboolean do_usage = FALSE;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	gnm_conf_set_persistence (FALSE);

	ocontext = g_option_context_new (_("INFILE [OUTFILE]"));
	g_option_context_add_main_entries (ocontext, ssconvert_options, GETTEXT_PACKAGE);
	g_option_context_add_group (ocontext, gnm_get_option_group ());
	/*
	 * The printing code uses gtk+ stuff, so we need to init gtk+.  We
	 * do that without opening any displays.
	 */
	g_option_context_add_group (ocontext, gtk_get_option_group (FALSE));
	g_option_context_parse (ocontext, &argc, (char ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (show_version) {
		g_print (_("ssconvert version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			 GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
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

	if (list_exporters)
		list_them (go_get_file_savers (),
			   (get_desc_f) &go_file_saver_get_id,
			   (get_desc_f) &go_file_saver_get_description);
	else if (list_importers)
		list_them (go_get_file_openers (),
			   (get_desc_f) &go_file_opener_get_id,
			   (get_desc_f) &go_file_opener_get_description);
	else if (use_clipboard) {
		if (argc == 3 && ssconvert_range)
			res = clipboard_export (argv[1], argv[2], cc);
		else
			do_usage = TRUE;
	} else if (argc == 2 || argc == 3) {
		res = convert (argv[1], argv[2], cc);
	} else
		do_usage = TRUE;

	if (do_usage) {
		g_printerr (_("Usage: %s [OPTION...] %s\n"),
			    g_get_prgname (),
			    _("INFILE [OUTFILE]"));
		res = 1;
	}

	go_component_set_default_command_context (NULL);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
