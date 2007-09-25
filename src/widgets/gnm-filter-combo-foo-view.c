/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-filter-combo.c: the autofilter combo box for FooCanvas
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "gnm-filter-combo-foo-view.h"
#include "gnm-cell-combo-foo-view-impl.h"

#include "gui-gnumeric.h"
#include "sheet-filter.h"
#include "sheet-filter-combo.h"
#include "gnm-format.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "style-color.h"
#include "sheet-control-gui.h"
#include "../dialogs/dialogs.h"

#include <goffice/utils/regutf8.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtktreeselection.h>
#include <glib/gi18n-lib.h>
#include <string.h>

static int
fcombo_index (GnmFilterCombo const *fcombo)
{
	return fcombo->parent.anchor.cell_bound.start.col
		- fcombo->filter->r.start.col;
}

static void
fcombo_activate (SheetObject *so, GtkWidget *popup, GtkTreeView *list,
		 WBCGtk *wbcg)
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

		field_num = fcombo_index (fcombo);
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

		if (set_condition) {
			gnm_filter_set_condition (fcombo->filter, field_num,
				cond, TRUE);
			sheet_update (fcombo->filter->sheet);
		}
	}
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
			format_value (fmt, v, NULL, -1, uc->date_conv));
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

static GtkListStore *
fcombo_fill_model (SheetObject *so,  GtkTreePath **clip, GtkTreePath **select)
{
	GnmFilterCombo  *fcombo = GNM_FILTER_COMBO (so);
	GnmFilter const *filter = fcombo->filter;
	GnmRange	 r = filter->r;
	Sheet 		*filtered_sheet;
	UniqueCollection uc;
	GtkTreeIter	 iter;
	GtkListStore *model;
	GPtrArray    *sorted = g_ptr_array_new ();
	unsigned i, field_num = fcombo_index (fcombo);
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
	uc.date_conv = workbook_date_conv (uc.src_sheet->workbook);

	/* We do not want to show items that are filtered by _other_ fields.
	 * The cleanest way to do that is to create a temporary sheet, apply
	 * all of the other conditions to it and use that as the source of visibility. */
	if (filter->fields->len > 1) {
		filtered_sheet = sheet_new (uc.src_sheet->workbook, "_DummyFilterPopulate");
		for (i = 0 ; i < filter->fields->len ; i++)
			if (i != field_num)
				gnm_filter_combo_apply (g_ptr_array_index (filter->fields, i),
							filtered_sheet);
		sheet_foreach_cell_in_range (filtered_sheet, CELL_ITER_IGNORE_HIDDEN,
			r.start.col, r.start.row, r.end.col, r.end.row,
			(CellIterFunc)&cb_collect_content, &uc);
		g_object_unref (filtered_sheet);
	} else
		sheet_foreach_cell_in_range (filter->sheet, CELL_ITER_ALL,
			r.start.col, r.start.row, r.end.col, r.end.row,
			(CellIterFunc)&cb_collect_content, &uc);

	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (char *),
	       &value_cmp);

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

	return model;
}

static void
fcombo_arrow_format (GnmFilterCombo *fcombo, GtkWidget *arrow)
{
	if (NULL != arrow->parent) {
		char *desc = NULL;
		if (NULL != fcombo->cond) {
		}
		if (desc) {
			go_widget_set_tooltip_text (arrow->parent, desc);
			g_free (desc);
		}
	}

	gtk_arrow_set (GTK_ARROW (arrow),
		fcombo->cond != NULL ? GTK_ARROW_RIGHT : GTK_ARROW_DOWN,
		GTK_SHADOW_IN);
	gtk_widget_modify_fg (arrow, GTK_STATE_NORMAL,
		fcombo->cond != NULL ? &gs_yellow : &gs_black);
}

static GtkWidget *
fcombo_create_arrow (SheetObject *so)
{
	GnmFilterCombo *fcombo = GNM_FILTER_COMBO (so);
	GtkWidget *arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
	fcombo_arrow_format (fcombo, arrow);
	g_signal_connect_object (G_OBJECT (so),
		"cond-changed",
		G_CALLBACK (fcombo_arrow_format), arrow, 0);
	return arrow;
}

static void
fcombo_ccombo_init (GnmCComboFooViewIface *ccombo_iface)
{
	ccombo_iface->fill_model	= fcombo_fill_model;
	ccombo_iface->activate		= fcombo_activate;
	ccombo_iface->create_arrow	= fcombo_create_arrow;
}

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
filter_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		double h = (coords[3] - coords[1]) + 1.;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		foo_canvas_item_set (view,
			/* put it inside the cell */
			"x",	  ((coords[2] >= 0.) ? (coords[2]-h+1) : coords[0]),
			"y",	  coords [3] - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}
static void filter_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
fcombo_sov_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= filter_view_destroy;
	sov_iface->set_bounds	= filter_view_set_bounds;
}

/****************************************************************************/

typedef FooCanvasWidget		GnmFilterComboFooView;
typedef FooCanvasWidgetClass	GnmFilterComboFooViewClass;
GSF_CLASS_FULL (GnmFilterComboFooView, gnm_filter_combo_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (fcombo_sov_init, SHEET_OBJECT_VIEW_TYPE)
	GSF_INTERFACE (fcombo_ccombo_init, GNM_CCOMBO_FOO_VIEW_TYPE)
)
