/*
 * dialog-simple-input.c: Implements various dialogs for simple
 * input values
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Almer S. Tigelaar (almer@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook.h>

#include <math.h>
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>
#include <sheet.h>

gboolean
dialog_choose_cols_vs_rows (WorkbookControlGUI *wbcg, const char *title,
			    gboolean *is_cols)
{
	GladeXML *gui;
	GtkDialog *dialog;
	GtkToggleButton *rows;
	gboolean res = FALSE;

	gui = gnumeric_glade_xml_new (wbcg, "colrow.glade");
        if (gui == NULL)
                return FALSE;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "dialog1"));
	if (dialog == NULL){
		g_warning ("Cannot find the `dialog1' widget in colrow.glade");
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	}
	
	rows = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "rows"));
	gtk_window_set_title (GTK_WINDOW (dialog), title);
		
	switch (gnumeric_dialog_run (wbcg, dialog)){
	case 1:		/* cancel */
		res = FALSE;
		break;
	case -1:		/* window manager close */
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	default:
		res = TRUE;
		*is_cols = !gtk_toggle_button_get_active (rows);
	}
	
	gtk_widget_destroy (GTK_WIDGET (dialog));
	gtk_object_destroy (GTK_OBJECT (gui));
	
	return res;
}

gboolean
dialog_get_number (WorkbookControlGUI *wbcg,
		   const char *glade_file, double *init_and_return)
{
	GladeXML *gui;
	GtkDialog *dialog;
	GtkWidget *entry;
	gboolean res = FALSE;

	gui = gnumeric_glade_xml_new (wbcg, glade_file);
        if (gui == NULL)
                return FALSE;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "dialog1"));
	if (dialog == NULL){
		g_warning ("Cannot find the `dialog1' widget in %s", glade_file);
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	}

	entry = glade_xml_get_widget (gui, "entry1");
	if (*init_and_return != 0.0){
		char buffer[80];

		sprintf (buffer, "%g", *init_and_return);

		gtk_entry_set_text (GTK_ENTRY (entry), buffer);
	}
	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_EDITABLE (entry));

	switch (gnumeric_dialog_run (wbcg, dialog)){
	case 1:			/* cancel */
		res = FALSE;
		break;
	case -1:		/* window manager close */
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;

	default:
		res = TRUE;
		*init_and_return = atof (gtk_entry_get_text (GTK_ENTRY (entry)));
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	gtk_object_destroy (GTK_OBJECT (gui));

	return res;
}

#define SHEET_NAME_CHANGE_DIALOG_KEY "sheet-name-change-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *entry;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GladeXML           *gui;
} SheetNameChangeState;

/**
 * sheet_name_destroy:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
sheet_name_destroy (GtkObject *w, SheetNameChangeState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_sheet_name_ok_clicked (GtkWidget *button, SheetNameChangeState *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *new_name;

	new_name = gtk_entry_get_text (GTK_ENTRY (state->entry));
	cmd_rename_sheet (wbc, sheet->name_unquoted, new_name);
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_sheet_name_cancel_clicked (GtkWidget *button, SheetNameChangeState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_sheet_name (WorkbookControlGUI *wbcg)
{
	SheetNameChangeState *state;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_NAME_CHANGE_DIALOG_KEY))
		return;

	state = g_new (SheetNameChangeState, 1);
	state->wbcg  = wbcg;

	state->gui = gnumeric_glade_xml_new (wbcg, "sheet-rename.glade");
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "dialog");
	if (state->dialog == NULL) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Sheet Name Change dialog."));
		g_free (state);
		return ;
	}

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	gtk_signal_connect (GTK_OBJECT (state->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_sheet_name_ok_clicked),
			    state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancelbutton");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_sheet_name_cancel_clicked),
			    state);

	state->entry = glade_xml_get_widget (state->gui, "entry");
	gtk_entry_set_text (GTK_ENTRY (state->entry), sheet->name_unquoted);
	gtk_editable_select_region (GTK_EDITABLE (state->entry), 0, -1);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog), GTK_EDITABLE (state->entry));

	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (sheet_name_destroy), state);	

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       SHEET_NAME_CHANGE_DIALOG_KEY);
	gtk_widget_show (state->dialog);
	gtk_widget_grab_focus (state->entry);
}



