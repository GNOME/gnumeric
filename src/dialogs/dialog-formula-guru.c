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

typedef struct _FomulaGuruState FomulaGuruState;
typedef struct
{
	FomulaGuruState	*state;
	GtkWidget	*name_label, *type_label;
	GtkEntry	*entry;

	gchar 	  *name;
	gchar	   type;
	gboolean   is_optional;
	int	   content_length;
	int	   index;
} ArgumentState;

struct _FomulaGuruState
{
	GladeXML  *gui;

	GtkWidget *dialog;
	GtkWidget *help_button;
	GtkWidget *rollup_button;
	GtkWidget *rolldown_button;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *rolled_box;
	GtkWidget *rolled_label;
	GtkWidget *rolled_entry;
	GtkWidget *unrolled_box;
	GtkWidget *arg_table;
	GtkWidget *arg_frame;
	GtkWidget *description;
	
	gboolean	    valid;
	gboolean	    is_rolled;
	gboolean	    var_args;
	ArgumentState	   *cur_arg;
	int		    max_arg; /* max arg # with a value */
	Workbook	   *wb;
	FunctionDefinition *fd;
	TokenizedHelp	   *help_tokens;
	GPtrArray	   *args;
};

static void formula_guru_arg_delete (FomulaGuruState *state, int i);
static void formula_guru_arg_new    (char * const name, char const type,
				     gboolean const is_optional,
				     FomulaGuruState *state);

static void
formula_guru_set_expr (FomulaGuruState *state, int index)
{
	GString *str;
	int pos = 0, i;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->fd != NULL);
	g_return_if_fail (state->args != NULL);

	/* Do not do anything until we are fully up and running */
	if (!state->valid)
		return;

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

