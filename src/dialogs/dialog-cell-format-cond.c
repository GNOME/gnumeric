/*
 * dialog-cell-format-cond.c:  Implements a dialog to format cells.
 *
 * (c) Copyright 2010-2011 Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
  **/

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <gui-util.h>
#include <selection.h>
#include <ranges.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <position.h>
#include <mstyle.h>
#include <application.h>
#include <validation.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <mathfunc.h>
#include <style-conditions.h>


#include <string.h>

#define CELL_FORMAT_KEY "cell-format-cond-dialog"
#define CELL_FORMAT_DEF_KEY "cell-format-cond-def-dialog"

typedef struct _CFormatState {
	GtkBuilder	*gui;
	WBCGtk	        *wbcg;
	GtkDialog	*dialog;
	GtkWidget	*close_button;

	Sheet		*sheet;
	SheetView	*sv;
	unsigned int	 conflicts;
	gboolean         homogeneous;
	GnmStyle	*style;

	GtkButton       *remove;
	GtkButton       *clear;
	GtkButton       *expand;
	GtkLabel        *label;
	GtkTreeView     *treeview;
	GtkTreeStore    *model;
	GtkTreeSelection *selection;

	struct {
		GOUndo *undo;
		GOUndo *redo;
		int size;
		GnmStyle *new_style;
		GnmStyle *old_style;
		gboolean  existing_conds_only;
	} action;
	struct {
		GtkWidget	*edit_style_button;
		GtkWidget	*add_button;
		GtkWidget	*replace_button;
		GtkWidget	*copy_button;
		GtkWidget	*combo;
		GtkWidget       *expr_x;
		GtkWidget       *expr_y;
		GtkListStore    *typestore;
		GnmStyle        *style;
		GtkWidget       *style_label;
		GtkDialog       *dialog;
	} editor;
} CFormatState;

enum {
	CONDITIONS_RANGE,
	CONDITIONS_COND,
	CONDITIONS_REFERENCE,
	CONDITIONS_NUM_COLUMNS
};



/*****************************************************************************/
static void c_fmt_dialog_load (CFormatState *state);
static void c_fmt_dialog_apply_add_choice (CFormatState *state, GnmStyleCond *cond, gboolean add);

/*****************************************************************************/

/* button handlers */
static void
cb_c_fmt_dialog_dialog_buttons (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
	/* users may accidentally click on 'close' before adding the formatting style see #733352 */
	if (!gtk_widget_get_sensitive (GTK_WIDGET (state->editor.add_button)) ||
	    gtk_widget_get_sensitive (GTK_WIDGET (state->clear)) ||
	    go_gtk_query_yes_no (GTK_WINDOW (state->dialog), FALSE,
				 _("You did not add the defined conditional format."
				   " Do you really want to close the conditional formatting dialog?")))
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

/* Handler for destroy */
static void
cb_c_fmt_dialog_dialog_destroy (CFormatState *state)
{
	if (state->editor.dialog) {
		gtk_widget_destroy (GTK_WIDGET (state->editor.dialog));
		state->editor.dialog = NULL;
	}
	if (state->editor.style)
		gnm_style_unref (state->editor.style);
	if (state->style)
		gnm_style_unref (state->style);
	g_object_unref (state->gui);
	g_free (state);
}

static void
cb_dialog_destroy (GtkDialog *dialog)
{
	g_object_set_data (G_OBJECT (dialog), "state", NULL);
}

/*****************************************************************************/

static void
c_fmt_dialog_set_sensitive (CFormatState *state)
{
	gboolean ok = (state->editor.style != NULL && state->homogeneous);
	GnmParsePos pp;
	GtkTreeIter iter;
	gboolean not_empty, selected;

	not_empty = gtk_tree_model_get_iter_first
		(GTK_TREE_MODEL (state->model), &iter);
	selected = gtk_tree_selection_get_selected
		(state->selection, NULL, NULL);

	gtk_widget_set_sensitive (GTK_WIDGET (state->clear), not_empty);

	gtk_widget_set_sensitive (GTK_WIDGET (state->remove),
				  state->homogeneous && selected);
	gtk_widget_set_sensitive (GTK_WIDGET (state->expand),
				  (!state->homogeneous) && selected);

	parse_pos_init_editpos (&pp, state->sv);

	if (ok && gtk_widget_get_sensitive (state->editor.expr_x)) {
		GnmExprTop const *texpr = gnm_expr_entry_parse (GNM_EXPR_ENTRY (state->editor.expr_x), &pp,
								NULL, FALSE,
								GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);
		ok = (texpr != NULL);
		if (texpr)
			gnm_expr_top_unref (texpr);
	}
	if (ok && gtk_widget_get_sensitive (state->editor.expr_y)) {
		GnmExprTop const *texpr = gnm_expr_entry_parse (GNM_EXPR_ENTRY (state->editor.expr_y), &pp,
								NULL, FALSE,
								GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);
		ok = (texpr != NULL);
		if (texpr)
			gnm_expr_top_unref (texpr);
	}

	gtk_widget_set_sensitive (state->editor.add_button, ok);
	gtk_widget_set_sensitive (state->editor.replace_button, ok && selected);
	gtk_widget_set_sensitive (state->editor.copy_button, selected && state->homogeneous);
}

static void
c_fmt_dialog_set_expr_sensitive (CFormatState *state)
{
	GtkTreeIter iter;
	gint n_expr = 0;

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->editor.combo), &iter))
		gtk_tree_model_get (GTK_TREE_MODEL (state->editor.typestore),
				    &iter,
				    2, &n_expr,
				    -1);
	if (n_expr < 1) {
		gtk_widget_set_sensitive (state->editor.expr_x, FALSE);
		gtk_entry_set_text (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->editor.expr_x)), "");
	} else
		gtk_widget_set_sensitive (state->editor.expr_x, TRUE);
	if (n_expr < 2) {
		gtk_widget_set_sensitive (state->editor.expr_y, FALSE);
		gtk_entry_set_text (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->editor.expr_y)), "");
	} else
		gtk_widget_set_sensitive (state->editor.expr_y, TRUE);
}

