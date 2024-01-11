/*
 * dialog-autofilter.c:  A pair of dialogs for autofilter conditions
 *
 * (c) Copyright 2002 Jody Goldberg <jody@gnome.org>
 * (C) Copyright 2024 Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <sheet-filter.h>
#include <number-match.h>
#include <undo.h>

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

static char const * const type_group[] = {
	"items-largest",
	"items-smallest",
	"percentage-largest",
	"percentage-smallest",
	"percentage-largest-number",
	"percentage-smallest-number",
	NULL
};

static GnmFilterOp
autofilter_get_type (AutoFilterState *state)
{
	return (GNM_FILTER_OP_TYPE_BUCKETS |
		gnm_gui_group_value (state->gui, type_group));
}


static void
cb_autofilter_destroy (AutoFilterState *state)
{
	if (state->gui != NULL) {
		g_object_unref (state->gui);
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
		Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));
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
				w = go_gtk_builder_get_widget (state->gui,
							       "and_button");
				cond = gnm_filter_condition_new_double
					(op0, v0,
					 gtk_toggle_button_get_active
					 (GTK_TOGGLE_BUTTON (w)),
					 op1, v1);
			} else
				cond = gnm_filter_condition_new_single
					(op0, v0);
		}
	} else {
		int count;
		GnmFilterOp op = autofilter_get_type (state);

		w = go_gtk_builder_get_widget (state->gui, "item_count");
		count = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));

		cond = gnm_filter_condition_new_bucket
			(!(op & GNM_FILTER_OP_BOTTOM_MASK),
			 !(op & GNM_FILTER_OP_PERCENT_MASK),
			 !(op & GNM_FILTER_OP_REL_N_MASK),
			 count);
	}
	if (cond != NULL)
		cmd_autofilter_set_condition (GNM_WBC (state->wbcg),
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
cb_top10_count_changed (GtkSpinButton *button,
			AutoFilterState *state)
{
	int val = 0.5 + gtk_spin_button_get_value (button);
	GtkWidget *w;
	gchar *label;
	int cval = val, count;

	count = range_height(&(state->filter->r)) - 1;

	if (cval > count)
		cval = count;

	w = go_gtk_builder_get_widget (state->gui, type_group[0]);
	/* xgettext : %d gives the number of items in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf (ngettext ("Show the largest item",
					   "Show the %d largest items",
					   cval),
				 cval);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);

	w = go_gtk_builder_get_widget (state->gui, type_group[1]);
	/* xgettext : %d gives the number of items in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf (ngettext ("Show the smallest item",
					   "Show the %d smallest items",
					   cval),
				 cval);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);

	if (val > 100)
		val = 100;

	w = go_gtk_builder_get_widget (state->gui, type_group[2]);
	/* xgettext : %d gives the percentage of the data range in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf
		(ngettext ("Show the items in the top %d%% of the data range",
			   "Show the items in the top %d%% of the data range", val),
		 val);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);

	w = go_gtk_builder_get_widget (state->gui, type_group[3]);
	/* xgettext : %d gives the percentage of the data range in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf
		(ngettext ("Show the items in the bottom %d%% of the data range",
			   "Show the items in the bottom %d%% of the data range", val),
		 val);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);


	w = go_gtk_builder_get_widget (state->gui, type_group[4]);
	/* xgettext : %d gives the percentage of item number in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf
		(ngettext ("Show the top %d%% of all items",
			   "Show the top %d%% of all items", val),
		 val);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);

	w = go_gtk_builder_get_widget (state->gui, type_group[5]);
	/* xgettext : %d gives the percentage of the item number in the autofilter. */
	/* This is input to ngettext. */
	label = g_strdup_printf
		(ngettext ("Show the bottom %d%% of all items",
			   "Show the bottom %d%% of all items", val),
		 val);
	gtk_button_set_label (GTK_BUTTON (w),label);
	g_free(label);
}

static void
cb_top10_type_changed (G_GNUC_UNUSED GtkToggleButton *button,
		       AutoFilterState *state)
{
	GnmFilterOp op = autofilter_get_type (state);
	GtkWidget *spin = go_gtk_builder_get_widget (state->gui, "item_count");
	GtkWidget *label = go_gtk_builder_get_widget (state->gui, "cp-label");

	if ((op & GNM_FILTER_OP_PERCENT_MASK) != 0) {
		gtk_spin_button_set_range (GTK_SPIN_BUTTON (spin), 1.,
					   100.);
		gtk_label_set_text (GTK_LABEL (label), _("Percentage:"));
	} else {
		gtk_spin_button_set_range
			(GTK_SPIN_BUTTON (spin), 1.,
			 range_height(&(state->filter->r)) - 1);
		gtk_label_set_text (GTK_LABEL (label), _("Count:"));
	}
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
	gnm_editable_enters (GTK_WINDOW (state->dialog), w);
	if (v != NULL)
		gtk_entry_set_text (GTK_ENTRY (w), content ? content : str);

	g_free (content);
}

