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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <wbc-gtk.h>
#include <gui-util.h>
#include <clipboard.h>
#include <dependent.h>
#include <cmd-edit.h>

#include <gtk/gtktogglebutton.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>

typedef struct {
	WBCGtk *wbcg;
	GtkDialog	*dialog;
	GtkWidget	*op_frame;
	struct {
		GtkWidget *btn;
		gboolean   val;
	} transpose, skip_blanks;
	GSList		*type_group, *op_group;
	int		type, op;
} PasteSpecialState;

#define BUTTON_PASTE_LINK 0

#define GNM_PASTE_SPECIAL_KEY	"gnm-paste-special"

static struct {
	char const *name;
	gboolean allows_operations;
} const paste_types [] = {
	/* xgettext : The accelerators for All, Content, As Value, Formats,
	 *	Comments, None, Add, Subtract, Multiply, Divide, Transpose,
	 *	Skip Blanks, Paste Link, Cancel, Ok are all on the same page
	 *	try to keep them from conflicting */
	{ N_("_All"),      TRUE },
	{ N_("Cont_ent"),  TRUE },
	{ N_("As _Value"), TRUE },
	{ N_("_Formats"),  FALSE },
	{ N_("Co_mments"),  FALSE },
	{ NULL, FALSE }
};

static const char * const paste_ops[] = {
	N_("_None"),
	N_("A_dd"),
	N_("_Subtract"),
	N_("M_ultiply"),
	N_("D_ivide"),
	NULL
};

/* The "Paste Link" button should be grayed-out, unless type "All" is
   selected, operation "None" is selected, and "Transpose" and "Skip
   Blanks" are not selected.  */
static void
paste_link_set_sensitive (PasteSpecialState *state)
{
	gboolean sensitive =
		(gtk_radio_group_get_selected (state->type_group) == 0) &&
		(gtk_radio_group_get_selected (state->op_group) == 0) &&
		!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->transpose.btn)) &&
		!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->skip_blanks.btn));

	gtk_dialog_set_response_sensitive (state->dialog,
		BUTTON_PASTE_LINK, sensitive);
}

static void
cb_type_toggle (G_GNUC_UNUSED GtkWidget *ignored,
		PasteSpecialState *state)
{
	state->type = gtk_radio_group_get_selected (state->type_group);
	gtk_widget_set_sensitive (state->op_frame,
		paste_types [state->type].allows_operations);
	paste_link_set_sensitive (state);
}

static void
cb_op_toggle (G_GNUC_UNUSED GtkWidget *ignored,
	      PasteSpecialState *state)
{
	state->op = gtk_radio_group_get_selected (state->op_group);
	paste_link_set_sensitive (state);
}

static void
cb_transpose (GtkWidget *widget, PasteSpecialState *state)
{
	state->transpose.val = GTK_TOGGLE_BUTTON (widget)->active;
	paste_link_set_sensitive (state);
}
static void
cb_skip_blanks (GtkWidget *widget, PasteSpecialState *state)
{
	state->skip_blanks.val = GTK_TOGGLE_BUTTON (widget)->active;
	paste_link_set_sensitive (state);
}

static void
cb_paste_special_response (GtkWidget *dialog,
			   gint response_id, PasteSpecialState *state)
{
	int result = 0;
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
		switch (state->type) {
		case 0: result = PASTE_ALL_TYPES;	break;
		case 1: result = PASTE_CONTENTS;	break;
		case 2: result = PASTE_AS_VALUES;	break;
		case 3: result = PASTE_FORMATS;		break;
		case 4: result = PASTE_COMMENTS;	break;
		}

		if (paste_types [state->type].allows_operations) {
			switch (state->op) {
			case 1 : result |= PASTE_OPER_ADD;  break;
			case 2 : result |= PASTE_OPER_SUB;  break;
			case 3 : result |= PASTE_OPER_MULT; break;
			case 4 : result |= PASTE_OPER_DIV;  break;
			}
		}
		if (state->transpose.val)
			result |= PASTE_TRANSPOSE;
		if (state->skip_blanks.val)
			result |= PASTE_SKIP_BLANKS;
	} else if (response_id == BUTTON_PASTE_LINK)
		result = PASTE_LINK;

	if (result != 0) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
		SheetView *sv = wb_control_cur_sheet_view (wbc);
		cmd_paste_to_selection (wbc, sv, result);
	}
	gtk_object_destroy (GTK_OBJECT (dialog));
}

void
dialog_paste_special (WBCGtk *wbcg)
{
	PasteSpecialState *state;
	GtkWidget *tmp, *hbox, *vbox, *f1, *f1v, *op_box, *first_button = NULL;
	int i;

	if (gnumeric_dialog_raise_if_exists (wbcg, GNM_PASTE_SPECIAL_KEY))
		return;

	tmp = gtk_dialog_new_with_buttons (_("Paste Special"),
		wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Paste _Link"),	BUTTON_PASTE_LINK,
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);
	state = g_new0 (PasteSpecialState, 1);
	state->wbcg   = wbcg;
	state->dialog = GTK_DIALOG (tmp);
	gtk_dialog_set_default_response (state->dialog, GTK_RESPONSE_OK);

	f1  = gtk_frame_new (_("Paste type"));
	f1v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f1), f1v);

	state->op_frame  = gtk_frame_new (_("Operation"));
	op_box = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (state->op_frame), op_box);

	state->type = 0;
	state->type_group = NULL;
	for (i = 0; paste_types[i].name; i++){
		GtkWidget *r = gtk_radio_button_new_with_mnemonic (state->type_group,
			_(paste_types[i].name));
		state->type_group = GTK_RADIO_BUTTON (r)->group;
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (cb_type_toggle), state);
		gtk_box_pack_start_defaults (GTK_BOX (f1v), r);
		if (i == 0)
			first_button = r;
	}

	state->op = 0;
	state->op_group = NULL;
	for (i = 0; paste_ops[i]; i++){
		GtkWidget *r = gtk_radio_button_new_with_mnemonic (state->op_group,
			_(paste_ops[i]));
		state->op_group = GTK_RADIO_BUTTON (r)->group;
		g_signal_connect (G_OBJECT (r),
			"toggled",
			G_CALLBACK (cb_op_toggle), state);
		gtk_box_pack_start_defaults (GTK_BOX (op_box), r);
	}

	hbox = gtk_hbox_new (TRUE, 0);

	state->transpose.btn = gtk_check_button_new_with_mnemonic (_("_Transpose"));
	g_signal_connect (G_OBJECT (state->transpose.btn),
		"toggled",
		G_CALLBACK (cb_transpose), state);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), state->transpose.btn);

	state->skip_blanks.btn = gtk_check_button_new_with_mnemonic (_("Skip _Blanks"));
	g_signal_connect (G_OBJECT (state->skip_blanks.btn),
		"toggled",
		G_CALLBACK (cb_skip_blanks), state);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), state->skip_blanks.btn);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), f1);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), state->op_frame);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), hbox);

	gtk_box_pack_start (GTK_BOX (state->dialog->vbox), vbox, TRUE, TRUE, 0);
	gtk_widget_show_all (vbox);
	gtk_widget_grab_focus (first_button);

	/* a candidate for merging into attach guru */
	g_signal_connect (G_OBJECT (state->dialog), "response",
		G_CALLBACK (cb_paste_special_response), state);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) g_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