static void
cb_c_fmt_dialog_chooser_type_changed (G_GNUC_UNUSED GtkComboBox *widget, CFormatState *state)
{
	c_fmt_dialog_set_expr_sensitive (state);
	c_fmt_dialog_set_sensitive (state);
}

static gboolean
cb_c_fmt_dialog_chooser_entry_changed (G_GNUC_UNUSED GnmExprEntry *widget, G_GNUC_UNUSED GdkEvent *event,
				       CFormatState *state)
{
	c_fmt_dialog_set_sensitive (state);
	return FALSE;
}

void
dialog_cell_format_style_added (gpointer closure, GnmStyle *style)
{
	CFormatState *state = closure;

	if (state->editor.style)
		gnm_style_unref (state->editor.style);
	state->editor.style = style;
	gtk_label_set_text (GTK_LABEL (state->editor.style_label),
			    style ? _("(defined)") : _("(undefined)"));
	c_fmt_dialog_set_sensitive (state);
}

static void
c_fmt_dialog_set_component (CFormatState *state, GnmStyle *overlay, gchar const *name,
			    GnmStyleElement elem, gboolean uncheck)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, name);

	if (gnm_style_is_element_set (overlay, elem))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
	else if (uncheck)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), FALSE);
}


static gint
cb_c_fmt_dialog_chooser_check_page (CFormatState *state, gchar const *name,
				    gint page)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, name);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
		return (1 << page);
	else
		return 0;
}

static void
editor_destroy_cb (G_GNUC_UNUSED GObject *obj, CFormatState *state)
{
	state->editor.dialog = NULL;
}

static void
c_fmt_dialog_select_style (CFormatState *state, int pages)
{
	if (state->editor.dialog)
		gtk_widget_destroy (GTK_WIDGET (state->editor.dialog));
	state->editor.dialog = dialog_cell_format_select_style
		(state->wbcg, pages,
		 GTK_WINDOW (state->dialog),
		 state->editor.style, state);
	if (state->editor.dialog)
		g_signal_connect
			(G_OBJECT (state->editor.dialog),
			 "destroy", G_CALLBACK (editor_destroy_cb), state);
}

static void
cb_c_fmt_dialog_edit_style_button (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
	int pages = 0;
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-background", FD_BACKGROUND);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-number", FD_NUMBER);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-align", FD_ALIGNMENT);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-font", FD_FONT);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-border", FD_BORDER);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-protection", FD_PROTECTION);
	pages |= cb_c_fmt_dialog_chooser_check_page
		(state, "check-validation", FD_VALIDATION);

	if (state->editor.style != NULL)
		gnm_style_ref (state->editor.style);
	c_fmt_dialog_select_style (state, pages);
}

