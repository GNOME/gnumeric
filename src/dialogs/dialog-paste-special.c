/*
 * Clipboard.c: Implements the copy/paste operations
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "eval.h"
#include "dialogs.h"

static struct {
	char *name;
	int  disables_second_group;
} paste_types [] = {
	{ N_("All"),      0 },
	{ N_("Formulas"), 0 },
	{ N_("Values"),   0 },
	{ N_("Formats"),  1 },
	{ NULL, 0 }
};

static char *paste_ops [] = {
	N_("None"),
	N_("Add "),
	N_("Subtract"),
	N_("Multiply"),
	N_("Divide"),
	NULL
};

static void
disable_op_group (GtkWidget *widget, GtkWidget *group)
{
	gtk_widget_set_sensitive (group, FALSE);
}

static void
enable_op_group (GtkWidget *widget, GtkWidget *group)
{
	gtk_widget_set_sensitive (group, TRUE);
}

static void
transpose_cb (GtkToggleButton *widget, gboolean *transpose_b)
{
	*transpose_b = widget->active;
}

int
dialog_paste_special (Workbook *wb)
{
	GtkWidget *dialog, *hbox;
	GtkWidget *f1, *f1v, *f2, *f2v, *cb;
	GSList *group_type, *group_ops;
	int result, i;
	int v;
	gboolean do_transpose = FALSE;
	
	dialog = gnome_dialog_new (_("Paste special"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (wb->toplevel));
	f1  = gtk_frame_new (_("Paste type"));
	f1v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f1), f1v);

	f2  = gtk_frame_new (_("Operation"));
	f2v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f2), f2v);

	group_type = NULL;
	for (i = 0; paste_types [i].name; i++){
		GtkSignalFunc func;
		GtkWidget *r;

		if (paste_types [i].disables_second_group)
			func = GTK_SIGNAL_FUNC (disable_op_group);
		else
			func = GTK_SIGNAL_FUNC (enable_op_group);
		
		r = gtk_radio_button_new_with_label (group_type, _(paste_types [i].name));
		group_type = GTK_RADIO_BUTTON (r)->group;
		
		gtk_signal_connect (GTK_OBJECT (r), "toggled", func, f2);

		gtk_box_pack_start_defaults (GTK_BOX (f1v), r);
	}

	group_ops = NULL;
	for (i = 0; paste_ops [i]; i++){
		GtkWidget *r;
		
		r = gtk_radio_button_new_with_label (group_ops, _(paste_ops [i]));
		group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (f2v), r);
	}

	cb = gtk_check_button_new_with_label (_("Transpose"));
	gtk_signal_connect (
		GTK_OBJECT (cb), "toggled",
		GTK_SIGNAL_FUNC (transpose_cb), &do_transpose);

	hbox = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f1);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f2);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), cb);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	v = gnome_dialog_run (GNOME_DIALOG (dialog));

	/* If closed with the window manager, cancel */
	if (v == -1)
		return 0;

	result = 0;

	/* Fetch the results */
	if (v == 0){
		result = 0;
		i = gtk_radio_group_get_selected (group_type);
		
		switch (i){
		case 0: /* all */
			result = PASTE_ALL_TYPES;
			break;
			
		case 1: /* formulas */
			result = PASTE_FORMULAS;
			break;
			
		case 2: /* values */
			result = PASTE_VALUES;
			break;
			
		case 3: /* formats */
			result = PASTE_FORMATS;
			break;
		}
		
		/* If it was not just formats, check operation */
		if (i != 3){
			i = gtk_radio_group_get_selected (group_ops);
			switch (i){
			case 1:		/* Add */
				result |= PASTE_OPER_ADD;
				break;
				
			case 2:
				result |= PASTE_OPER_SUB;
				break;
				
			case 3:
				result |= PASTE_OPER_MULT;
				break;
				
			case 4:
				result |= PASTE_OPER_DIV;
				break;
			}
		}
		if (do_transpose)
			result |= PASTE_TRANSPOSE;
	}
	gtk_object_destroy (GTK_OBJECT (dialog));

	return result;
}
