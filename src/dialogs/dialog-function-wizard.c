/* vim: set sw=8: */

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
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "gui-util.h"
#include "dialogs.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-edit.h"
#include "workbook-control.h"
#include "cell.h"
#include "expr.h"
#include "func.h"
#include "format.h"
#include "widgets/gnumeric-expr-entry.h"
#include <locale.h>

#define MAX_ARGS_DISPLAYED 4

typedef struct _FormulaGuruState FormulaGuruState;
typedef struct
{
	FormulaGuruState	*state;
	GtkWidget	*name_label, *type_label;
	GnumericExprEntry	*entry;

	gchar 	  *name;
	gchar	   type;
	gboolean   is_optional;
	int	   content_length;
	int	   index;
} ArgumentState;

struct _FormulaGuruState
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
	GtkWidget *arg_view;
	GtkRequisition arg_requisition;

	gboolean	    valid;
	gboolean	    is_rolled;
	gboolean	    var_args;
	ArgumentState	   *cur_arg;
	int		    max_arg; /* max arg # with a value */
	WorkbookControlGUI	   *wbcg;
	FunctionDefinition *fd;
	TokenizedHelp	   *help_tokens;
	GPtrArray	   *args;
};

static void formula_guru_arg_delete (FormulaGuruState *state, int i);
static void formula_guru_arg_new    (char * const name, char const type,
				     gboolean const is_optional,
				     FormulaGuruState *state);

static void
formula_guru_set_expr (FormulaGuruState *state, int index, gboolean set_text)
{
	GnumericExprEntry *entry;
	GString *str;
	int pos = 0;
	guint i;
	char const sep = format_get_arg_sep ();

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
		gchar *val = gtk_entry_get_text (GTK_ENTRY (as->entry));

		if (!as->is_optional || i <= (guint) state->max_arg || i <= (guint) index || strlen (val)) {
			if (i > 0)
				g_string_append_c (str, sep);
			if (i == (guint) index)
				pos = str->len +
					gtk_editable_get_position (GTK_EDITABLE (as->entry));

			g_string_append (str, val);
		}
	}
	g_string_append_c (str, ')'); /* FIXME use suffix string */

	entry = wbcg_get_entry (state->wbcg);
	if (set_text)
		gtk_entry_set_text (GTK_ENTRY (entry), str->str);

	/*
	 * ICK!
	 * This is necessary until Gtk-1.4.
	 * Setting the text resets the cursor position to 0 then triggers the
	 * changed event.  The changed handler is what handles creating the
	 * magic cursor to mark the range being edited
	 */
	gtk_entry_set_position (GTK_ENTRY (entry), pos);
	gtk_editable_changed (GTK_EDITABLE (entry));

	g_string_free (str, TRUE);
}

static void
cb_formula_guru_rolled_entry_changed (GtkEditable *editable,
				      FormulaGuruState *state)
{
	gtk_entry_set_text (GTK_ENTRY (state->cur_arg->entry),
		gtk_entry_get_text (GTK_ENTRY (state->rolled_entry)));
}

static gboolean
cb_formula_guru_entry_event (GtkWidget *w, GdkEvent *ev, ArgumentState *as)
{
	g_return_val_if_fail (as != NULL, TRUE);

	/* FIXME : this is lazy.  Have a special routine */
	formula_guru_set_expr (as->state, as->index, FALSE);
	return FALSE;
}

static gboolean
cb_formula_guru_rolled_entry_event (GtkWidget *w, GdkEvent *ev,
				    FormulaGuruState *state)
{
	return cb_formula_guru_entry_event (w, ev, state->cur_arg);
}

static void
cb_formula_guru_entry_changed (GtkEditable *editable, ArgumentState *as)
{
	as->content_length
		= strlen (gtk_entry_get_text (GTK_ENTRY (as->entry)));

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

	formula_guru_set_expr (as->state, as->index, TRUE);
}

