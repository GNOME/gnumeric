/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 * dialog-sheet-order.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 */

#include <config.h>
#include <glib.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "workbook.h"
#include "sheet.h"
#include "application.h"
#include "expr.h"

#define GLADE_FILE "sheet-order.glade"

typedef struct {
	Workbook  *wb;
	GtkWidget *dialog;
	GtkWidget *clist;
	gint       current_row;
} SheetManager;

enum { BUTTON_UP = 0, BUTTON_DOWN, BUTTON_DELETE, BUTTON_CLOSE };

/*
 * Add one sheet's name to the clist
 */
static void
add_to_sheet_clist (Sheet *sheet, GtkWidget *clist)
{
	gchar *data [1];
	gint   row;

	data [0] = sheet->name;
	row = gtk_clist_append (GTK_CLIST (clist), data);
	gtk_clist_set_row_data (GTK_CLIST (clist), row, sheet);
}

/*
 * Add all of the sheets to the clist
 */
static void
populate_sheet_clist (SheetManager *sm)
{
        GtkCList *clist = GTK_CLIST (sm->clist);
	GList *sheets   = workbook_sheets (sm->wb);
	GtkNotebook *nb = GTK_NOTEBOOK (sm->wb->notebook);
	gint row;

	gtk_clist_freeze (clist);
	gtk_clist_clear  (clist);
	g_list_foreach   (sheets, (GFunc) add_to_sheet_clist, clist);
	gtk_clist_thaw   (clist);

	if ((row = gtk_notebook_get_current_page (nb)) >= 0)
		gtk_clist_select_row (clist, row, 0);
}

/*
 * Handle key-press events
 * Currently we only handle "ESC" - should destroy the widget
 * But we may expand in the future to have keybindings
 */
static gint
key_event_cb (GtkWidget *dialog, GdkEventKey *event)
{
	if (event->keyval == GDK_Escape) {
		gtk_widget_destroy (dialog);
		return 1;
	}
	return 0;
}

/*
 * Refreshes the buttons on a row (un)selection
 * And moves the representative page/sheet in the notebook
 * To the foreground
 */
static void
row_cb (GtkWidget *w, gint row, gint col,
	GdkEvent *event, SheetManager *sm)
{
	GtkCList *clist = GTK_CLIST (w);
	gint numrows = clist->rows;
	gboolean can_go = FALSE;
	
	if (numrows) {
		sm->current_row = row;

		can_go = (row != 0); /* top row test */
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_UP, can_go);

		can_go = !(row >= (numrows - 1)); /* bottom row test */
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_DOWN, can_go);

		/* don't delete the last remaining sheet */
		can_go = (numrows > 1);
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_DELETE, can_go);

		/* Display/focus on the selected sheet underneath us */
		gtk_notebook_set_page (GTK_NOTEBOOK (sm->wb->notebook), row);

	} else {
		sm->current_row = -1;
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_UP, FALSE);
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_DOWN, FALSE);
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm->dialog),
					    BUTTON_DELETE, FALSE);
	}

}

/*
 * User wanted to delete this sheet from the workbook
 */
static void
delete_cb (SheetManager *sm)
{
	GtkCList *clist = GTK_CLIST (sm->clist);
	GList *selection = GTK_CLIST (clist)->selection;
	gint row = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
	gint numrows = GTK_CLIST (sm->clist)->rows;

	Sheet *sheet = gtk_clist_get_row_data (clist, row);
	GtkWidget *popup;
	gchar *message;
	gint response = 1;

	/* Don't delete anything if number of rows <= 1 */
	if (numrows <= 1)
		return;

	message = g_strdup_printf (
		_("Are you sure you want to remove the sheet called `%s'?"),
		sheet->name);

	popup = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);

	response = gnumeric_dialog_run (sheet->workbook, GNOME_DIALOG (popup));
	if (response != 0)
		return;

	workbook_delete_sheet (sheet);
	populate_sheet_clist (sm);
}

/*
 * Actual implementation of the re-ordering sheets
 * Both in the GtkCList and in the GtkNotebook underneath
 */
static void
move_cb (SheetManager *sm, gint direction)
{
	gint numrows = GTK_CLIST (sm->clist)->rows;

	if (numrows && sm->current_row >= 0) {
		GList *selection = GTK_CLIST (sm->clist)->selection;
		Sheet *sheet = gtk_clist_get_row_data (GTK_CLIST (sm->clist), sm->current_row);
		gint source = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
		gint dest = source + direction;

		gtk_clist_freeze (GTK_CLIST (sm->clist));
		gtk_clist_row_move (GTK_CLIST (sm->clist), source, dest);
		gtk_clist_thaw (GTK_CLIST (sm->clist));

		workbook_move_sheet (sheet, direction);
		workbook_focus_sheet (sheet);

		/* this is a little hack-ish, but we need to refresh the buttons */
		row_cb (sm->clist, dest, 0, NULL, sm);
	}
}

/*
 * User wants to move the sheet up
 */
static void
up_cb (SheetManager *sm)
{
	move_cb (sm, -1); /* c-array style : move -1 == left == up */
}

/*
 * User wants to move the sheet down
 */
static void
down_cb (SheetManager *sm)
{
	move_cb (sm, 1); /* c-array style : move 1 == right == down */
}

/*
 * Actual implementation of the sheet order dialog
 */
static void
dialog_sheet_order_impl (Workbook *wb, GladeXML *gui)
{
	SheetManager sm;
	int bval;

	sm.wb = wb;
	sm.dialog = glade_xml_get_widget (gui, "dialog");
	sm.clist  = glade_xml_get_widget (gui, "sheet_name_clist");
	sm.current_row = -1;

	gtk_signal_connect (GTK_OBJECT (sm.dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (key_event_cb), NULL);

	gtk_signal_connect (GTK_OBJECT (sm.clist), "select_row",
			    GTK_SIGNAL_FUNC (row_cb), &sm);
	
	gtk_signal_connect (GTK_OBJECT (sm.clist), "unselect_row",
			    GTK_SIGNAL_FUNC (row_cb), &sm);

	populate_sheet_clist (&sm);

	if (GTK_CLIST (sm.clist)->rows > 0) {
		gtk_widget_grab_focus (sm.clist);
	} else {
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm.dialog),
					    BUTTON_UP, FALSE);
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm.dialog),
					    BUTTON_DOWN, FALSE);
		gnome_dialog_set_sensitive (GNOME_DIALOG (sm.dialog),
					    BUTTON_DELETE, FALSE);
	}

	gnome_dialog_set_default (GNOME_DIALOG (sm.dialog), BUTTON_CLOSE);
	gtk_window_set_policy (GTK_WINDOW (sm.dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (GNOME_DIALOG (sm.dialog)->vbox);

	do {
		bval = gnumeric_dialog_run (sm.wb, GNOME_DIALOG (sm.dialog));
		switch (bval) {

		case BUTTON_UP:
			up_cb (&sm);
			break;

		case BUTTON_DOWN:
			down_cb (&sm);
			break;

		case BUTTON_DELETE:
			delete_cb (&sm);
			break;

		case -1: /* close window */
		        return;
		  
		case BUTTON_CLOSE:
		default:
			break;
		}
	} while (bval != BUTTON_CLOSE);

	/* If the user canceled we have already returned */
	gnome_dialog_close (GNOME_DIALOG (sm.dialog));
}

/*
 * Dialog
 */
void
dialog_sheet_order (Workbook *wb)
{
	GladeXML *gui;

	g_return_if_fail (wb != NULL);

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	dialog_sheet_order_impl (wb, gui);
	gtk_object_unref (GTK_OBJECT (gui));
}
