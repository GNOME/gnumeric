/*
 * dialog-paste-special.c: The dialog for selecting non-standard
 *    behaviors when pasting.
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook-control-gui.h>
#include <gui-util.h>
#include <clipboard.h>
#include <eval.h>

#include <libgnome/gnome-i18n.h>

#define BUTTON_PASTE_LINK 0

static const struct {
	char *name;
	int  disables_second_group;
} paste_types[] = {
	{ N_("All"),      0 },
	{ N_("Content"),  0 },
	{ N_("As Value"), 0 },
	{ N_("Formats"),  1 },
	{ NULL, 0 }
};

static const char *paste_ops[] = {
	N_("None"),
	N_("Add "),
	N_("Subtract"),
	N_("Multiply"),
	N_("Divide"),
	NULL
};

typedef struct {
	GtkDialog       *dialog;
	GSList          *group_type;
	GSList          *group_ops;
	GtkToggleButton *transpose;
	GtkToggleButton *skip_blanks;
} PasteSpecialState;

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
checkbutton_toggled (GtkWidget *widget, gboolean *flag)
{
        *flag = GTK_TOGGLE_BUTTON (widget)->active;
}

static gboolean
dialog_destroy (GtkObject *w, PasteSpecialState *state)
{
	g_free (state);
	return FALSE;
}

/* The "Paste Link" button should be grayed-out, unless type "All" is
   selected, operation "None" is selected, and "Transpose" and "Skip
   Blanks" are not selected.  */
static void
paste_link_set_sensitive (GtkWidget *widget, PasteSpecialState *state)
{
	gboolean sensitive =
		(gtk_radio_group_get_selected (state->group_type) == 0) &&
		(gtk_radio_group_get_selected (state->group_ops) == 0) &&
		!state->transpose->active &&
		!state->skip_blanks->active;

	gtk_dialog_set_response_sensitive (state->dialog,
		BUTTON_PASTE_LINK, sensitive);
}

int
dialog_paste_special (WorkbookControlGUI *wbcg)
{
	GtkWidget *hbox, *vbox;
	GtkWidget *f1, *f1v, *f2, *f2v, *first_button = NULL;
	int result, i;
	int v;
	gboolean do_transpose = FALSE;
	gboolean do_skip_blanks = FALSE;
	PasteSpecialState *state;
	GtkWidget *tmp;

	state = g_new (PasteSpecialState, 1);

	tmp = gtk_dialog_new_with_buttons (_("Paste special"),
		wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Paste Link"),	BUTTON_PASTE_LINK,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		NULL);
	state->dialog = GTK_DIALOG (tmp);
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), state);
	gtk_dialog_set_default_response (state->dialog, GTK_RESPONSE_CANCEL);

	f1  = gtk_frame_new (_("Paste type"));
	f1v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f1), f1v);

	f2  = gtk_frame_new (_("Operation"));
	f2v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f2), f2v);

	state->group_type = NULL;
	for (i = 0; paste_types[i].name; i++){
		GCallback func;
		GtkWidget *r;

		if (paste_types[i].disables_second_group)
			func = G_CALLBACK (disable_op_group);
		else
			func = G_CALLBACK (enable_op_group);

		r = gtk_radio_button_new_with_label (state->group_type, _(paste_types[i].name));
		state->group_type = GTK_RADIO_BUTTON (r)->group;

		g_signal_connect (G_OBJECT (r), "toggled", func, f2);
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (paste_link_set_sensitive), state);

		gtk_box_pack_start_defaults (GTK_BOX (f1v), r);
		if (i == 0) first_button = r;
	}

	state->group_ops = NULL;
	for (i = 0; paste_ops[i]; i++){
		GtkWidget *r;

		r = gtk_radio_button_new_with_label (state->group_ops, _(paste_ops[i]));
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (paste_link_set_sensitive),
				    state);
		state->group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (f2v), r);
	}

	vbox = gtk_vbox_new (TRUE, 0);

	state->transpose = GTK_TOGGLE_BUTTON (gtk_check_button_new_with_label (_("Transpose")));
	g_signal_connect (G_OBJECT (state->transpose),
		"toggled",
		G_CALLBACK (checkbutton_toggled), &do_transpose);
	g_signal_connect (G_OBJECT (state->transpose),
		"toggled",
		G_CALLBACK (paste_link_set_sensitive), state);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), GTK_WIDGET (state->transpose));

	state->skip_blanks = GTK_TOGGLE_BUTTON (gtk_check_button_new_with_label (_("Skip Blanks")));
	g_signal_connect (G_OBJECT (state->skip_blanks),
		"toggled",
		G_CALLBACK (checkbutton_toggled), &do_skip_blanks);
	g_signal_connect (G_OBJECT (state->skip_blanks),
		"toggled",
		G_CALLBACK (paste_link_set_sensitive), state);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), GTK_WIDGET (state->skip_blanks));

	hbox = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f1);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f2);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), vbox);

	gtk_box_pack_start (GTK_BOX (state->dialog->vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);
	gtk_widget_grab_focus (first_button);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (state->dialog), TRUE);
	v = gnumeric_dialog_run (wbcg, state->dialog);

	/* If closed with the window manager, cancel */
	if (v == -1)
		return 0;

	result = 0;

	/* Fetch the results */
	if (v == GTK_RESPONSE_OK) {
		result = 0;
		i = gtk_radio_group_get_selected (state->group_type);

		switch (i){
		case 0: /* all */
			result = PASTE_ALL_TYPES;
			break;

		case 1: /* content */
			result = PASTE_CONTENT;
			break;

		case 2: /* as values */
			result = PASTE_AS_VALUES;
			break;

		case 3: /* formats */
			result = PASTE_FORMATS;
			break;
		}

		/* If it was not just formats, check operation */
		if (i != 3){
			i = gtk_radio_group_get_selected (state->group_ops);
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
		if (do_skip_blanks)
			result |= PASTE_SKIP_BLANKS;
	} else if (v == BUTTON_PASTE_LINK) {
		result = PASTE_LINK;
	}
	gtk_object_destroy (GTK_OBJECT (state->dialog));

	return result;
}