static GnmStyleCond *
c_fmt_dialog_get_condition (CFormatState *state)
{
	GnmStyleCondOp op;
	GnmStyleCond *cond;
	GtkTreeIter iter;
	gint n_expr = 0;
	GnmParsePos pp;
	GnmStyle *overlay;

	parse_pos_init_editpos (&pp, state->sv);

	overlay = gnm_style_new ();
	if (state->editor.style) {
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-background", FD_BACKGROUND)) {
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_COLOR_BACK);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_COLOR_PATTERN);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_PATTERN);
		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-number", FD_NUMBER)) {
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FORMAT);
		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-align", FD_ALIGNMENT)) {
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_ALIGN_V);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_ALIGN_H);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_INDENT);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_ROTATION);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_TEXT_DIR);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_WRAP_TEXT);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_SHRINK_TO_FIT);
		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-font", FD_FONT)) {
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_COLOR);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_NAME);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_BOLD);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_ITALIC);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_UNDERLINE);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_STRIKETHROUGH);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_SCRIPT);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_FONT_SIZE);
		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-border", FD_BORDER)) {
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_TOP);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_BOTTOM);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_LEFT);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_RIGHT);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_REV_DIAGONAL);
			gnm_style_merge_element (overlay, state->editor.style,
						 MSTYLE_BORDER_DIAGONAL);
		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-protection", FD_PROTECTION)) {

		}
		if (cb_c_fmt_dialog_chooser_check_page
		    (state, "check-validation", FD_VALIDATION)) {

		}
	}
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->editor.combo), &iter))
		gtk_tree_model_get (GTK_TREE_MODEL (state->editor.typestore),
				    &iter,
				    1, &op,
				    2, &n_expr,
				    -1);
	else
		op = GNM_STYLE_COND_CONTAINS_ERR;

	cond = gnm_style_cond_new (op, state->sheet);
	gnm_style_cond_set_overlay (cond, overlay);
	gnm_style_unref (overlay);

	if (n_expr > 0) {
		GnmExprTop const *texpr = gnm_expr_entry_parse (GNM_EXPR_ENTRY (state->editor.expr_x), &pp,
								NULL, FALSE,
								GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);
		gnm_style_cond_set_expr (cond, texpr, 0);
		gnm_expr_top_unref (texpr);
	}
	if (n_expr > 1) {
		GnmExprTop const *texpr = gnm_expr_entry_parse (GNM_EXPR_ENTRY (state->editor.expr_y), &pp,
								NULL, FALSE,
								GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);
		gnm_style_cond_set_expr (cond, texpr, 1);
		gnm_expr_top_unref (texpr);
	}
	return cond;
}

static void
cb_c_fmt_dialog_add_button (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
	GnmStyleCond *cond = c_fmt_dialog_get_condition (state);
	c_fmt_dialog_apply_add_choice (state, cond, TRUE);
	gnm_style_cond_free (cond);
}

static void
cb_c_fmt_dialog_replace_button (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
	GnmStyleCond *cond = c_fmt_dialog_get_condition (state);
	c_fmt_dialog_apply_add_choice (state, cond, FALSE);
	gnm_style_cond_free (cond);
}

static void
cb_c_fmt_dialog_copy_button (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
	GnmStyleConditions *sc;
	GtkTreeIter iter;
	sc = gnm_style_get_conditions (state->style);
	if (sc != NULL && gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
		GtkTreePath *path = gtk_tree_model_get_path
			(GTK_TREE_MODEL (state->model), &iter);
		gint *pind = gtk_tree_path_get_indices (path);
		GPtrArray const *conds = gnm_style_conditions_details (sc);
		if (pind && conds) {
			gint ind = *pind;
			GnmStyleCond *gsc = g_ptr_array_index (conds, ind);
			GtkTreeIter iter;
			GnmParsePos pp;
			GnmStyle   *style;
			GnmStyleConditions *conds;

			/* Set the condition op */
			if (gtk_tree_model_get_iter_first
			    (GTK_TREE_MODEL (state->editor.typestore), &iter)) {
				do {
					guint op;
					gtk_tree_model_get (GTK_TREE_MODEL (state->editor.typestore),
							    &iter,
							    1, &op,
							    -1);
					if (op == gsc->op) {
						gtk_combo_box_set_active_iter
							(GTK_COMBO_BOX (state->editor.combo), &iter);
						break;
					}
				} while (gtk_tree_model_iter_next
					 (GTK_TREE_MODEL (state->editor.typestore), &iter));
			}
			/* Set the expressions */
			parse_pos_init_editpos (&pp, state->sv);
			if (gnm_style_cond_get_expr (gsc, 0))
				gnm_expr_entry_load_from_expr (GNM_EXPR_ENTRY (state->editor.expr_x),
							       gnm_style_cond_get_expr (gsc, 0),
							       &pp);
			else
				gnm_expr_entry_load_from_text (GNM_EXPR_ENTRY (state->editor.expr_x),
							       "");
			if (gnm_style_cond_get_expr (gsc, 1))
				gnm_expr_entry_load_from_expr (GNM_EXPR_ENTRY (state->editor.expr_y),
							       gnm_style_cond_get_expr (gsc, 1),
							       &pp);
			else
				gnm_expr_entry_load_from_text (GNM_EXPR_ENTRY (state->editor.expr_y),
							       "");
			/* Set the style */
			conds = state->style
				? gnm_style_get_conditions (state->style)
				: NULL;
			if (conds)
				style = gnm_style_dup
					(gnm_style_get_cond_style (state->style, ind));
			else {
				style = gnm_style_new_default ();
				gnm_style_merge (style, gsc->overlay);
			}
			dialog_cell_format_style_added (state, style);
			/* Set the appl. style components */
			c_fmt_dialog_set_component (state, gsc->overlay, "check-background",
						    MSTYLE_COLOR_BACK, TRUE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-background",
						    MSTYLE_COLOR_PATTERN, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-background",
						    MSTYLE_PATTERN, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-number",
						    MSTYLE_FORMAT, TRUE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_ALIGN_V, TRUE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_ALIGN_H, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_ROTATION, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_INDENT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_TEXT_DIR, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_WRAP_TEXT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-align",
						    MSTYLE_SHRINK_TO_FIT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_COLOR, TRUE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_NAME, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_BOLD, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_ITALIC, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_UNDERLINE, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_STRIKETHROUGH, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_SCRIPT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-font",
						    MSTYLE_FONT_SIZE, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_TOP, TRUE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_BOTTOM, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_LEFT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_RIGHT, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_REV_DIAGONAL, FALSE);
			c_fmt_dialog_set_component (state, gsc->overlay, "check-border",
						    MSTYLE_BORDER_DIAGONAL, FALSE);
		}
		gtk_tree_path_free (path);
	}
}