static gchar *
dialog_auto_filter_get_col_name (GnmCell *cell, int col, int len)
{
	gchar *label;
	char *content = gnm_cell_get_rendered_text (cell);
	if (g_utf8_strlen (content, -1) > len) {
		char *end = g_utf8_find_prev_char
			(content, content + len + 1 - strlen (UNICODE_ELLIPSIS));
		strcpy (end, UNICODE_ELLIPSIS);
	}
	label = g_strdup_printf (_("Column %s (\"%s\")"),
				 col_name (col), content);
	g_free (content);
	return label;
}
static void
dialog_auto_filter_expression (WBCGtk *wbcg,
			       GnmFilter *filter, int field,
			       GnmFilterCondition *cond)
{
	AutoFilterState *state;
	GtkWidget *w;
	GtkBuilder *gui;
	int col;
	gchar *label;
	GnmCell *cell;
	int const len = 15;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists
	    (wbcg, DIALOG_KEY_EXPRESSION))
		return;
	gui = gnm_gtk_builder_load ("res:ui/autofilter-expression.ui",
				   NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (AutoFilterState, 1);
	state->wbcg	= wbcg;
	state->filter	= filter;
	state->field	= field;
	state->is_expr	= TRUE;
	state->gui	= gui;

	g_return_if_fail (state->gui != NULL);

	col = filter->r.start.col + field;

	cell = sheet_cell_get (filter->sheet, col, filter->r.start.row);

	if (cell == NULL || gnm_cell_is_blank (cell))
		label = g_strdup_printf (_("Column %s"), col_name (col));
	else
		label = dialog_auto_filter_get_col_name (cell, col, len);

	gtk_label_set_text
		(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label1")), label);
	gtk_label_set_text
		(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label2")), label);
	g_free (label);

	state->dialog = go_gtk_builder_get_widget (state->gui, "dialog");
	if (cond != NULL) {
		GnmFilterOp const op = cond->op[0];
		if (0 == (op & GNM_FILTER_OP_TYPE_MASK)) {
			init_operator (state, cond->op[0],
				       cond->value[0], "op0", "value0");
			if (cond->op[1] != GNM_FILTER_UNUSED)
				init_operator (state, cond->op[1],
					       cond->value[1], "op1", "value1");
			w = go_gtk_builder_get_widget (state->gui,
				cond->is_and ? "and_button" : "or_button");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
		}
	} else {
		/* initialize the combo boxes (not done by li.ui) */
		w = go_gtk_builder_get_widget (state->gui, "op0");
		gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
		w = go_gtk_builder_get_widget (state->gui, "op1");
		gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
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
	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_AUTOFILTER_CUSTOM);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_autofilter_destroy);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       DIALOG_KEY_EXPRESSION);
	gtk_widget_show (state->dialog);
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
	char const * const *rb;

	if (is_expr) {
		dialog_auto_filter_expression (wbcg, filter, field, cond);
		return;
	}

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/autofilter-top10.ui",
				   NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (AutoFilterState, 1);
	state->wbcg	= wbcg;
	state->filter	= filter;
	state->field	= field;
	state->is_expr	= FALSE;
	state->gui	= gui;

	g_return_if_fail (state->gui != NULL);

	col = filter->r.start.col + field;

	cell = sheet_cell_get (filter->sheet, col, filter->r.start.row);

	if (cell == NULL || gnm_cell_is_blank (cell))
		label = g_strdup_printf (_("Column %s"), col_name (col));
	else
		label = dialog_auto_filter_get_col_name (cell, col, len);

	gtk_label_set_text
		(GTK_LABEL (go_gtk_builder_get_widget (state->gui, "col-label")), label);
	g_free (label);

	state->dialog = go_gtk_builder_get_widget (state->gui, "dialog");
	if (cond != NULL && GNM_FILTER_OP_TOP_N == (cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		gchar const *radio = NULL;
		switch (cond->op[0]) {
		case GNM_FILTER_OP_TOP_N:
		default:
			radio = type_group[0];
			break;
		case GNM_FILTER_OP_BOTTOM_N:
			radio = type_group[1];
			break;
		case GNM_FILTER_OP_TOP_N_PERCENT:
			radio = type_group[2];
			break;
		case GNM_FILTER_OP_BOTTOM_N_PERCENT:
			radio = type_group[3];
			break;
		case GNM_FILTER_OP_TOP_N_PERCENT_N:
			radio = type_group[4];
			break;
		case GNM_FILTER_OP_BOTTOM_N_PERCENT_N:
			radio = type_group[5];
			break;
		}
		w = go_gtk_builder_get_widget (state->gui, radio);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
	} else {
		w = go_gtk_builder_get_widget (state->gui, "items-largest");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
	}

	w = go_gtk_builder_get_widget (state->gui, "item_count");
	g_signal_connect (G_OBJECT (w),
			  "value-changed",
			  G_CALLBACK (cb_top10_count_changed), state);
	if (cond != NULL && GNM_FILTER_OP_TOP_N == (cond->op[0] & GNM_FILTER_OP_TYPE_MASK))
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), cond->count);
	else
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (w),
				   range_height(&(state->filter->r))/2);
	cb_top10_count_changed (GTK_SPIN_BUTTON (w), state);
	cb_top10_type_changed (NULL, state);

	rb = type_group;
	while (*rb != NULL) {
		w = go_gtk_builder_get_widget (state->gui, *rb);
		g_signal_connect (G_OBJECT (w),
				  "toggled",
				  G_CALLBACK (cb_top10_type_changed), state);
		rb++;
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
	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_AUTOFILTER_TOP_TEN);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_autofilter_destroy);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
