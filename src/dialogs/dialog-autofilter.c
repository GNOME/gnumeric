/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/**
 * dialog-autofilter.c:  A pair of dialogs for autofilter conditions
 *
 * (c) Copyright 2002 Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
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
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <sheet-filter.h>
#include <number-match.h>
#include <undo.h>

#include <gtk/gtk.h>
#include <string.h>

typedef struct {
	GtkBuilder         *gui;
	WBCGtk *wbcg;
	GtkWidget          *dialog;
	GnmFilter	   *filter;
	unsigned	    field;
	gboolean	    is_expr;
} AutoFilterState;

#define DIALOG_KEY "autofilter"
#define DIALOG_KEY_EXPRESSION "autofilter-expression"
#define UNICODE_ELLIPSIS "\xe2\x80\xa6"

static void
cb_autofilter_destroy (AutoFilterState *state)
{
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static GnmValue *
map_op (AutoFilterState *state, GnmFilterOp *op,
	char const *op_widget, char const *val_widget)
{
	int i;
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, val_widget);
	char const *txt = gtk_entry_get_text (GTK_ENTRY (w));
	GnmValue *v = NULL;

	*op = GNM_FILTER_UNUSED;
	if (txt == NULL || *txt == '\0')
		return NULL;

	w = go_gtk_builder_get_widget (state->gui, op_widget);
	i = gtk_combo_box_get_active (GTK_COMBO_BOX (w));
	switch (i) {
	case 0: return NULL;
	case 1: *op = GNM_FILTER_OP_EQUAL;	break;
	case 2: *op = GNM_FILTER_OP_NOT_EQUAL;	break;
	case 3: *op = GNM_FILTER_OP_GT;		break;
	case 4: *op = GNM_FILTER_OP_GTE;	break;
	case 5: *op = GNM_FILTER_OP_LT;		break;
	case 6: *op = GNM_FILTER_OP_LTE;	break;

	case 7:
	case 8: *op = (i == 8) ? GNM_FILTER_OP_NOT_EQUAL : GNM_FILTER_OP_EQUAL;
		v = value_new_string_nocopy (g_strconcat (txt, "*", NULL));
		break;

	case 9:
	case 10: *op = (i == 10) ? GNM_FILTER_OP_NOT_EQUAL : GNM_FILTER_OP_EQUAL;
		v = value_new_string_nocopy (g_strconcat ("*", txt, NULL));
		break;

	case 11:
	case 12: *op = (i == 12) ? GNM_FILTER_OP_NOT_EQUAL : GNM_FILTER_OP_EQUAL;
		v = value_new_string_nocopy (g_strconcat ("*", txt, "*", NULL));
		break;
	default :
		g_warning ("huh?");
		return NULL;
	}

	if (v == NULL) {
		Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (state->wbcg));
		v = format_match (txt, NULL, workbook_date_conv (wb));
	}
	if (v == NULL)
		v = value_new_string (txt);

	return v;
}

static void
cb_autofilter_ok (G_GNUC_UNUSED GtkWidget *button,
		  AutoFilterState *state)
{
	GnmFilterCondition *cond = NULL;
	GtkWidget *w;

	if (state->is_expr) {
		GnmFilterOp op0;
		GnmValue *v0 = map_op (state, &op0, "op0", "value0");

		if (op0 != GNM_FILTER_UNUSED) {
			GnmFilterOp op1;
			GnmValue *v1 = map_op (state, &op1, "op1", "value1");
			if (op1 != GNM_FILTER_UNUSED) {
				w = go_gtk_builder_get_widget (state->gui, "and_button");
				cond = gnm_filter_condition_new_double (op0, v0,
						gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)),
						op1, v1);
			} else
				cond = gnm_filter_condition_new_single (op0, v0);
		}
	} else {
		int bottom, percentage, count;

		w = go_gtk_builder_get_widget (state->gui, "top_vs_bottom_option_menu");
		bottom = gtk_combo_box_get_active (GTK_COMBO_BOX (w));

		w = go_gtk_builder_get_widget (state->gui, "item_vs_percentage_option_menu");
		percentage = gtk_combo_box_get_active (GTK_COMBO_BOX (w));

		w = go_gtk_builder_get_widget (state->gui, "item_count");
		count = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));
		if (bottom >= 0 && percentage >= 0)
			cond = gnm_filter_condition_new_bucket (
					!bottom, !percentage, count);
	}
	if (cond != NULL)
		cmd_autofilter_set_condition (WORKBOOK_CONTROL (state->wbcg),
					      state->filter, state->field,
					      cond);

	gtk_widget_destroy (state->dialog);
}

static void
cb_autofilter_cancel (G_GNUC_UNUSED GtkWidget *button,
		      AutoFilterState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_top10_type_changed (GtkComboBox *menu,
		       AutoFilterState *state)
{
	GtkWidget *spin = go_gtk_builder_get_widget (state->gui, "item_count");

	gtk_spin_button_set_range (GTK_SPIN_BUTTON (spin), 1.,
		(gtk_combo_box_get_active (menu) > 0) ? 100. : 500.);
}

static void
init_operator (AutoFilterState *state, GnmFilterOp op, GnmValue const *v,
	       char const *op_widget, char const *val_widget)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, op_widget);
	char const *str = v ? value_peek_string (v) : NULL;
	char *content = NULL;
	int i;

	switch (op) {
	case GNM_FILTER_OP_EQUAL:	i = 1; break;
	case GNM_FILTER_OP_GT:		i = 3; break;
	case GNM_FILTER_OP_LT:		i = 5; break;
	case GNM_FILTER_OP_GTE:		i = 4; break;
	case GNM_FILTER_OP_LTE:		i = 6; break;
	case GNM_FILTER_OP_NOT_EQUAL:	i = 2; break;
	default :
		return;
	}

	if (v != NULL && VALUE_IS_STRING (v) && (i == 1 || i == 2)) {
		unsigned const len = strlen (str);

		/* there needs to be at least 1 letter */
		int ends = (len > 1 && str[0] == '*') ? 1 : 0; /* as a bool and offset */

		if (len > 1 && str[len-1] == '*' && str[len-2] != '~') {
			content = g_strdup (str + ends);
			content[len - ends - 1] = '\0';
			i += (ends ? 10 : 6);
		} else if (ends) {
			str += 1;
			i += 8;
		}
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (w), i);

	w = go_gtk_builder_get_widget (state->gui, val_widget);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog), w);
	if (v != NULL)
		gtk_entry_set_text (GTK_ENTRY (w), content ? content : str);

	g_free (content);
}

