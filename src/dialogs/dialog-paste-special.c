/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-paste-special.c: The dialog for selecting non-standard
 *    behaviors when pasting.
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook-control-gui.h>
#include <gui-util.h>
#include <clipboard.h>
#include <dependent.h>

#define BUTTON_PASTE_LINK 0

static const struct {
	char const *name;
	gboolean allows_operations;
} paste_types [] = {
	{ N_("All"),      TRUE },
	{ N_("Content"),  TRUE },
	{ N_("As Value"), TRUE },
	{ N_("Formats"),  FALSE },
	{ NULL, FALSE }
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
	GtkWidget	*op_frame;
	GtkToggleButton *transpose;
	GtkToggleButton *skip_blanks;
	GSList *type_group, *op_group;
	int     type,	     op;
} PasteSpecialState;

/* The "Paste Link" button should be grayed-out, unless type "All" is
   selected, operation "None" is selected, and "Transpose" and "Skip
   Blanks" are not selected.  */
static void
paste_link_set_sensitive (__attribute__((unused)) GtkWidget *ignored,
			  PasteSpecialState *state)
{
	gboolean sensitive =
		(gtk_radio_group_get_selected (state->type_group) == 0) &&
		(gtk_radio_group_get_selected (state->op_group) == 0) &&
		!gtk_toggle_button_get_active (state->transpose) &&
		!gtk_toggle_button_get_active (state->skip_blanks);

	gtk_dialog_set_response_sensitive (state->dialog,
		BUTTON_PASTE_LINK, sensitive);
}

static void
cb_type_toggle (__attribute__((unused)) GtkWidget *ignored,
		PasteSpecialState *state)
{
	state->type = gtk_radio_group_get_selected (state->type_group);
	gtk_widget_set_sensitive (state->op_frame,
		paste_types [state->type].allows_operations);
	paste_link_set_sensitive (NULL, state);
}

static void
cb_op_toggle (__attribute__((unused)) GtkWidget *ignored,
	      PasteSpecialState *state)
{
	state->op = gtk_radio_group_get_selected (state->op_group);
	paste_link_set_sensitive (NULL, state);
}

static void
checkbutton_toggled (GtkWidget *widget, gboolean *flag)
{
        *flag = GTK_TOGGLE_BUTTON (widget)->active;
}

int
dialog_paste_special (WorkbookControlGUI *wbcg)
{
	GtkWidget *hbox, *vbox;
	GtkWidget *f1, *f1v, *op_box, *first_button = NULL;
	int result, i;
	int v;
	gboolean do_transpose = FALSE;
	gboolean do_skip_blanks = FALSE;
	PasteSpecialState state;
	GtkWidget *tmp;

	tmp = gtk_dialog_new_with_buttons (_("Paste special"),
		wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Paste Link"),	BUTTON_PASTE_LINK,
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);
	state.dialog = GTK_DIALOG (tmp);
	gtk_dialog_set_default_response (state.dialog, GTK_RESPONSE_CANCEL);

	f1  = gtk_frame_new (_("Paste type"));
	f1v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f1), f1v);

	state.op_frame  = gtk_frame_new (_("Operation"));
	op_box = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (state.op_frame), op_box);

	state.type = 0;
	state.type_group = NULL;
	for (i = 0; paste_types[i].name; i++){
		GtkWidget *r = gtk_radio_button_new_with_label (state.type_group,
			_(paste_types[i].name));
		state.type_group = GTK_RADIO_BUTTON (r)->group;
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (cb_type_toggle), &state);
		gtk_box_pack_start_defaults (GTK_BOX (f1v), r);
		if (i == 0)
			first_button = r;
	}

	state.op = 0;
	state.op_group = NULL;
	for (i = 0; paste_ops[i]; i++){
		GtkWidget *r = gtk_radio_button_new_with_label (state.op_group,
			_(paste_ops[i]));
		state.op_group = GTK_RADIO_BUTTON (r)->group;
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (cb_op_toggle), &state);
		gtk_box_pack_start_defaults (GTK_BOX (op_box), r);
	}

	vbox = gtk_vbox_new (TRUE, 0);

	state.transpose = GTK_TOGGLE_BUTTON (gtk_check_button_new_with_label (_("Transpose")));
	g_signal_connect (G_OBJECT (state.transpose),
		"toggled",
		G_CALLBACK (checkbutton_toggled), &do_transpose);
	g_signal_connect (G_OBJECT (state.transpose),
		"toggled",
		G_CALLBACK (paste_link_set_sensitive), &state);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), GTK_WIDGET (state.transpose));

	state.skip_blanks = GTK_TOGGLE_BUTTON (gtk_check_button_new_with_label (_("Skip Blanks")));
	g_signal_connect (G_OBJECT (state.skip_blanks),
		"toggled",
		G_CALLBACK (checkbutton_toggled), &do_skip_blanks);
	g_signal_connect (G_OBJECT (state.skip_blanks),
		"toggled",
		G_CALLBACK (paste_link_set_sensitive), &state);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), GTK_WIDGET (state.skip_blanks));

	hbox = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f1);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), state.op_frame);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), vbox);

	gtk_box_pack_start (GTK_BOX (state.dialog->vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);
	gtk_widget_grab_focus (first_button);

	/* Run the dialog */
	gtk_window_set_modal (GTK_WINDOW (state.dialog), TRUE);
	v = gnumeric_dialog_run (wbcg, state.dialog);

	/* If closed with the window manager, cancel */
	if (v == -1)
		return 0;

	result = 0;

	/* Fetch the results */
	if (v == GTK_RESPONSE_OK) {
		result = 0;

		switch (state.type) {
		case 0: result = PASTE_ALL_TYPES;	break;
		case 1: result = PASTE_CONTENT;		break;
		case 2: result = PASTE_AS_VALUES;	break;
		case 3: result = PASTE_FORMATS;		break;
		}

		/* If it was not just formats, check operation */
		if (result != PASTE_FORMATS) {
			switch (state.op) {
			case 1 : result |= PASTE_OPER_ADD;  break;
			case 2 : result |= PASTE_OPER_SUB;  break;
			case 3 : result |= PASTE_OPER_MULT; break;
			case 4 : result |= PASTE_OPER_DIV;  break;
			}
		}
		if (do_transpose)
			result |= PASTE_TRANSPOSE;
		if (do_skip_blanks)
			result |= PASTE_SKIP_BLANKS;
	} else if (v == BUTTON_PASTE_LINK)
		result = PASTE_LINK;
	return result;
}
