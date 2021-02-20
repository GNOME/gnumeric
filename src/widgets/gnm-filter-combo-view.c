/*
 * gnm-filter-combo-view.c: the autofilter combo box for the goffice canvas
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
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
#include <widgets/gnm-filter-combo-view.h>
#include <widgets/gnm-cell-combo-view-impl.h>

#include <gnumeric.h>
#include <sheet-filter.h>
#include <sheet-filter-combo.h>
#include <gnm-format.h>
#include <value.h>
#include <cell.h>
#include <commands.h>
#include <sheet.h>
#include <sheet-object-impl.h>
#include <wbc-gtk.h>
#include <workbook.h>
#include <style-color.h>
#include <sheet-control-gui.h>
#include <dialogs/dialogs.h>
#include <wbc-gtk-impl.h>
#include <undo.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

static gboolean
fcombo_activate (SheetObject *so, GtkTreeView *list, WBCGtk *wbcg,
		 G_GNUC_UNUSED gboolean button)
{
	GnmFilterCombo *fcombo = GNM_FILTER_COMBO (so);
	GtkTreeIter     iter;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list), NULL, &iter)) {
		GnmFilterCondition *cond = NULL;
		gboolean  set_condition = TRUE;
		GnmValue *v;
		int	  field_num, type;

		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
				    2, &type, 3, &v,
				    -1);

		field_num = gnm_filter_combo_index (fcombo);
		switch (type) {
		case  0:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_EQUAL, v);
			break;
		case  1: /* unfilter */
			cond = NULL;
			break;
		case  2: /* Custom */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, fcombo->filter, field_num,
					    TRUE, fcombo->cond);
			break;
		case  3:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_BLANKS, NULL);
			break;
		case  4:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_NON_BLANKS, NULL);
			break;
		case 10: /* Top 10 */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, fcombo->filter, field_num,
					    FALSE, fcombo->cond);
			break;
		default:
			set_condition = FALSE;
			g_warning ("Unknown type %d", type);
		}

		if (set_condition)
			cmd_autofilter_set_condition
				(GNM_WBC (wbcg),
				 fcombo->filter, field_num, cond);
	}
	return TRUE;
}

typedef struct {
	gboolean has_blank;
	GHashTable *hash;
	GODateConventions const *date_conv;
	Sheet *src_sheet;
} UniqueCollection;

static GnmValue *
cb_collect_content (GnmCellIter const *iter, UniqueCollection *uc)
{
	GnmCell const *cell = (iter->pp.sheet == uc->src_sheet) ? iter->cell
		: sheet_cell_get (uc->src_sheet,
			iter->pp.eval.col, iter->pp.eval.row);

	if (gnm_cell_is_blank (cell))
		uc->has_blank = TRUE;
	else {
		GOFormat const *fmt = gnm_cell_get_format (cell);
		GnmValue const *v   = cell->value;
		g_hash_table_replace (uc->hash,
			value_dup (v),
			format_value (fmt, v, -1, uc->date_conv));
	}

	return NULL;
}

static void
cb_hash_domain (GnmValue *key, gpointer value, gpointer accum)
{
	g_ptr_array_add (accum, key);
}

/* Distinguish between the same value with different formats */
static gboolean
formatted_value_equal (GnmValue const *a, GnmValue const *b)
{
	return value_equal (a, b) && (VALUE_FMT(a) == VALUE_FMT(b));
}

