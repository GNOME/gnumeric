/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "stf.h"
#include "plugin.h"
#include "format.h"
#include "formats.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "workbook.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "sheet-object.h"
#include "number-match.h"
#include "main.h"
#include "expr-name.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"
#include "auto-format.h"
#include "style.h"
#include "style-color.h"
#include "str.h"
#include "dependent.h"
#include "sheet-autofill.h"
#include "xml-io.h"
#include "cell.h"
#include "value.h"
#include "expr.h"
#include "rendered-value.h"
#include "gnumeric-gconf.h"
#include "auto-correct.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <gtk/gtkmain.h>
#include <locale.h>
#ifdef WITH_BONOBO
#include "bonobo-io.h"
/* DO NOT include embeddable-grid.h.  It causes odd depends in the non-bonobo
 * case */
extern gboolean EmbeddableGridFactory_init (void);
#endif
#include <gal/widgets/e-cursors.h>
#include <glade/glade.h>

#include <libgnome/gnome-config.h>

#ifdef USE_WM_ICONS
#include <libgnomeui/gnome-window-icon.h>
#endif

#ifdef WITH_BONOBO
#include <bonobo.h>
#endif
#include <libgnome/gnome-i18n.h>

/* The debugging level */
int gnumeric_debugging = 0;
int dependency_debugging = 0;
int expression_sharing_debugging = 0;
int immediate_exit_flag = 0;
int print_debugging = 0;
gboolean initial_workbook_open_complete = FALSE;

char *x_geometry;


/* Actions common to application and component init
   - to do before arg parsing */
void
init_init (char const* gnumeric_binary)
{
	g_set_prgname (gnumeric_binary);

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Force all of the locale segments to update from the environment.
	 * Unless we do this they will default to C
	 */
	setlocale (LC_ALL, "");
}

static void
handle_paint_events (void)
{
	/* FIXME: we need to mask input events correctly here */
	/* Show something coherent */
	while (gtk_events_pending () && !initial_workbook_open_complete)
		gtk_main_iteration_do (FALSE);
}


static void
warn_about_ancient_gnumerics (const char *binary, WorkbookControl *wbc)
{
	struct stat buf;
	time_t now = time (NULL);
	int days = 180;

	if (binary &&
	    stat (binary, &buf) != -1 &&
	    buf.st_mtime != -1 &&
	    now - buf.st_mtime > days * 24 * 60 * 60) {
		handle_paint_events ();

		gnumeric_error_system (COMMAND_CONTEXT (wbc),
				       _("Thank you for using Gnumeric!\n"
					 "\n"
					 "The version of Gnumeric you are using is quite old\n"
					 "by now.  It is likely that many bugs have been fixed\n"
					 "and that new features have been added in the meantime.\n"
					 "\n"
					 "Please consider upgrading before reporting any bugs.\n"
					 "Consult http://www.gnumeric.org/ for details.\n"
					 "\n"
					 "-- The Gnumeric Team."));
	}
}



#if 0
static void
gnumeric_check_for_components (void)
{
	OAF_ServerInfoList *result;

	result = oaf_query ("repo_ids.has('IDL::1.0')", NULL, NULL);

	g_return_if_fail (result != NULL);

	CORBA_free (info_list);
}
#endif

/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
void
gnm_common_init ()
{	
#ifdef USE_WM_ICONS
	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-gnumeric.png");
#endif

	application_init ();
	value_init ();
	expr_init ();
	cell_init ();
	dependent_types_init ();
	string_init ();
	rendered_value_init ();
	style_init ();
	gnumeric_color_init ();
	format_color_init ();
	format_match_init ();
	e_cursors_init ();
	auto_format_init ();
	functions_init ();
	expr_name_init ();
	print_init ();
	autofill_init ();
	sheet_object_register ();
	autocorrect_init ();

	/* The statically linked in file formats */
	xml_init ();
	stf_init ();
#ifdef WITH_BONOBO
#ifdef GNOME2_CONVERSION_COMPLETE
	gnumeric_bonobo_io_init ();
#endif
#endif

	global_gnome_font_init ();
	glade_gnome_init ();
}

void
gnm_application_init (poptContext *ctx)
{
	char const **startup_files;
	gboolean opened_workbook = FALSE;
	WorkbookControl *wbc;

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (*ctx);
	else
		startup_files = NULL;

#ifdef WITH_BONOBO
	bonobo_activate ();
#endif
 	wbc = workbook_control_gui_new (NULL, NULL);
 	plugins_init (COMMAND_CONTEXT (wbc));
	if (startup_files) {
		int i;
		for (i = 0; startup_files [i]  && !initial_workbook_open_complete ; i++) {
 			if (wb_view_open (wb_control_view (wbc), wbc,
					  startup_files[i], TRUE))
  				opened_workbook = TRUE;

			handle_paint_events ();
		}
	}

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete && !immediate_exit_flag) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			GConfClient *client  = application_get_gconf_client ();
			GError *err = NULL;
			gint n_of_sheets;
			Sheet *sheet = NULL;
			
			n_of_sheets = gconf_client_get_int (client, 
							    GNUMERIC_GCONF_WORKBOOK_NSHEETS, 
							    &err);
			if (err || n_of_sheets < 1)
				n_of_sheets = 1;
			
			while (n_of_sheets--)
				sheet = workbook_sheet_add (wb_control_workbook (wbc),
						    sheet, FALSE);
			handle_paint_events ();
		}

		warn_about_ancient_gnumerics (g_get_prgname(), wbc);

		gtk_main ();
	}
}

int
gnm_dump_func_defs (char const* filename)
{
	int retval;
	CommandContextStderr *ccs = command_context_stderr_new ();
	
	plugins_init (COMMAND_CONTEXT (ccs));
	if ((retval = command_context_stderr_get_status (ccs)) == 0)
		function_dump_defs (filename);
	
	return retval;
}

void
gnm_shutdown ()
{
	application_release_pref_dialog ();

	autocorrect_shutdown ();
	plugins_shutdown ();
	print_shutdown ();
	auto_format_shutdown ();
	e_cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	gnumeric_color_shutdown ();
	style_shutdown ();
	rendered_value_shutdown ();
	string_shutdown ();
	dependent_types_shutdown ();
	cell_shutdown ();
	expr_shutdown ();
	value_shutdown ();

	global_gnome_font_shutdown ();

	gnome_config_drop_all ();
	application_release_gconf_client ();
}