static void
c_fmt_dialog_chooser_load_combo (CFormatState *state)
{
	static struct {
		char const *label;
		gint type;
		gint n_expressions;
	} cond_types[] = {
		/* without any expression */
		{ N_("Cell contains an error value."),                GNM_STYLE_COND_CONTAINS_ERR,         0},
		{ N_("Cell does not contain an error value."),        GNM_STYLE_COND_NOT_CONTAINS_ERR,     0},
		{ N_("Cell contains whitespace."),                    GNM_STYLE_COND_CONTAINS_BLANKS,      0},
		{ N_("Cell does not contain whitespace."),            GNM_STYLE_COND_NOT_CONTAINS_BLANKS,  0},
		/* with one expression */
		{ N_("Cell value is = x."),                           GNM_STYLE_COND_EQUAL,                1},
		{ N_("Cell value is \xe2\x89\xa0 x."),                GNM_STYLE_COND_NOT_EQUAL,            1},
		{ N_("Cell value is > x."),                           GNM_STYLE_COND_GT,                   1},
		{ N_("Cell value is < x."),                           GNM_STYLE_COND_LT,                   1},
		{ N_("Cell value is \xe2\x89\xa7 x."),                GNM_STYLE_COND_GTE,                  1},
		{ N_("Cell value is \xe2\x89\xa6 x."),                GNM_STYLE_COND_LTE,                  1},
		{ N_("Expression x evaluates to TRUE."),              GNM_STYLE_COND_CUSTOM,               1},
		{ N_("Cell contains the string x."),                  GNM_STYLE_COND_CONTAINS_STR,         1},
		{ N_("Cell does not contain the string x."),          GNM_STYLE_COND_NOT_CONTAINS_STR,     1},
		{ N_("Cell value begins with the string x."),         GNM_STYLE_COND_BEGINS_WITH_STR,      1},
		{ N_("Cell value does not begin with the string x."), GNM_STYLE_COND_NOT_BEGINS_WITH_STR,  1},
		{ N_("Cell value ends with the string x."),           GNM_STYLE_COND_ENDS_WITH_STR,        1},
		{ N_("Cell value does not end with the string x."),   GNM_STYLE_COND_NOT_ENDS_WITH_STR,    1},
		/* with two expressions */
		{ N_("Cell value is between x and y (incl.)."),               GNM_STYLE_COND_BETWEEN,              2},
		{ N_("Cell value is not between x and y (incl.)."),           GNM_STYLE_COND_NOT_BETWEEN,          2}
	};
	guint i;
	GtkCellRenderer  *cell;
	GtkTreeIter iter;

	for (i = 0; i < G_N_ELEMENTS (cond_types); i++)
		gtk_list_store_insert_with_values (state->editor.typestore,
						   NULL, G_MAXINT,
                                                   0, _(cond_types[i].label),
						   1, cond_types[i].type,
						   2, cond_types[i].n_expressions,
						   -1);
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(state->editor.combo), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(state->editor.combo), cell, "text", 0, NULL);
	if (gtk_tree_model_get_iter_first
	    (GTK_TREE_MODEL (state->editor.typestore), &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->editor.combo), &iter);

}

/*****************************************************************************/
/*****************************************************************************/
static gboolean
c_fmt_dialog_condition_setter (SheetView *sv, GnmRange const *range, CFormatState *state)
{
	GnmSheetRange *sr = g_new (GnmSheetRange, 1);
	sr->range = *range;
	sr->sheet = sv->sheet;
	state->action.redo = go_undo_combine
		(state->action.redo,
		 sheet_apply_style_undo (sr, state->action.new_style));
	sr = g_new (GnmSheetRange, 1);
	sr->range = *range;
	sr->sheet = sv->sheet;
	state->action.undo = go_undo_combine
		(sheet_apply_style_undo (sr, state->action.old_style),
		 state->action.undo);
	state->action.size++;
	return TRUE;
}