static void
formula_guru_set_rolled_state (FormulaGuruState *state, gboolean is_rolled)
{
	GnumericExprEntry *new_entry;

	if (state->cur_arg == NULL) {
		gtk_widget_hide	(state->rolled_box);
		gtk_widget_show	(state->unrolled_box);
		gtk_widget_hide (state->rolldown_button);
		gtk_widget_hide (state->rollup_button);
		return;
	}

	state->is_rolled = is_rolled;
	if (is_rolled) {
		new_entry = GNUMERIC_EXPR_ENTRY (state->rolled_entry);
		gtk_entry_set_text (
			GTK_ENTRY (new_entry),
			gtk_entry_get_text (
				GTK_ENTRY (state->cur_arg->entry)));
		gtk_label_set_text (GTK_LABEL (state->rolled_label),
			state->cur_arg->name);

		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					  GTK_EDITABLE (new_entry));

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
	gtk_widget_grab_focus (GTK_WIDGET (new_entry));
	wbcg_set_entry (state->wbcg, new_entry);
}

static gboolean
cb_formula_guru_entry_focus_in (GtkWidget *ignored0, GdkEventFocus *ignored1, ArgumentState *as)
{
	ArgumentState *tmp;
	FormulaGuruState *state = as->state;
	GtkViewport *view = GTK_VIEWPORT (as->state->arg_view);
	GtkAdjustment *va = view->vadjustment;
	gboolean scrolled = FALSE;
	int i, lim;

	if (state->var_args) {
		i = state->args->len - 1;

		if (i == as->index) {
			formula_guru_arg_new (NULL, '?', TRUE, state);
			gtk_widget_show_all (state->arg_table);
			/* FIXME : scroll window to make visible if necessary */
		} else {
			lim = MAX (as->index+1, state->max_arg) + 1;
			if (lim < MAX_ARGS_DISPLAYED)
				lim = MAX_ARGS_DISPLAYED;

			for (; i >= lim ; --i) {
				tmp = g_ptr_array_index (state->args, i);

				gtk_container_remove (GTK_CONTAINER (state->arg_table),
						      GTK_WIDGET (tmp->name_label));
				gtk_container_remove (GTK_CONTAINER (state->arg_table),
						      GTK_WIDGET (tmp->entry));
				gtk_container_remove (GTK_CONTAINER (state->arg_table),
						      GTK_WIDGET (tmp->type_label));
				formula_guru_arg_delete (state, i);
			}

			g_ptr_array_set_size (state->args, lim);
		}
	}

	/* Do we want to scroll */
	if (as->index > 0) {
		ArgumentState *tmp = g_ptr_array_index (state->args, as->index-1);
		int const prev_top = tmp->name_label->allocation.y;
		int const cur_bottom =
			as->name_label->allocation.y +
			as->name_label->allocation.height;

		if (va->value > prev_top &&
		    (prev_top + va->page_size) >= cur_bottom) {
			va->value = prev_top;
			scrolled = TRUE;
		}
	}
	if (!scrolled && as->index < (int) as->state->args->len-1) {
		ArgumentState *tmp = g_ptr_array_index (state->args, as->index+1);
		int const cur_top = as->name_label->allocation.y;
		int const next_bottom =
			tmp->name_label->allocation.y +
			tmp->name_label->allocation.height;

		if (next_bottom > (va->value + va->page_size) &&
		    cur_top >= (next_bottom - va->page_size)) {
			va->value = next_bottom - va->page_size;
			scrolled = TRUE;
		}
	}

	if (scrolled)
		gtk_adjustment_value_changed (va);

	state->cur_arg = as;
	wbcg_set_entry (state->wbcg, as->entry);
	formula_guru_set_expr (state, as->index, TRUE);

	return FALSE;
}

static gboolean
cb_formula_guru_destroy (GtkObject *w, FormulaGuruState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->args != NULL) {
		int i;

		for (i = state->args->len; i-- > 0 ;)
			formula_guru_arg_delete (state, i);
		g_ptr_array_free (state->args, TRUE);
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
	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);
	return FALSE;
}

static  gint
cb_formula_guru_key_press (GtkWidget *widget, GdkEventKey *event,
			   FormulaGuruState *state)
{
	if (event->keyval == GDK_Escape) {
		wbcg_edit_finish (state->wbcg, FALSE);
		return TRUE;
	} else
		return FALSE;
}

static void
cb_formula_guru_clicked (GtkWidget *button, FormulaGuruState *state)
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

	/* Detach BEFORE we finish editing */
	wbcg_edit_detach_guru (state->wbcg);
	wbcg_edit_finish (state->wbcg, button == state->ok_button);

	gtk_widget_destroy (state->dialog);
}

static void
formula_guru_arg_delete (FormulaGuruState *state, int i)
{
	ArgumentState *as = g_ptr_array_index (state->args, i);
	g_free (as->name);
	g_free (as);
}

