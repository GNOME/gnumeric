/* vim: set sw=8:
 * $Id$
 */

/*
 * dialog-function-wizard.c:  The formula guru
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "cell.h"
#include "expr.h"
#include "func.h"

typedef struct {
	GtkWidget	   *dialog;
	GtkBox		   *dialog_box;

	int		    max_arg;
	Workbook	   *wb;
	FunctionDefinition *fd;
	TokenizedHelp	   *help_tokens;
	GPtrArray	   *args;
} FomulaGuruState;

typedef struct {
	FomulaGuruState	 *state;
	GtkEntry *entry;

	gchar   *name;
	gchar    type;
	gboolean is_optional;
	int       content_length;
	int       index;
} ArgumentState;

static void
formula_guru_set_expr (FomulaGuruState *state, int index)
{
	GString *str;
	int pos = 0, i;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->fd != NULL);
	g_return_if_fail (state->args != NULL);

	str = g_string_new ("="); /* FIXME use prefix string */
	g_string_append (str, function_def_get_name (state->fd));
	g_string_append_c (str, '(');

	for (i = 0; i < state->args->len; i++) {
		ArgumentState *as = g_ptr_array_index (state->args, i);
		gchar *val = gtk_entry_get_text (as->entry);

		if (!as->is_optional || i <= state->max_arg || i <= index || strlen (val)) {
			if (i > 0)
				g_string_append_c (str, ',');
			if (i == index)
				pos = str->len +
					gtk_editable_get_position (GTK_EDITABLE (as->entry));

			g_string_append (str, val);
		}
	}
	g_string_append_c (str, ')'); /* FIXME use suffix string */
	gtk_entry_set_text (workbook_get_entry (state->wb), str->str);
	gtk_entry_set_position (workbook_get_entry (state->wb), pos);

	g_string_free (str, TRUE);
}

static gboolean
cb_formula_guru_entry_focus_in (GtkWidget *ignored0, GdkEventFocus *ignored1, ArgumentState *as)
{
	workbook_set_entry (as->state->wb, as->entry);
	formula_guru_set_expr (as->state, as->index);
	return FALSE;
}

static void
cb_formula_guru_entry_changed (GtkEditable *editable, ArgumentState *as)
{
	as->content_length = strlen (gtk_entry_get_text (as->entry));

	if (as->content_length) {
		if (as->state->max_arg < as->index)
			as->state->max_arg = as->index;
	} else {
		if (as->state->max_arg == as->index) {
			int i = as->index;
			while (--i > 0 && as->content_length > 0)
				;
			as->state->max_arg = i;
		}
	}

	formula_guru_set_expr (as->state, as->index);
}

static gboolean
cb_formula_guru_destroy (GtkObject *w, FomulaGuruState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	workbook_edit_detach_guru (state->wb);

	if (state->args != NULL) {
		int i;

		for (i = state->args->len; i-- > 0 ;){
			ArgumentState *as = g_ptr_array_index (state->args, i);
			g_free (as->name);
			g_free (as);
		}
		g_ptr_array_free (state->args, FALSE);
		state->args = NULL;
	}

	if (state->help_tokens != NULL) {
		tokenized_help_destroy (state->help_tokens);
		state->help_tokens = NULL;
	}

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	workbook_finish_editing (state->wb, FALSE);

	g_free (state);
	return FALSE;
}

static void
cb_formula_guru_clicked (GnomeDialog *d, gint arg, FomulaGuruState *state)
{
	/* Help */
	if (arg == 0)
		return;

	/* Accept for OK, reject for cancel */
	workbook_finish_editing (state->wb, arg == 1);
	gnome_dialog_close (d);
}

static void
formula_guru_init_arg (GtkTable *table, int row, ArgumentState *as)
{
	gchar *txt = NULL, *label;

	g_return_if_fail (as);
	g_return_if_fail (table);

	switch (as->type){
	case 's': txt = _("String"); break;
	case 'f': txt = _("Number"); break;
	case 'b': txt = _("Boolean"); break;
	case 'r': txt = _("Range"); break;
	case 'a': txt = _("Array"); break;
	case 'A': txt = _("Range/Array"); break;
	case '?': txt = _("Any"); break;
	default:  txt = _("Unknown"); break;
	}
	gtk_table_attach_defaults (table, gtk_label_new (as->name),
				   0, 1, row, row+1);

	as->entry = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach_defaults (table, GTK_WIDGET(as->entry),
				   1, 2, row, row+1);

	if (as->is_optional)
		label = g_strconcat ("(", txt, ")", NULL);
	else
		label = g_strconcat ("=", txt, NULL);

	gtk_table_attach_defaults (table, gtk_label_new (label),
				   3, 4, row, row+1);
#if 0
	GtkButton *button;
	GtkWidget *pix;
	gtk_table_attach_defaults (table, GTK_WIDGET(button),
				   2, 3, row, row+1);
	button = GTK_BUTTON(gtk_button_new());
	pix = gnome_stock_pixmap_widget_new (as->state->wb->toplevel,
					     GNOME_STOCK_PIXMAP_BOOK_GREEN);
	gtk_container_add (GTK_CONTAINER (button), pix);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
#endif
	/* FIXME : Do I really need focus-in ?  is there something less draconian */
	gtk_signal_connect (GTK_OBJECT (as->entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_formula_guru_entry_focus_in), as);
	gtk_signal_connect (GTK_OBJECT (as->entry), "changed",
			    GTK_SIGNAL_FUNC (cb_formula_guru_entry_changed), as);

	if (row == 0)
		gtk_widget_activate (GTK_WIDGET (as->entry));

	g_free (label);
}