static gboolean
c_fmt_dialog_condition_setter_tiled (G_GNUC_UNUSED SheetView *sv, GnmRange const *range,
				     CFormatState *state)
{
	GnmStyleList *l, *list;
	if (state->action.existing_conds_only)
		list = sheet_style_collect_conditions (state->sheet, range);
	else
		list = sheet_style_get_range (state->sheet, range);
	for (l = list; l != NULL; l = l->next) {
		GnmStyleConditions *old_cond;
		GnmStyleRegion const *sr = l->data;
		GnmRange r  = *((GnmRange *) l->data);

		r.start.row += range->start.row;
		r.end.row += range->start.row;
		r.start.col += range->start.col;
		r.end.col += range->start.col;
		state->action.old_style = gnm_style_new ();
		if (gnm_style_is_element_set (sr->style, MSTYLE_CONDITIONS) &&
		    NULL != (old_cond = gnm_style_get_conditions (sr->style)))
			gnm_style_set_conditions (state->action.old_style,
						  g_object_ref (old_cond));
		else
			gnm_style_set_conditions (state->action.old_style, NULL);
		c_fmt_dialog_condition_setter (state->sv, &r, state);
		gnm_style_unref (state->action.old_style);
		state->action.old_style = NULL;
	}
	style_list_free (list);
	return TRUE;
}

static void
c_fmt_dialog_set_conditions (CFormatState *state, char const *cmd_label)
{
	GnmStyleConditions *old_cond;

	state->action.undo = NULL;
	state->action.redo = NULL;
	state->action.size = 0;

	if (state->homogeneous) {
		state->action.old_style = gnm_style_new ();
		old_cond = gnm_style_get_conditions (state->style);
		gnm_style_set_conditions (state->action.old_style,
					  old_cond ? g_object_ref (old_cond) : NULL);

		sv_selection_foreach (state->sv,
				      (GnmSelectionFunc)c_fmt_dialog_condition_setter,
				      state);
	} else {
		sv_selection_foreach (state->sv,
				      (GnmSelectionFunc)c_fmt_dialog_condition_setter_tiled,
				      state);
	}
	cmd_generic_with_size (GNM_WBC (state->wbcg), cmd_label,
			       state->action.size, state->action.undo, state->action.redo);

	state->action.undo = NULL;
	state->action.redo = NULL;
	if (state->action.old_style) {
		gnm_style_unref (state->action.old_style);
		state->action.old_style = NULL;
	}
}

static void
c_fmt_dialog_apply_add_choice (CFormatState *state, GnmStyleCond *cond, gboolean add)
{
	if (cond != NULL) {
		GnmStyleConditions *sc;
		int index = -1;
		sc = gnm_style_conditions_dup (gnm_style_get_conditions (state->style));
		if (sc == NULL)
			sc = gnm_style_conditions_new (state->sheet);
		if (!add) {
			GtkTreeIter iter;
			if (gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
				GtkTreePath *path = gtk_tree_model_get_path
					(GTK_TREE_MODEL (state->model), &iter);
				gint *ind = gtk_tree_path_get_indices (path);
				if (ind) {
					gnm_style_conditions_delete (sc, *ind);
					index = *ind;
				}
				gtk_tree_path_free (path);
			}
		}
		gnm_style_conditions_insert (sc, cond, index);
		state->action.new_style = gnm_style_new ();
		gnm_style_set_conditions (state->action.new_style, sc);
		state->action.existing_conds_only = FALSE;

		c_fmt_dialog_set_conditions (state, _("Set conditional formatting"));

		gnm_style_unref (state->action.new_style);
		state->action.new_style = NULL;

		c_fmt_dialog_load (state);
	}
}

static void
cb_c_fmt_dialog_clear_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	state->action.new_style = gnm_style_new ();
	gnm_style_set_conditions (state->action.new_style, NULL);
	state->action.existing_conds_only = TRUE;

	c_fmt_dialog_set_conditions (state, _("Clear conditional formatting"));

	gnm_style_unref (state->action.new_style);
	state->action.new_style = NULL;

	c_fmt_dialog_load (state);
}

static void
cb_c_fmt_dialog_remove_clicked (GtkButton *button, CFormatState *state)
{
	if (1 == gtk_tree_model_iter_n_children (GTK_TREE_MODEL (state->model), NULL))
		cb_c_fmt_dialog_clear_clicked (button, state);
	else {
		GtkTreeIter iter;
		if (gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
			GtkTreePath *path = gtk_tree_model_get_path
				(GTK_TREE_MODEL (state->model), &iter);
			gint *ind = gtk_tree_path_get_indices (path);
			if (ind) {
				GnmStyleConditions *sc;
				sc = gnm_style_conditions_dup
					(gnm_style_get_conditions (state->style));
				if (sc != NULL) {
					gnm_style_conditions_delete (sc, *ind);
					state->action.new_style = gnm_style_new ();
					gnm_style_set_conditions
						(state->action.new_style, sc);
					state->action.existing_conds_only = TRUE;

					c_fmt_dialog_set_conditions
						(state,
						 _("Remove condition from conditional "
						   "formatting"));

					gnm_style_unref (state->action.new_style);
					state->action.new_style = NULL;

					c_fmt_dialog_load (state);
				}
			}
			gtk_tree_path_free (path);
		}
	}
}

