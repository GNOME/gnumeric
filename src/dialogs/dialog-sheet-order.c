/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 * dialog-sheet-order.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook-control-gui.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <application.h>
#include <expr.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

typedef struct {
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkWidget *dialog;
	GtkWidget *clist;
	GtkWidget *up_btn;
	GtkWidget *down_btn;
	GtkWidget *delete_btn;
	GtkWidget *close_btn;
	gint       current_row;
} SheetManager;

/*
 * Add one sheet's name to the clist
 */
static void
add_to_sheet_clist (Sheet *sheet, GtkWidget *clist)
{
	gchar *data[1];
	gint   row;

	data[0] = sheet->name_unquoted;
	row = gtk_clist_append (GTK_CLIST (clist), data);
	gtk_clist_set_row_data (GTK_CLIST (clist), row, sheet);
}

/*
 * Add all of the sheets to the clist
 */
static void
populate_sheet_clist (SheetManager *sm)
{
	Sheet *cur_sheet= wb_control_cur_sheet (WORKBOOK_CONTROL (sm->wbcg));
        GtkCList *clist = GTK_CLIST (sm->clist);
	GList *sheets   = workbook_sheets (sm->wb), *ptr;
	gint row = 0;

	gtk_clist_freeze (clist);
	gtk_clist_clear  (clist);
	g_list_foreach   (sheets, (GFunc) add_to_sheet_clist, clist);

	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next, row++)
		if (ptr->data == cur_sheet)
			gtk_clist_select_row (clist, row, 0);

	g_list_free (sheets);
	gtk_clist_thaw (clist);
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
		Sheet *sheet = gtk_clist_get_row_data (GTK_CLIST (clist), row);

		sm->current_row = row;

		can_go = (row != 0); /* top row test */
		gtk_widget_set_sensitive (sm->up_btn, can_go);

		can_go = !(row >= (numrows - 1)); /* bottom row test */
		gtk_widget_set_sensitive (sm->down_btn, can_go);

		/* don't delete the last remaining sheet */
		can_go = (numrows > 1);
		gtk_widget_set_sensitive (sm->delete_btn, can_go);

		/* Display/focus on the selected sheet underneath us */
		wb_control_sheet_focus (WORKBOOK_CONTROL (sm->wbcg), sheet);
	} else {
		sm->current_row = -1;
		gtk_widget_set_sensitive (sm->up_btn, FALSE);
		gtk_widget_set_sensitive (sm->down_btn, FALSE);
		gtk_widget_set_sensitive (sm->delete_btn, FALSE);
	}
}

/*
 * User wanted to delete this sheet from the workbook
 */
static void
delete_clicked_cb (GtkWidget *button, SheetManager *sm)
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
		sheet->name_unquoted);

	popup = gtk_message_dialog_new (wbcg_toplevel (sm->wbcg),
		  GTK_DIALOG_DESTROY_WITH_PARENT,
		  GTK_MESSAGE_QUESTION,
		  GTK_BUTTONS_YES_NO,
		  message, NULL);
	g_free (message);

	response = gnumeric_dialog_run (sm->wbcg, GTK_DIALOG (popup));
	if (response != 0)
		return;

	workbook_sheet_delete (sheet);
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
		WorkbookView *view = wb_control_view (
			WORKBOOK_CONTROL (sm->wbcg));

		gtk_clist_freeze (GTK_CLIST (sm->clist));
		gtk_clist_row_move (GTK_CLIST (sm->clist), source, dest);
		gtk_clist_thaw (GTK_CLIST (sm->clist));

		workbook_sheet_move (sheet, direction);
		wb_view_sheet_focus (view, sheet);

		/* this is a little hack-ish, but we need to refresh the buttons */
		row_cb (sm->clist, dest, 0, NULL, sm);
	}
}

/*
 * User wants to move the sheet up
 */
static void
up_clicked_cb (GtkWidget *button, SheetManager *sm)
{
	move_cb (sm, -1); /* c-array style : move -1 == left == up */
}

/*
 * User wants to move the sheet down
 */
static void
down_clicked_cb (GtkWidget *button, SheetManager *sm)
{
	move_cb (sm, 1); /* c-array style : move 1 == right == down */
}

static void
close_clicked_cb (GtkWidget *button, SheetManager *sm)
{
	gnome_dialog_close (GNOME_DIALOG (sm->dialog));
}

/*
 * Actual implementation of the sheet order dialog
 */
static void
dialog_sheet_order_impl (WorkbookControlGUI *wbcg, GladeXML *gui)
{
	SheetManager sm;
	int bval;

	sm.wbcg = wbcg;
	sm.wb  = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	sm.dialog     = glade_xml_get_widget (gui, "dialog");
	sm.clist      = glade_xml_get_widget (gui, "sheet_clist");
	sm.up_btn     = glade_xml_get_widget (gui, "up_btn");
	sm.down_btn   = glade_xml_get_widget (gui, "down_btn");
	sm.delete_btn = glade_xml_get_widget (gui, "delete_btn");
	sm.close_btn  = glade_xml_get_widget (gui, "close_btn");

	sm.current_row = -1;

	gtk_clist_column_titles_passive (GTK_CLIST (sm.clist));

	gtk_signal_connect (GTK_OBJECT (sm.dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (key_event_cb), NULL);

	gtk_signal_connect (GTK_OBJECT (sm.clist), "select_row",
			    GTK_SIGNAL_FUNC (row_cb), &sm);

	gtk_signal_connect (GTK_OBJECT (sm.clist), "unselect_row",
			    GTK_SIGNAL_FUNC (row_cb), &sm);

	gtk_signal_connect (GTK_OBJECT (sm.up_btn), "clicked",
 			    GTK_SIGNAL_FUNC (up_clicked_cb), &sm);

	gtk_signal_connect (GTK_OBJECT (sm.down_btn), "clicked",
			    GTK_SIGNAL_FUNC (down_clicked_cb), &sm);

	gtk_signal_connect (GTK_OBJECT (sm.delete_btn), "clicked",
			    GTK_SIGNAL_FUNC (delete_clicked_cb), &sm);

	gtk_signal_connect (GTK_OBJECT (sm.close_btn), "clicked",
			    GTK_SIGNAL_FUNC (close_clicked_cb), &sm);

	populate_sheet_clist (&sm);
	gnumeric_clist_make_selection_visible (GTK_CLIST (sm.clist));

	if (GTK_CLIST (sm.clist)->rows > 0) {
		gtk_widget_grab_focus (sm.clist);
	} else {
		gtk_widget_set_sensitive (sm.up_btn, FALSE);
		gtk_widget_set_sensitive (sm.down_btn, FALSE);
		gtk_widget_set_sensitive (sm.delete_btn, FALSE);
	}

	gtk_clist_column_titles_passive (GTK_CLIST (sm.clist));
#if 0
	gnome_dialog_set_default (GNOME_DIALOG (sm.dialog), BUTTON_CLOSE);
#endif
	gtk_window_set_policy (GTK_WINDOW (sm.dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (GNOME_DIALOG (sm.dialog)->vbox);

	bval = gnumeric_dialog_run (sm.wbcg, GNOME_DIALOG (sm.dialog));

  	/* If the user canceled we have already returned */
	if (bval != -1)
		gnome_dialog_close (GNOME_DIALOG (sm.dialog));
}

/*
 * Dialog
 */
void
dialog_sheet_order (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "sheet-order.glade");
        if (gui == NULL)
                return;

	dialog_sheet_order_impl (wbcg, gui);
	g_object_unref (G_OBJECT (gui));
}