static void
formula_guru_arg_new (char * const name,
		      char const type,
		      gboolean const is_optional,
		      FormulaGuruState *state)
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
	case 'S': txt = _("Scalar"); break;
	case '?':
	default: txt = _("Any");
	}

	as->name_label = gtk_label_new (as->name);
	gtk_table_attach (GTK_TABLE (state->arg_table),
			  as->name_label,
			  0, 1, row, row+1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);

	as->entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (
		as->entry, GNUM_EE_SHEET_OPTIONAL, GNUM_EE_SHEET_OPTIONAL);
	gtk_table_attach (GTK_TABLE (state->arg_table),
			  GTK_WIDGET (as->entry),
			  1, 2, row, row+1,
			  GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (as->entry));

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
	gtk_signal_connect_after (GTK_OBJECT (as->entry), "key-press-event",
		GTK_SIGNAL_FUNC (cb_formula_guru_entry_event), as);
	gtk_signal_connect_after (GTK_OBJECT (as->entry), "button-press-event",
		GTK_SIGNAL_FUNC (cb_formula_guru_entry_event), as);

	gnumeric_expr_entry_set_scg (as->entry,
				     wb_control_gui_cur_sheet (state->wbcg));

	g_ptr_array_add (state->args, as);
	if (row == 0)
		as->state->cur_arg = as;
	if (row + 1 == MAX_ARGS_DISPLAYED) {
		gtk_widget_show_all (state->arg_table);
		gtk_widget_size_request (state->arg_table,
					 &state->arg_requisition);
	}
}

static void
formula_guru_init_args (FormulaGuruState *state)
{
	gchar *copy_args;
	const gchar *syntax;
	gchar *ptr, *start = NULL;
	int i;
	int arg_max, arg_min;
	gchar arg_separator;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->fd != NULL);
	g_return_if_fail (state->help_tokens != NULL);

	state->cur_arg = NULL;

	function_def_count_args (state->fd, &arg_min, &arg_max);

	/* Var arg function */
	state->var_args = (arg_max == G_MAXINT);

	if (state->var_args) {
		/* Display at least MAX_ARGS_DISPLAYED vararg functions */
		for (i = 0; i < MAX_ARGS_DISPLAYED; i++)
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
	/*
	 We must use different argument separator for parsing depending
	 on if the function's help is localized or not. If the help is
	 translated, e.g. to Polish ("@SYNTAX=DGET(baza_danych;pole;kryteria)")
	 then we use locale dependent separator. If the help is not translated
	 then we use comma. */
	arg_separator = state->help_tokens->help_is_localized
	                ? format_get_arg_sep ()
	                : ',';
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
		if (*ptr == arg_separator || *ptr == ')') {
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
formula_guru_init_button (FormulaGuruState *state, char const *name)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
	gtk_signal_connect (GTK_OBJECT (tmp), "clicked",
			    GTK_SIGNAL_FUNC (cb_formula_guru_clicked),
			    state);
	return tmp;
}

static void
formula_guru_set_scrollwin_size (FormulaGuruState *state)
{
	GtkWidget *scrollwin;
	gint height;

	scrollwin = glade_xml_get_widget (state->gui, "scrolledwindow1");

	if (state->arg_requisition.height == 0) {
		gtk_widget_show_all (state->arg_table);
		gtk_widget_size_request (state->arg_table,
					 &state->arg_requisition);
	}

	height = state->arg_requisition.height +
		2 * GTK_CONTAINER (scrollwin)->border_width;
	gtk_widget_set_usize (scrollwin, -2, height);
}

static gboolean
formula_guru_init (FormulaGuruState *state, ExprTree const *expr, Cell const *cell)
{
	TokenizedHelp *help_tokens;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "formula-guru.glade");
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
	state->rolled_entry = gnumeric_expr_entry_new (state->wbcg);
	gnumeric_expr_entry_set_flags (
		GNUMERIC_EXPR_ENTRY (state->rolled_entry),
		GNUM_EE_SHEET_OPTIONAL, GNUM_EE_SHEET_OPTIONAL);
	gtk_box_pack_start (GTK_BOX (state->rolled_box), state->rolled_entry,
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (state->rolled_entry));
	state->unrolled_box = glade_xml_get_widget (state->gui, "unrolled_box");
	state->arg_table    = glade_xml_get_widget (state->gui, "arg_table");
	state->arg_frame    = glade_xml_get_widget (state->gui, "arg_frame");
	state->description  = glade_xml_get_widget (state->gui, "description");
	state->arg_view	    = glade_xml_get_widget (state->gui, "arg_view");
	state->arg_requisition.width = state->arg_requisition.height = 0;

	formula_guru_init_args (state);

	gtk_signal_connect (GTK_OBJECT (state->rolled_entry), "changed",
		GTK_SIGNAL_FUNC (cb_formula_guru_rolled_entry_changed), state);
	gtk_signal_connect_after (GTK_OBJECT (state->rolled_entry), "key-press-event",
		GTK_SIGNAL_FUNC (cb_formula_guru_rolled_entry_event), state);
	gtk_signal_connect_after (GTK_OBJECT (state->rolled_entry), "button-press-event",
		GTK_SIGNAL_FUNC (cb_formula_guru_rolled_entry_event), state);

	/* If there were arguments initialize the fields */
	if (expr != NULL) {
		GList *l;
		guint i = 0;
		char *str;
		ParsePos pos;
		parse_pos_init_cell (&pos, cell);

		for (l = expr->func.arg_list; l; l = l->next, ++i) {
			ArgumentState *as;

			while (i >= state->args->len)
				formula_guru_arg_new (NULL, '?', TRUE, state);

			as = g_ptr_array_index (state->args, i);
			str = expr_tree_as_string (l->data, &pos);
			gtk_entry_set_text (GTK_ENTRY (as->entry), str);
			if (i == 0)
				gtk_widget_grab_focus (GTK_WIDGET (as->entry));
			g_free (str);
		}
		gtk_widget_show_all (state->arg_table);
	}

	/* Lifecyle management */
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_expr_entry_set_scg (GNUMERIC_EXPR_ENTRY (state->rolled_entry),
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_formula_guru_destroy),
			    state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (cb_formula_guru_key_press),
			    state);

	help_tokens = tokenized_help_new (state->fd);
	gtk_frame_set_label (GTK_FRAME (state->arg_frame),
			     tokenized_help_find (help_tokens, "SYNTAX"));
	gtk_label_set_text  (GTK_LABEL (state->description),
			     tokenized_help_find (help_tokens, "DESCRIPTION"));
	tokenized_help_destroy (help_tokens);

	formula_guru_set_scrollwin_size (state);
	gnumeric_set_transient (state->wbcg, GTK_WINDOW (state->dialog));

	formula_guru_set_expr (state, 0, TRUE);
	formula_guru_set_rolled_state (state, FALSE);

	return FALSE;
}