static void
cb_c_fmt_dialog_expand_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	GtkTreeIter iter;
	if (!state->homogeneous && gtk_tree_selection_get_selected (state->selection, NULL, &iter)) {
		GnmStyleConditions *sc;
		gtk_tree_model_get (GTK_TREE_MODEL (state->model),
				    &iter,
				    CONDITIONS_REFERENCE, &sc,
				    -1);
		if (sc != NULL) {
			state->action.new_style = gnm_style_new ();
			gnm_style_set_conditions
				(state->action.new_style, sc);
			state->action.existing_conds_only = FALSE;

			c_fmt_dialog_set_conditions
				(state,
				 _("Expand conditional formatting"));

			gnm_style_unref (state->action.new_style);
			state->action.new_style = NULL;

			c_fmt_dialog_load (state);
		}
	}
}

static void
c_fmt_dialog_conditions_page_load_cond_single_f (CFormatState *state,
					       GnmExprTop const *texpr, GtkTreeIter *iter1)
{
	char *formula;
	GnmParsePos pp;
	GtkTreeIter iter2;

	gtk_tree_store_append (state->model, &iter2, iter1);

	parse_pos_init_editpos (&pp, state->sv);

	formula = gnm_expr_top_as_string (texpr, &pp, gnm_conventions_default);
	gtk_tree_store_set (state->model, &iter2, CONDITIONS_RANGE, NULL,
			    CONDITIONS_COND, formula, CONDITIONS_REFERENCE, NULL, -1);
	g_free (formula);
}


static void
c_fmt_dialog_conditions_page_load_cond_double_f (CFormatState *state,
					       GnmStyleCond const *cond, GtkTreeIter *iter1)
{
	c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), iter1);
	c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 1), iter1);
}

static void
c_fmt_dialog_conditions_page_load_cond (CFormatState *state, GnmStyleCond const *cond,
					GtkTreeIter *iter)
{
	GtkTreeIter iter1;

	gtk_tree_store_append (state->model, &iter1, iter);

	switch (cond->op) {
	case GNM_STYLE_COND_BETWEEN:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is between these "
				      "two values, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_double_f (state, cond, &iter1);
		break;
	case GNM_STYLE_COND_NOT_BETWEEN:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is not between these"
				      " two values, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_double_f (state, cond, &iter1);
		break;
	case GNM_STYLE_COND_EQUAL:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is equal to this value"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_NOT_EQUAL:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is not equal to this value"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_GT:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is > this value, a "
				      "special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_LT:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is < this value, a "
				      "special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_GTE:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is \xe2\x89\xa7 this "
				      "value, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);

		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_LTE:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is \xe2\x89\xa6 this "
				      "value, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;

	case GNM_STYLE_COND_CUSTOM:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If this formula evaluates to TRUE, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_CONTAINS_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content contains this string"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not contain this string"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_BEGINS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content begins with this string"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not begin with this string,"
				      " a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_ENDS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content ends with this string"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not end  "
				      "with this string, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, gnm_style_cond_get_expr (cond, 0), &iter1);
		break;
	case GNM_STYLE_COND_CONTAINS_ERR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell contains an error "
				      "value, a special style is used."), -1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell does not contain an error value"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		break;
	case GNM_STYLE_COND_CONTAINS_BLANKS:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content "
				      "contains blanks, a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not contain blanks"
				      ", a special style is used."),
				    CONDITIONS_REFERENCE, NULL, -1);
		break;
	default:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("This is an unknown condition type."),
				    CONDITIONS_REFERENCE, NULL, -1);
		return;
	}
}

static void
c_fmt_dialog_conditions_page_load_conditions (GnmStyle *style, char const *range, CFormatState *state)
{
	GnmStyleConditions const *sc;
	GPtrArray const *conds;
	guint i;
	GtkTreeIter iter1, *iter;

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style)) &&
	    NULL != (conds = gnm_style_conditions_details (sc))) {
		if (range == NULL)
			iter = NULL;
		else {
			iter = &iter1;
			gtk_tree_store_append (state->model, iter, NULL);
			gtk_tree_store_set (state->model, iter, CONDITIONS_RANGE, range,
					    CONDITIONS_COND, NULL,
					    CONDITIONS_REFERENCE, sc, -1);
		}
		for (i = 0 ; i < conds->len ; i++)
			c_fmt_dialog_conditions_page_load_cond
				(state, g_ptr_array_index (conds, i), iter);
	}

}

static gboolean
c_fmt_dialog_condition_collector (G_GNUC_UNUSED SheetView *sv, GnmRange const *range,
				  gpointer user_data)
{
	CFormatState *state = user_data;
	GnmStyleList *l, *list = sheet_style_collect_conditions (state->sheet, range);

	for (l = list; l != NULL; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange r  = *((GnmRange *) l->data);
		r.start.row += range->start.row;
		r.end.row += range->start.row;
		r.start.col += range->start.col;
		r.end.col += range->start.col;
		c_fmt_dialog_conditions_page_load_conditions
			(sr->style, range_as_string (&r), state);
	}

	style_list_free (list);
	return TRUE;

}