static void
cb_formula_guru_rolled_entry_changed (GtkEditable *editable,
				      FomulaGuruState *state) 
{
	gtk_entry_set_text (state->cur_arg->entry,
		gtk_entry_get_text (GTK_ENTRY (state->rolled_entry)));
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

static void
formula_guru_set_rolled_state (FomulaGuruState *state, gboolean is_rolled)
{
	GtkEntry *new_entry;

	g_return_if_fail (state->cur_arg != NULL);

	state->is_rolled = is_rolled;
	if (is_rolled) {
		new_entry = GTK_ENTRY (state->rolled_entry);
		gtk_entry_set_text (new_entry,
			gtk_entry_get_text (state->cur_arg->entry));
		gtk_label_set_text (GTK_LABEL (state->rolled_label),
			state->cur_arg->name);

		gtk_signal_connect (GTK_OBJECT (new_entry), "changed",
				    GTK_SIGNAL_FUNC (cb_formula_guru_rolled_entry_changed),
				    state);

		gtk_widget_show	(state->rolled_box);
		gtk_widget_hide	(state->unrolled_box);
		gtk_widget_show (state->rolldown_button);
		gtk_widget_hide (state->rollup_button);
	} else {
		new_entry = state->cur_arg->entry;
		gtk_widget_hide	(state->rolled_box);
		gtk_widget_show	(state->unrolled_box);
		gtk_widget_hide (state->rolldown_button);
		gtk_widget_show (state->rollup_button);
	}
	workbook_set_entry (state->wb, new_entry);
}

static gboolean
cb_formula_guru_entry_focus_in (GtkWidget *ignored0, GdkEventFocus *ignored1, ArgumentState *as)
{
	FomulaGuruState *state = as->state;
	int i, lim;

	if (state->var_args) {
		i = state->args->len - 1;

		if (i == as->index) {
			formula_guru_arg_new (NULL, '?', TRUE, state);
			gtk_widget_show_all (state->arg_table);
			/* FIXME : scroll window to make visible if necessary */
		} else {
			lim = MAX (as->index+1, state->max_arg) +1;
			if (lim < 2) lim = 2;

			for (; i >= lim ; --i) {
				ArgumentState *tmp = g_ptr_array_index (state->args, i);
				gtk_object_destroy (GTK_OBJECT (tmp->name_label));
				gtk_object_destroy (GTK_OBJECT (tmp->entry));
				gtk_object_destroy (GTK_OBJECT (tmp->type_label));
				formula_guru_arg_delete (state, i);
			}

			g_ptr_array_set_size (state->args, lim);
		}
	}

	state->cur_arg = as;
	workbook_set_entry (state->wb, as->entry);
	formula_guru_set_expr (state, as->index);
	return FALSE;
}

static gboolean
cb_formula_guru_destroy (GtkObject *w, FomulaGuruState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	workbook_edit_detach_guru (state->wb);

	if (state->args != NULL) {
		int i;

		for (i = state->args->len; i-- > 0 ;)
			formula_guru_arg_delete (state, i);
		g_ptr_array_free (state->args, FALSE);
		state->args = NULL;
	}

	if (state->help_tokens != NULL) {
		tokenized_help_destroy (state->help_tokens);
		state->help_tokens = NULL;
	}

	if (state->gui != NULL) {
		gtk_object_unref (GTK_OBJECT (state->gui));
		state->gui = NULL;
	}

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	workbook_finish_editing (state->wb, FALSE);

	state->dialog = NULL;

	g_free (state);
	return FALSE;
}

static void
cb_formula_guru_clicked (GtkWidget *button, FomulaGuruState *state)
{
	if (state->dialog == NULL)
		return;

	if (button == state->help_button)
		return;

	if (button == state->rolldown_button) {
		formula_guru_set_rolled_state (state, FALSE);
		return;
	}
	if (button == state->rollup_button) {
		formula_guru_set_rolled_state (state, TRUE);
		return;
	}

	/* Accept for OK, reject for cancel */
	workbook_finish_editing (state->wb, button == state->ok_button);
}

static void
formula_guru_arg_delete (FomulaGuruState *state, int i)
{
	ArgumentState *as = g_ptr_array_index (state->args, i);
	g_free (as->name);
	g_free (as);
}

static void
formula_guru_arg_new (char * const name,
		      char const type,
		      gboolean const is_optional,
		      FomulaGuruState *state)
{
	ArgumentState *as;
	gchar *txt = NULL, *label;
	int row;

	as = g_new (ArgumentState, 1);
	as->index = row = state->args->len;
	as->name = name ? name : g_strdup_printf (_("Value%d"), row+1);
	as->is_optional = is_optional;
	as->type = type;
	as->content_length = 0;
	as->state = state;

	switch (as->type){
	case 's': txt = _("String"); break;
	case 'f': txt = _("Number"); break;
	case 'b': txt = _("Boolean"); break;
	case 'r': txt = _("Range"); break;
	case 'a': txt = _("Array"); break;
	case 'A': txt = _("Range/Array"); break;
	case '?':
	default: txt = _("Any");
	}

	as->name_label = gtk_label_new (as->name);
	gtk_table_attach (GTK_TABLE (state->arg_table),
			  as->name_label,
			  0, 1, row, row+1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);

	as->entry = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach (GTK_TABLE (state->arg_table),
			  GTK_WIDGET (as->entry),
			  1, 2, row, row+1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);

	if (as->is_optional)
		label = g_strconcat ("(", txt, ")", NULL);
	else
		label = g_strconcat ("=", txt, NULL);

	as->type_label = gtk_label_new (label);
	gtk_table_attach (GTK_TABLE (state->arg_table),
			  as->type_label,
			  2, 3, row, row+1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);
	g_free (label);

	/* FIXME : Do I really need focus-in ?  is there something less draconian */
	gtk_signal_connect (GTK_OBJECT (as->entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_formula_guru_entry_focus_in), as);
	gtk_signal_connect (GTK_OBJECT (as->entry), "changed",
			    GTK_SIGNAL_FUNC (cb_formula_guru_entry_changed), as);

	g_ptr_array_add (state->args, as);
	if (row == 0)
		as->state->cur_arg = as;
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

	function_def_count_args (state->fd, &arg_min, &arg_max);

	/* Var arg function */
	state->var_args = (arg_max == G_MAXINT);

	if (state->var_args) {
		/* Display 2 argument for vararg functions by default */
		for (i = 0; i < 2; i++)
			formula_guru_arg_new (NULL, '?', TRUE, state);
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
				formula_guru_arg_new (g_strndup (start, (int)(ptr - start)),
						      function_def_get_arg_type (state->fd, i),
						      (i >= arg_min), state);
				++i;
			}
			start = ptr + 1;
		}
		ptr++;
	}

	if (i != arg_max)
		g_warning ("Function mis-match : %s has %d named arguments but was declared to have %d",
			   function_def_get_name (state->fd), i, arg_max);

	while (i < arg_max) {
		formula_guru_arg_new (NULL,
				      function_def_get_arg_type (state->fd, i),
				      (i >= arg_min), state);
		++i;
	}

	g_free (copy_args);
}