/**
 * dialog_formula_guru
 * @wbcg : The workbook to use as a parent window.
 *
 * Pop up a function selector then a formula guru.
 */
void
dialog_formula_guru (WorkbookControlGUI *wbcg)
{
	FormulaGuruState *state;
	FunctionDefinition *fd;
	ExprTree const *expr = NULL;
	Sheet	 *sheet;
	Cell	 *cell;

	g_return_if_fail (wbcg != NULL);

	sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	cell = sheet_cell_get (sheet,
			       sheet->edit_pos.col,
			       sheet->edit_pos.row);

	if (cell != NULL && cell_has_expr (cell))
		expr = expr_tree_first_func (cell->base.expression);

	/* If the current cell has no function calls,  clear cell, and start an
	 * expression
	 */
	if (expr == NULL) {
		wbcg_edit_start (wbcg, TRUE, TRUE);
		gtk_entry_set_text (
			GTK_ENTRY (wbcg_get_entry (wbcg)), "=");

		fd = dialog_function_select (wbcg);
		if (fd == NULL) {
			wbcg_edit_finish (wbcg, FALSE);
			return;
		}
	} else {
		wbcg_edit_start (wbcg, FALSE, TRUE);
		fd = expr_tree_get_func_def (expr);
	}

	state = g_new(FormulaGuruState, 1);
	state->wbcg	= wbcg;
	state->fd	= fd;
	state->valid	= FALSE;
	if (formula_guru_init (state, expr, cell)) {
		g_free (state);
		return;
	}

	/* Ok everything is hooked up. Let-er rip */
	state->valid = TRUE;
	gtk_widget_show (state->dialog);
	formula_guru_set_expr (state, 0, TRUE);
}