void
dialog_auto_filter (WBCGtk *wbcg,
		    GnmFilter *filter, int field,
		    gboolean is_expr, GnmFilterCondition *cond)
{
	AutoFilterState *state;
	GtkWidget *w;
	GtkBuilder *gui;
	int col;
	gchar *label;
	GnmCell *cell;
	int len = is_expr ? 15 : 30;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists
	    (wbcg, is_expr ? DIALOG_KEY_EXPRESSION : DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_new ((is_expr ? "autofilter-expression.ui" : "autofilter-top10.ui"),
				   NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (AutoFilterState, 1);
	state->wbcg	= wbcg;
	state->filter	= filter;
	state->field	= field;
	state->is_expr	= is_expr;
	state->gui	= gui;

	g_return_if_fail (state->gui != NULL);

	col = filter->r.start.col + field;

	cell = sheet_cell_get (filter->sheet, col, filter->r.start.row);

	if (cell == NULL || gnm_cell_is_blank (cell))
		label = g_strdup_printf (_("Column %s"), col_name (col));
	else {
		char *content = gnm_cell_get_rendered_text (cell);
		if (g_utf8_strlen (content, -1) > len) {
			char *end = g_utf8_find_prev_char (content, content + len + 1 - strlen (UNICODE_ELLIPSIS));
			strcpy (end, UNICODE_ELLIPSIS);
		}
		label = g_strdup_printf (_("Column %s (\"%s\")"),
					 col_name (col), content);
		g_free (content);
	}

	if (is_expr) {
		gtk_label_set_text
			(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label1")), label);
		gtk_label_set_text
			(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label2")), label);
	} else {
		gtk_label_set_text
			(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label")), label);
		w = go_gtk_builder_get_widget (state->gui, "item_vs_percentage_option_menu");
		g_signal_connect (G_OBJECT (w),
			"changed",
			G_CALLBACK (cb_top10_type_changed), state);
	}
	g_free (label);

	state->dialog = go_gtk_builder_get_widget (state->gui, "dialog");
	if (cond != NULL) {
		GnmFilterOp const op = cond->op[0];
		if (is_expr && 0 == (op & GNM_FILTER_OP_TYPE_MASK)) {
			init_operator (state, cond->op[0],
				       cond->value[0], "op0", "value0");
			if (cond->op[1] != GNM_FILTER_UNUSED)
				init_operator (state, cond->op[1],
					       cond->value[1], "op1", "value1");
			w = go_gtk_builder_get_widget (state->gui,
				cond->is_and ? "and_button" : "or_button");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
		} else if (!is_expr &&
			   GNM_FILTER_OP_TOP_N == (op & GNM_FILTER_OP_TYPE_MASK)) {
			w = go_gtk_builder_get_widget (state->gui, "top_vs_bottom_option_menu");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), (op & 1) ? 1 : 0);
			w = go_gtk_builder_get_widget (state->gui, "item_vs_percentage_option_menu");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), (op & 2) ? 1 : 0);
			w = go_gtk_builder_get_widget (state->gui, "item_count");
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (w),
						   cond->count);
		}
	} else {
		/* initialize the combo boxes (not done by li.ui) */
		if (is_expr) {
			w = go_gtk_builder_get_widget (state->gui, "op0");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
			w = go_gtk_builder_get_widget (state->gui, "op1");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
		} else {
			w = go_gtk_builder_get_widget (state->gui, "top_vs_bottom_option_menu");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
			w = go_gtk_builder_get_widget (state->gui, "item_vs_percentage_option_menu");
			gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
		}
	}

	w = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_autofilter_ok), state);
	w = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_autofilter_cancel), state);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		is_expr ? GNUMERIC_HELP_LINK_AUTOFILTER_CUSTOM :
		GNUMERIC_HELP_LINK_AUTOFILTER_TOP_TEN);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_autofilter_destroy);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       is_expr ? DIALOG_KEY_EXPRESSION : DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