static GtkWidget *
formula_guru_init_button (FomulaGuruState *state, char const *name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
	gtk_signal_connect (GTK_OBJECT (tmp), "clicked",
			    GTK_SIGNAL_FUNC (cb_formula_guru_clicked),
			    state);
	return tmp;
}

static gboolean
formula_guru_init (FomulaGuruState *state)
{
	TokenizedHelp *help_tokens;

	state->gui = gnumeric_glade_xml_new (workbook_command_context_gui (state->wb),
					     "formula-guru.glade");
        if (state->gui == NULL)
                return TRUE;


	state->args	= NULL;
	state->max_arg	= 0;
	state->help_tokens = tokenized_help_new (state->fd);
	state->args = g_ptr_array_new ();

	state->dialog	    = glade_xml_get_widget (state->gui, "FormulaGuru");
	state->help_button  = formula_guru_init_button (state, "help_button");
	state->rollup_button  = formula_guru_init_button (state, "rollup_button");
	state->rolldown_button= formula_guru_init_button (state, "rolldown_button");
	state->ok_button    = formula_guru_init_button (state, "ok_button");
	state->cancel_button= formula_guru_init_button (state, "cancel_button");
	state->rolled_box   = glade_xml_get_widget (state->gui, "rolled_box");
	state->rolled_label = glade_xml_get_widget (state->gui, "rolled_label");
	state->rolled_entry = glade_xml_get_widget (state->gui, "rolled_entry");
	state->unrolled_box = glade_xml_get_widget (state->gui, "unrolled_box");
	state->arg_table    = glade_xml_get_widget (state->gui, "arg_table");
	state->arg_frame    = glade_xml_get_widget (state->gui, "arg_frame");
	state->description  = glade_xml_get_widget (state->gui, "description");
	
	formula_guru_init_args (state);

	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_formula_guru_destroy),
			    state);

	help_tokens = tokenized_help_new (state->fd);
	gtk_frame_set_label (GTK_FRAME (state->arg_frame),
			     tokenized_help_find (help_tokens, "SYNTAX"));
	gtk_label_set_text  (GTK_LABEL (state->description),
			     tokenized_help_find (help_tokens, "DESCRIPTION"));
	tokenized_help_destroy (help_tokens);

	gtk_window_set_transient_for (GTK_WINDOW (state->dialog),
				      GTK_WINDOW (state->wb->toplevel));
	gtk_widget_show_all (GTK_WIDGET (state->arg_table));

	workbook_edit_attach_guru (state->wb, state->dialog);
	formula_guru_set_expr (state, 0);
	formula_guru_set_rolled_state (state, FALSE);

	return FALSE;
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
	FomulaGuruState *state;
	FunctionDefinition *fd;
	Sheet *sheet;
	Cell *cell;
	ExprTree const *expr = NULL;

	g_return_if_fail (wb != NULL);

	sheet = wb->current_sheet;
	cell = sheet_cell_get (sheet,
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (cell != NULL && cell_has_expr (cell))
		expr = expr_tree_first_func (cell->u.expression);

	/* If the current cell has no function calls,  clear cell, and start an
	 * expression
	 */
	if (expr == NULL) {
		workbook_start_editing_at_cursor (wb, TRUE, TRUE);
		gtk_entry_set_text (workbook_get_entry (wb), "=");

		fd = dialog_function_select (wb);
		if (fd == NULL) {
			workbook_finish_editing (wb, FALSE);
			return;
		}
	} else {
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);
		fd = expr_tree_get_func_def (expr);
	}

	state = g_new(FomulaGuruState, 1);
	state->wb	= wb;
	state->fd	= fd;
	state->valid	= FALSE;
	if (formula_guru_init (state)) {
		g_free (state);
		return;
	}

	/* If there were arguments initialize the fields */
	if (expr != NULL) {
		GList *l;
		int i = 0;
		char *str;
		ParsePos pos;
		parse_pos_init_cell (&pos, cell);

		for (l = expr->func.arg_list; l; l = l->next) {
			ArgumentState *as;

			while (i >= state->args->len)
				formula_guru_arg_new (NULL, '?', TRUE, state);

			as = g_ptr_array_index (state->args, i++);
			str = expr_tree_as_string (l->data, &pos);
			gtk_entry_set_text (as->entry, str);
			g_free (str);
		}
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;
	formula_guru_set_expr (state, 0);
}