static gboolean
c_fmt_dialog_selection_type (SheetView *sv,
			     GnmRange const *range,
			     gpointer user_data)
{
	GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX] = {NULL};
	CFormatState *state = user_data;
	int i;
	GSList *merged = gnm_sheet_merge_get_overlap (sv->sheet, range);
	GnmRange r = *range;
	gboolean allow_multi =
		merged == NULL ||
		merged->next != NULL ||
		!range_equal ((GnmRange *)merged->data, range);


	g_slist_free (merged);

	/* allow_multi == FALSE && !is_singleton (range) means that we are in
	 * an merge cell, use only the top left */
	if (!allow_multi) {
		if (r.start.col != r.end.col)
	   			r.end.col = r.start.col;
		if (range->start.row != range->end.row)
				r.end.row = r.start.row;
	}

	state->conflicts = sheet_style_find_conflicts (state->sheet, &r,
		&(state->style), borders);

	for (i = GNM_STYLE_BORDER_TOP ; i < GNM_STYLE_BORDER_EDGE_MAX ; i++) {
		gnm_style_border_unref (borders[i]);
	}

	return TRUE;
}

static void
c_fmt_dialog_load (CFormatState *state)
{
	gtk_tree_store_clear (state->model);
	if (state->style)
		gnm_style_unref (state->style);
	state->style = NULL;

	(void) sv_selection_foreach (state->sv,
		c_fmt_dialog_selection_type, state);

	state->homogeneous = !(state->conflicts & (1 << MSTYLE_CONDITIONS));

	if (state->homogeneous) {
		gtk_label_set_markup (state->label,
				      _("The selection is homogeneous with "
					 "respect to conditions."));
		if (state->style != NULL)
			c_fmt_dialog_conditions_page_load_conditions
				(state->style, NULL, state);
		gtk_tree_view_expand_all (state->treeview);
	} else {
		gtk_label_set_markup (state->label,
				      _("The selection is <b>not</b> "
					 "homogeneous "
					 "with respect to conditions!"));
		(void) sv_selection_foreach (state->sv,
					     c_fmt_dialog_condition_collector, state);
	}
	gtk_tree_view_column_queue_resize
		(gtk_tree_view_get_column (state->treeview, CONDITIONS_RANGE));
	c_fmt_dialog_set_sensitive (state);
}

static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *treeselection, CFormatState *state)
{
	c_fmt_dialog_set_sensitive (state);
}

static gboolean
cb_can_select (G_GNUC_UNUSED GtkTreeSelection *selection,
	       G_GNUC_UNUSED GtkTreeModel *model,
	       GtkTreePath *path,
	       gboolean path_currently_selected,
	       G_GNUC_UNUSED CFormatState *state)
{
	if (path_currently_selected)
		return TRUE;

	return (gtk_tree_path_get_depth (path) == 1);
}

static gboolean
cb_c_format_dialog_range (G_GNUC_UNUSED SheetView *sv, GnmRange const *range, GString *str)
{
	g_string_append (str, range_as_string (range));
	g_string_append (str, ", ");
	return TRUE;
}

static void
c_fmt_dialog_init_editor_page (CFormatState *state)
{
	GtkGrid  *grid;

	state->editor.add_button = go_gtk_builder_get_widget (state->gui, "add-button");
	state->editor.replace_button = go_gtk_builder_get_widget (state->gui, "replace-button");
	state->editor.copy_button = go_gtk_builder_get_widget (state->gui, "copy-button");
	state->editor.edit_style_button = go_gtk_builder_get_widget (state->gui, "edit-style-button");
	state->editor.combo = go_gtk_builder_get_widget (state->gui, "condition-combo");
	grid = GTK_GRID (go_gtk_builder_get_widget (state->gui, "condition-grid"));
	state->editor.expr_x = GTK_WIDGET (gnm_expr_entry_new (state->wbcg, TRUE));
	gtk_grid_attach (grid, state->editor.expr_x, 1, 2, 2, 1);
	gtk_widget_set_hexpand (state->editor.expr_x, TRUE);
	gtk_widget_show(state->editor.expr_x);
	gnm_expr_entry_set_flags (GNM_EXPR_ENTRY (state->editor.expr_x),
				  GNM_EE_SHEET_OPTIONAL |
				  GNM_EE_CONSTANT_ALLOWED,
				  GNM_EE_MASK);

	state->editor.expr_y = GTK_WIDGET (gnm_expr_entry_new (state->wbcg, TRUE));
	gtk_grid_attach (grid, state->editor.expr_y, 1, 3, 2, 1);
	gtk_widget_set_hexpand (state->editor.expr_y, TRUE);
	gtk_widget_show(state->editor.expr_y);
	gnm_expr_entry_set_flags (GNM_EXPR_ENTRY (state->editor.expr_y),
				  GNM_EE_SHEET_OPTIONAL |
				  GNM_EE_CONSTANT_ALLOWED,
				  GNM_EE_MASK);

	state->editor.typestore = GTK_LIST_STORE (gtk_combo_box_get_model
					   (GTK_COMBO_BOX (state->editor.combo)));
	c_fmt_dialog_chooser_load_combo (state);

	state->editor.style_label = go_gtk_builder_get_widget (state->gui, "style-label");
	gtk_label_set_text (GTK_LABEL (state->editor.style_label), _("(undefined)"));

	c_fmt_dialog_set_expr_sensitive (state);

	g_signal_connect (G_OBJECT (state->editor.add_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_add_button), state);
	g_signal_connect (G_OBJECT (state->editor.replace_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_replace_button), state);
	g_signal_connect (G_OBJECT (state->editor.copy_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_copy_button), state);
	g_signal_connect (G_OBJECT (state->editor.edit_style_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_edit_style_button), state);
	g_signal_connect (G_OBJECT (state->editor.combo),
		"changed",
		G_CALLBACK (cb_c_fmt_dialog_chooser_type_changed), state);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->editor.expr_x))),
		"focus-out-event",
		G_CALLBACK (cb_c_fmt_dialog_chooser_entry_changed), state);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (state->editor.expr_y))),
		"focus-out-event",
		G_CALLBACK (cb_c_fmt_dialog_chooser_entry_changed), state);

}

