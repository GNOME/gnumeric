/*
 * dialog-search-replace.c:
 *   Dialog for entering a search-and-replace query.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include <glib.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "workbook-control-gui.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "application.h"
#include "expr.h"
#include "search.h"

typedef struct {
	Workbook *wb;
	WorkbookControlGUI  *wbcg;
	GtkWidget *dialog;
} SearchReplaceInfo;


/*
 * Actual implementation of the sheet order dialog
 */
static void
dialog_sheet_order_impl (WorkbookControlGUI *wbcg, GladeXML *gui)
{
	SearchReplaceInfo sri;
	int bval;

	sri.wbcg = wbcg;
	sri.wb  = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	sri.dialog     = glade_xml_get_widget (gui, "dialog");

#if 0
	gnome_dialog_set_default (GNOME_DIALOG (sri.dialog), BUTTON_CLOSE);
#endif
	gtk_window_set_policy (GTK_WINDOW (sri.dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (GNOME_DIALOG (sri.dialog)->vbox);

	bval = gnumeric_dialog_run (sri.wbcg, GNOME_DIALOG (sri.dialog));

  	/* If the user canceled we have already returned */
	if (bval != -1)
		gnome_dialog_close (GNOME_DIALOG (sri.dialog));
}

/*
 * Dialog
 */
SearchReplace *
dialog_search_replace (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	SearchReplace *sr;

	g_return_val_if_fail (wbcg != NULL, NULL);

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return NULL;

	dialog_sheet_order_impl (wbcg, gui);
	gtk_object_unref (GTK_OBJECT (gui));

	sr = search_replace_new ();
	/* FIXME */
	return sr;
}