static GtkWidget *
fcombo_create_list (SheetObject *so,
		    GtkTreePath **clip, GtkTreePath **select, gboolean *make_buttons)
{
	GnmFilterCombo  *fcombo = GNM_FILTER_COMBO (so);
	GnmFilter const *filter = fcombo->filter;
	GnmRange	 r = filter->r;
	Sheet		*filtered_sheet;
	UniqueCollection uc;
	GtkTreeIter	 iter;
	GtkListStore *model;
	GtkWidget    *list;
	GPtrArray    *sorted = g_ptr_array_new ();
	unsigned i, field_num = gnm_filter_combo_index (fcombo);
	gboolean is_custom = FALSE;
	GnmValue const *v;
	GnmValue const *cur_val = NULL;

	model = gtk_list_store_new (4,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, gnm_value_get_type ());

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(All)"),	   1, NULL, 2, 1, -1);
	if (fcombo->cond == NULL || fcombo->cond->op[0] == GNM_FILTER_UNUSED)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Top 10...)"),     1, NULL, 2, 10,-1);
	if (fcombo->cond != NULL &&
	    (GNM_FILTER_OP_TYPE_MASK & fcombo->cond->op[0]) == GNM_FILTER_OP_TOP_N)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	/* default to this we can easily revamp later */
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Custom...)"),     1, NULL, 2, 2, -1);
	if (*select == NULL) {
		is_custom = TRUE;
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	}

	r.start.row++;
	/* r.end.row =  XL actually extend to the first non-empty element in the list */
	r.end.col = r.start.col += field_num;
	uc.has_blank = FALSE;
	uc.hash = g_hash_table_new_full ((GHashFunc)value_hash, (GEqualFunc)formatted_value_equal,
		(GDestroyNotify)value_release, (GDestroyNotify)g_free);
	uc.src_sheet = filter->sheet;
	uc.date_conv = sheet_date_conv (uc.src_sheet);

	/* We do not want to show items that are filtered by _other_ fields.
	 * The cleanest way to do that is to create a temporary sheet, apply
	 * all of the other conditions to it and use that as the source of visibility. */
	if (filter->fields->len > 1) {
		Workbook *wb = uc.src_sheet->workbook;
		char *name = workbook_sheet_get_free_name (wb, "DummyFilterPopulate", FALSE, FALSE);
		filtered_sheet = sheet_new (wb, name,
					    gnm_sheet_get_max_cols (uc.src_sheet),
					    gnm_sheet_get_max_rows (uc.src_sheet));
		g_free (name);
		for (i = 0 ; i < filter->fields->len ; i++)
			if (i != field_num)
				gnm_filter_combo_apply (g_ptr_array_index (filter->fields, i),
							filtered_sheet);
		sheet_foreach_cell_in_range (filtered_sheet,
					     CELL_ITER_IGNORE_HIDDEN,
					     &r,
					     (CellIterFunc)&cb_collect_content, &uc);
		g_object_unref (filtered_sheet);
	} else
		sheet_foreach_cell_in_range (filter->sheet, CELL_ITER_ALL,
					     &r,
					     (CellIterFunc)&cb_collect_content, &uc);

	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	g_ptr_array_sort (sorted, value_cmp);

	if (fcombo->cond != NULL &&
	    fcombo->cond->op[0] == GNM_FILTER_OP_EQUAL &&
	    fcombo->cond->op[1] == GNM_FILTER_UNUSED) {
		cur_val = fcombo->cond->value[0];
	}

	for (i = 0; i < sorted->len ; i++) {
		char *label = NULL;
		unsigned const max = 50;
		char const *str = g_hash_table_lookup (uc.hash,
			(v = g_ptr_array_index (sorted, i)));
		gsize len = g_utf8_strlen (str, -1);

		if (len > max + 3) {
			label = g_strdup (str);
			strcpy (g_utf8_offset_to_pointer (label, max), "...");
		}

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    0, label ? label : str, /* Menu text */
				    1, str, /* Actual string selected on.  */
				    2, 0,
				    3, v,
				    -1);
		g_free (label);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		if (cur_val != NULL && v != NULL && value_equal	(cur_val, v)) {
			gtk_tree_path_free (*select);
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		}
	}

	if (uc.has_blank) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Blanks...)"),	   1, NULL, 2, 3, -1);
		if (fcombo->cond != NULL &&
		    fcombo->cond->op[0] == GNM_FILTER_OP_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Non Blanks...)"), 1, NULL, 2, 4, -1);
		if (fcombo->cond != NULL &&
		    fcombo->cond->op[0] == GNM_FILTER_OP_NON_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	} else if (is_custom && fcombo->cond != NULL &&
		   (GNM_FILTER_OP_TYPE_MASK & fcombo->cond->op[0]) == GNM_FILTER_OP_BLANKS) {
		gtk_tree_path_free (*select);
		*select = NULL;
	}

	g_hash_table_destroy (uc.hash);
	g_ptr_array_free (sorted, TRUE);

	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL));
	return list;
}

static void
fcombo_arrow_format (GnmFilterCombo *fcombo, GtkWidget *arrow)
{
	gboolean is_active = (fcombo->cond != NULL);
	GtkWidget *button = gtk_widget_get_parent (arrow);

	if (button) {
		char *desc = NULL;
		if (is_active) {
			// Presumably we could cook up a description of the filter
			// here.
		}
		if (desc) {
			gtk_widget_set_tooltip_text (button, desc);
			g_free (desc);
		}
	}

	if (is_active)
		gtk_widget_set_state_flags (arrow, GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_CHECKED, FALSE);
	else
		gtk_widget_unset_state_flags (arrow, GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_CHECKED);
}

static gboolean
fcombo_draw_arrow (GtkWidget *widget, cairo_t *cr, gpointer data)
{
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	guint width = gtk_widget_get_allocated_width (widget);
	guint height = gtk_widget_get_allocated_height (widget);
	gtk_render_background (context, cr, 0, 0, width, height);
	gtk_render_expander (context, cr, 0, 0, width, height);
	return FALSE;
}

static GtkWidget *
fcombo_create_arrow (SheetObject *so)
{
	GnmFilterCombo *fcombo = GNM_FILTER_COMBO (so);
	GtkWidget *arrow = gtk_drawing_area_new ();
	g_signal_connect (G_OBJECT (arrow), "draw",
			  G_CALLBACK (fcombo_draw_arrow), NULL);
	gtk_style_context_add_class (gtk_widget_get_style_context (arrow),
				     "auto-filter");
	fcombo_arrow_format (fcombo, arrow);
	g_signal_connect_object (G_OBJECT (so),
		"cond-changed",
		G_CALLBACK (fcombo_arrow_format), arrow, 0);
	return arrow;
}

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
filter_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocGroup *view = GOC_GROUP (sov);

	if (visible) {
		double scale = goc_canvas_get_pixels_per_unit (GOC_ITEM (view)->canvas);
		double h = (coords[3] - coords[1]) + 1.;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		h /= scale;
		goc_item_set (sheet_object_view_get_item (sov),
			/* put it inside the cell */
			"x",	  ((coords[2] >= 0.) ? (coords[2] / scale - h + 1) : coords[0] / scale),
			"y",	  coords [3] / scale - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);

		goc_item_show (GOC_ITEM (view));
	} else
		goc_item_hide (GOC_ITEM (view));
}

static void
gnm_filter_view_class_init (GnmCComboViewClass *ccombo_class)
{
	SheetObjectViewClass *sov_class = (SheetObjectViewClass *) ccombo_class;
	ccombo_class->create_list	= fcombo_create_list;
	ccombo_class->create_arrow	= fcombo_create_arrow;
	ccombo_class->activate		= fcombo_activate;
	sov_class->set_bounds		= filter_view_set_bounds;
}

/****************************************************************************/

typedef GnmCComboView		GnmFilterComboView;
typedef GnmCComboViewClass	GnmFilterComboViewClass;
GSF_CLASS (GnmFilterComboView, gnm_filter_combo_view,
	gnm_filter_view_class_init, NULL,
	GNM_CCOMBO_VIEW_TYPE)