static void
c_fmt_dialog_init_conditions_page (CFormatState *state)
{
	GtkTreeViewColumn * column;
	GtkCellRenderer *renderer;
	GtkLabel *hl;
	GString *str;

	g_return_if_fail (state != NULL);

	state->remove = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_remove"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove), FALSE);
	state->clear = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_clear"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->clear), FALSE);
	state->expand = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_expand"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->expand), FALSE);

	state->model = gtk_tree_store_new (CONDITIONS_NUM_COLUMNS,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_OBJECT);
	state->treeview = GTK_TREE_VIEW (go_gtk_builder_get_widget
					 (state->gui, "conditions_treeview"));
	gtk_tree_view_set_fixed_height_mode (state->treeview, FALSE);
	gtk_tree_view_set_model (state->treeview, GTK_TREE_MODEL (state->model));
	g_object_unref (state->model);
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_SINGLE);
	gtk_tree_selection_set_select_function (state->selection,
						(GtkTreeSelectionFunc) cb_can_select,
						state, NULL);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes
		("Range", renderer, "text", CONDITIONS_RANGE, NULL);
	gtk_tree_view_insert_column (state->treeview, column, -1);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes
		("Conditions", renderer, "text", CONDITIONS_COND, NULL);
	gtk_tree_view_insert_column (state->treeview, column, -1);
	gtk_tree_view_set_expander_column (state->treeview, column);

	state->label = GTK_LABEL (go_gtk_builder_get_widget (state->gui,
								   "conditions_label"));
	hl = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "header-label"));
	gtk_label_set_ellipsize (hl, PANGO_ELLIPSIZE_END);
	str = g_string_new (_("Editing conditional formatting: "));
	sv_selection_foreach (state->sv,
			      (GnmSelectionFunc)cb_c_format_dialog_range,
			      str);
	g_string_truncate (str, str->len -2);
	gtk_label_set_text(hl, str->str);
	g_string_free (str, TRUE);

	g_signal_connect (G_OBJECT (state->selection), "changed",
			  G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (G_OBJECT (state->remove), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_remove_clicked), state);
	g_signal_connect (G_OBJECT (state->clear), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_clear_clicked), state);
	g_signal_connect (G_OBJECT (state->expand), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_expand_clicked), state);
}

/*****************************************************************************/


void
dialog_cell_format_cond (WBCGtk *wbcg)
{
	GtkBuilder     *gui;
	CFormatState  *state;
	GtkWidget *dialog;

	g_return_if_fail (wbcg != NULL);

	gui = gnm_gtk_builder_load ("res:ui/cell-format-cond.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (CFormatState, 1);
	state->wbcg	= wbcg;
	state->gui	= gui;
	state->sv	= wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet	= sv_sheet (state->sv);
	state->style	= NULL;
	state->editor.style = NULL;
	state->editor.dialog = NULL;

	dialog = go_gtk_builder_get_widget (state->gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Conditional Cell Formatting"));

	/* Initialize */
	state->dialog	   = GTK_DIALOG (dialog);

	c_fmt_dialog_init_conditions_page (state);
	c_fmt_dialog_init_editor_page (state);

	c_fmt_dialog_load (state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "helpbutton"),
		GNUMERIC_HELP_LINK_CELL_FORMAT_COND);

	state->close_button = go_gtk_builder_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (state->close_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_dialog_buttons), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog), state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	/* a candidate for merging into attach guru */
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_c_fmt_dialog_dialog_destroy);
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (cb_dialog_destroy), NULL);

	gnm_restore_window_geometry (GTK_WINDOW (state->dialog),
					  CELL_FORMAT_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));

	gtk_widget_show (GTK_WIDGET (state->dialog));
}
