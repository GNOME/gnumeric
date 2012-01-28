#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "command-context.h"
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "wbc-gtk.h"
#include "workbook-view.h"
#include <goffice/goffice.h>
#include "selection.h"
#include "sheet-view.h"
#include "commands.h"
#include "workbook.h"
#include "sheet-control.h"
#include "gnumeric-paths.h"
#include "gnm-plugin.h"
#include "wbc-gtk-impl.h"

#include <gtk/gtk.h>

#define TEST_STEPS	50
#define STEP_SIZE	40

static gboolean
cb_kill_wbcg (WBCGtk *wbcg)
{
	gboolean still_open = wbc_gtk_close (wbcg);
	g_assert (!still_open);
	return FALSE;
}

static gboolean
cb_exercise_pango (gpointer data)
{
	static int state = 0;

	WorkbookControl *wbc = data;
	SheetView	*sv  = wb_control_cur_sheet_view (wbc);

	if (state == 0) {
		sv_selection_reset (sv);
		sv_selection_add_full (sv, 0, 0, 0, 0, 40, STEP_SIZE*TEST_STEPS,
				       GNM_SELECTION_MODE_ADD);
		cmd_area_set_text (wbc, sv, "=rand()", NULL);
	} else if (state < TEST_STEPS) {
		SHEET_VIEW_FOREACH_CONTROL(wb_control_cur_sheet_view (wbc),
			sc, sc_set_top_left (sc, 0, state*STEP_SIZE););
	} else if (state < (TEST_STEPS*2)) {
		SHEET_VIEW_FOREACH_CONTROL(wb_control_cur_sheet_view (wbc),
			sc, sc_set_top_left (sc, 0, (state-TEST_STEPS)*STEP_SIZE););
	} else if (state == (TEST_STEPS*2)) {
		go_doc_set_dirty (wb_control_get_doc (wbc), FALSE);
		g_object_unref (wb_control_get_workbook (wbc));
	}

	if (state++ < TEST_STEPS*2)
	    return 1;
	g_idle_add ((GSourceFunc)cb_kill_wbcg, wbc);
	return 0;
}

int
main (int argc, char const **argv)
{
	GOCmdContext *cc;
	WBCGtk *wbc;
	GOIOContext *ioc;

	argv = gnm_pre_parse_init (argc, argv);
	gtk_init (&argc, (char ***)&argv);
	gnm_init ();

	cc  = cmd_context_stderr_new ();
	ioc = go_io_context_new (cc);
	g_object_unref (cc);

	gnm_plugins_init (GO_CMD_CONTEXT (ioc));
	g_object_unref (ioc);

	initial_workbook_open_complete = TRUE; /* make the last unref exit */

	wbc = wbc_gtk_new (NULL, workbook_new_with_sheets (1), NULL, NULL);

	g_idle_add (cb_exercise_pango, wbc);

	gtk_main ();

	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}
