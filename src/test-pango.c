#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "command-context.h"
#include "io-context.h"
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "plugin.h"
#include "selection.h"
#include "sheet-view.h"
#include "commands.h"
#include "workbook.h"
#include "sheet-control.h"
#include "gnumeric-paths.h"

#include <gtk/gtkmain.h>

int gnumeric_no_splash = TRUE;

const struct poptOption
gnumeric_popt_options[] = {
	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },
	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },
	{ "geometry", 'g', POPT_ARG_STRING, &x_geometry, 0,
	  N_("Specify the size and location of the initial window"), N_("WIDTHxHEIGHT+XOFF+YOFF")
	},

	{ NULL, '\0', 0, NULL, 0 }
};

#define TEST_STEPS	50
#define STEP_SIZE	40

static gboolean
cb_exercise_pango (gpointer data)
{
	static int state = 0;

	WorkbookControl *wbc = data;
	SheetView	*sv  = wb_control_cur_sheet_view (wbc);

	if (state == 0) {
		sv_selection_reset (sv);
		sv_selection_add_range(sv, 0, 0, 0, 0, 40, STEP_SIZE*TEST_STEPS);
		cmd_area_set_text (wbc, sv, "=rand()", FALSE);
	} else if (state < TEST_STEPS) {
		SHEET_VIEW_FOREACH_CONTROL(wb_control_cur_sheet_view (wbc),
			sc, sc_set_top_left (sc, 0, state*STEP_SIZE););
	} else if (state < (TEST_STEPS*2)) {
		SHEET_VIEW_FOREACH_CONTROL(wb_control_cur_sheet_view (wbc),
			sc, sc_set_top_left (sc, 0, (state-TEST_STEPS)*STEP_SIZE););
	} else if (state == (TEST_STEPS*2)) {
		workbook_set_dirty (wb_control_workbook (wbc), FALSE);
		workbook_unref (wb_control_workbook (wbc));
	}

	return state++ < TEST_STEPS*2;
}

int
main (int argc, char *argv [])
{
	GnmCmdContext *cc;
	WorkbookControl *wbc;
	IOContext *ioc;
	poptContext ctx;

	gnm_pre_parse_init (argv[0]);

	ctx = gnumeric_arg_parse (argc, argv);

	cc  = cmd_context_stderr_new ();
	ioc = gnumeric_io_context_new (cc);
	g_object_unref (cc);

	/* TODO: Use the ioc. */
	gnm_common_init (FALSE);

 	plugins_init (GNM_CMD_CONTEXT (ioc));
	g_object_unref (ioc);

	initial_workbook_open_complete = TRUE; /* make the last unref exit */

	wbc = workbook_control_gui_new (NULL, workbook_new_with_sheets (1), NULL);

	g_idle_add (cb_exercise_pango, wbc);

	gtk_main ();

	gnm_shutdown ();

	return 0;
}