static void
formula_guru_init_args (FomulaGuruState *state)
{
	gchar *copy_args;
	const gchar *syntax;
	gchar *ptr, *start = NULL;
	int i;
	int arg_max, arg_min;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->fd != NULL);
	g_return_if_fail (state->help_tokens != NULL);

	state->args = g_ptr_array_new ();

	/* Var arg function */
	function_def_count_args (state->fd, &arg_min, &arg_max);
	if (arg_max == G_MAXINT) {
		/* Display 2 argument for vararg functions by default */
		for (i = 0; i < 2; i++) {
			ArgumentState *as;

			as = g_new (ArgumentState, 1);
			as->name = g_strdup ("Value");
			as->is_optional = TRUE;  /* All arguments are optional. */
			as->type = '?';
			as->index = i;
			as->content_length = 0;
			as->entry = NULL;
			as->state = state;
			g_ptr_array_add (state->args, as);
		}
		return;
	}

	syntax = tokenized_help_find (state->help_tokens, "SYNTAX");
	if (!syntax) {
		g_ptr_array_free (state->args, FALSE);
		state->args = NULL;
		return;
	}
	ptr = copy_args = g_strdup (syntax);
	i   = 0;
	while (*ptr) {
		if (*ptr == '(' && !start)
			start = ptr + 1;
		if (*ptr == '[' || *ptr == ']') {
			*ptr = '\0';
			if (start == ptr)
				start++;
			ptr++;
			continue;
		}
		if (*ptr == ',' || *ptr == ')') {
			if (ptr > start) {
				ArgumentState *as;
				as = g_new (ArgumentState, 1);
				as->name = g_strndup (start, (int)(ptr - start));
				as->is_optional = (i >= arg_min);
				as->type = function_def_get_arg_type (state->fd, i);
				as->index = i;
				as->content_length = 0;
				as->entry = NULL;
				as->state = state;
				g_ptr_array_add (state->args, as);
				i++;
			}
			start = ptr + 1;
		}
		ptr++;
	}

	if (i != arg_max)
		g_warning ("Function mis-match : %s has %d named arguments but was declared to have %d",
			   function_def_get_name (state->fd), i, arg_max);

	g_free (copy_args);
}

static void
formula_guru_init (FomulaGuruState *state)
{
	GtkBox *vbox;
	GtkTable *table;
	GtkWidget *dialog;
	TokenizedHelp *help_tokens;
	int lp;

	state->args	= NULL;
	state->max_arg	= 0;
	state->help_tokens = tokenized_help_new (state->fd);
	formula_guru_init_args (state);

	dialog = gnome_dialog_new (_("Formula Guru"),
				   GNOME_STOCK_BUTTON_HELP,
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);

	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_formula_guru_destroy),
			    state);
	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (cb_formula_guru_clicked),
			    state);

	state->dialog = dialog;
	state->dialog_box = GTK_BOX(GNOME_DIALOG (dialog)->vbox);

	help_tokens = tokenized_help_new (state->fd);

	vbox = GTK_BOX (gtk_vbox_new (0, 0));

	{ /* Syntax label */
		GtkWidget *label = gtk_label_new (tokenized_help_find (help_tokens, "SYNTAX"));
		gtk_box_pack_start (vbox, label, TRUE, TRUE, 0);
	}

	{ /* Description */
		GtkLabel *label;
		const char *txt = tokenized_help_find (help_tokens, "DESCRIPTION");
		label = GTK_LABEL (gtk_label_new (txt));
		gtk_label_set_line_wrap (label, TRUE);
		gtk_box_pack_start (vbox, GTK_WIDGET(label),
				    TRUE, TRUE, 0);
	}

	tokenized_help_destroy (help_tokens);

	table = GTK_TABLE (gtk_table_new (3, state->args->len, FALSE));
	for (lp = 0; lp < state->args->len; lp++)
		formula_guru_init_arg (table, lp, g_ptr_array_index (state->args, lp));

	gtk_box_pack_start (GTK_BOX(vbox),	GTK_WIDGET (table), TRUE, TRUE, 0);
	gtk_box_pack_start (state->dialog_box,	GTK_WIDGET (vbox),  FALSE, FALSE, 0);
	gtk_widget_show_all (GTK_WIDGET(state->dialog_box));

	workbook_edit_attach_guru (state->wb, state->dialog);

	formula_guru_set_expr (state, 0);
}

/**
 * dialog_formula_guru
 * @wb : The workbook to use as a parent window.
 *
 * Pop up a function selector then a formula guru.
 */
void
dialog_formula_guru (Workbook *wb)
{
	Sheet *sheet;
	GtkEntry *entry;
	gchar *txt;
	FomulaGuruState *state;
	FunctionDefinition *fd;

	g_return_if_fail (wb != NULL);

	entry = workbook_get_entry (wb);
	txt   = gtk_entry_get_text (entry);
	sheet = wb->current_sheet;

	/* If the current cell is not an expression clear cell, and start one */
	if (gnumeric_char_start_expr_p (txt) == NULL) {
		workbook_start_editing_at_cursor (wb, TRUE, TRUE);
		gtk_entry_set_text (entry, "=");
	} else
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);

	fd = dialog_function_select (wb);
	if (fd == NULL) {
		workbook_finish_editing (wb, FALSE);
		return;
	}

	state = g_new(FomulaGuruState, 1);
	state->wb	= wb;
	state->fd	= fd;
	formula_guru_init (state);

	gnumeric_dialog_show (wb->toplevel, GNOME_DIALOG (state->dialog), FALSE, TRUE);
}
