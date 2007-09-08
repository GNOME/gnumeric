/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-validation-combo-foo-view.c: A foocanvas object for Validate from list
 * 				in cell combos
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
#include "gnm-validation-combo-foo-view.h"
#include "gnm-cell-combo-foo-view-impl.h"

#include "validation-combo.h"
#include "commands.h"
#include "gnm-format.h"
#include "workbook-control.h"
#include "workbook.h"
#include "sheet-control-gui.h"
#include "sheet-view.h"
#include "sheet.h"
#include "cell.h"
#include "expr-impl.h"
#include "expr.h"
#include "value.h"

#include "gui-gnumeric.h"
#include <goffice/utils/regutf8.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtktreeselection.h>
#include <glib/gi18n-lib.h>
#include <string.h>

static void
vcombo_activate (SheetObject *so, GtkWidget *popup, GtkTreeView *list,
		 WBCGtk *wbcg)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);
	GtkTreeIter	    iter;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list), NULL, &iter)) {
		SheetView	*sv  = vcombo->sv;
		char		*strval;
		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
			1, &strval,
			-1);
		cmd_set_text (WORKBOOK_CONTROL (wbcg),
			sv_sheet (sv), &sv->edit_pos, strval, NULL);
		g_free (strval);
	}
}

typedef struct {
	GHashTable *hash;
	GODateConventions const *date_conv;
} UniqueCollection;

static GnmValue *
cb_collect_unique (GnmValueIter const *iter, UniqueCollection *uc)
{
	GOFormat const *fmt = (NULL != iter->cell_iter)
		? gnm_cell_get_format (iter->cell_iter->cell) : NULL;
	g_hash_table_replace (uc->hash,
		value_dup (iter->v),
		format_value (fmt, iter->v, NULL, -1, uc->date_conv));
	return NULL;
}

static void
cb_hash_domain (GnmValue *key, gpointer value, gpointer accum)
{
	g_ptr_array_add (accum, key);
}

static GtkListStore *
vcombo_fill_model (SheetObject *so, GtkTreePath **clip, GtkTreePath **select)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);
	unsigned	 i;
	UniqueCollection uc;
	GnmEvalPos	 ep;
	GtkTreeIter	 iter;
	GPtrArray	*sorted;
	GtkListStore	*model;
	GnmValue	*v;
	GnmValue const	*cur_val;
	GnmValidation const *val = vcombo->validation;
	GnmExprArrayCorner array = { GNM_EXPR_OP_ARRAY_CORNER, 1, 1, NULL, NULL };

	model = gtk_list_store_new (3,
		G_TYPE_STRING, G_TYPE_STRING, gnm_value_get_type ());

	g_return_val_if_fail (val != NULL, model);
	g_return_val_if_fail (val->type == VALIDATION_TYPE_IN_LIST, model);
	g_return_val_if_fail (val->texpr[0] != NULL, model);
	g_return_val_if_fail (vcombo->sv != NULL, model);

	eval_pos_init_pos (&ep, sv_sheet (vcombo->sv), &vcombo->sv->edit_pos);
	/* Force into 'array' mode by supplying a fake corner */
	ep.array = &array;
	v = gnm_expr_top_eval (val->texpr[0], &ep,
		 GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
	if (NULL == v)
		return model;

	uc.date_conv = workbook_date_conv (vcombo->parent.sheet->workbook);
	uc.hash = g_hash_table_new_full ((GHashFunc)value_hash, (GEqualFunc)value_equal,
		(GDestroyNotify)value_release, (GDestroyNotify)g_free);
	value_area_foreach (v, &ep, CELL_ITER_IGNORE_BLANK,
		 (GnmValueIterFunc) cb_collect_unique, &uc);
	value_release (v);

	sorted = g_ptr_array_new ();
	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (char *),
	       &value_cmp);

	cur_val = sheet_cell_get_value (ep.sheet, ep.eval.col, ep.eval.row);
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
				    -1);
		g_free (label);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		if (cur_val != NULL && v != NULL && value_equal	(cur_val, v)) {
			gtk_tree_path_free (*select);
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		}
	}

	g_hash_table_destroy (uc.hash);
	g_ptr_array_free (sorted, TRUE);

	return model;
}

static GtkWidget *
vcombo_create_arrow (G_GNUC_UNUSED SheetObject *so)
{
	return gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
}

static void
vcombo_ccombo_init (GnmCComboFooViewIface *ccombo_iface)
{
	ccombo_iface->fill_model	= vcombo_fill_model;
	ccombo_iface->activate		= vcombo_activate;
	ccombo_iface->create_arrow	= vcombo_create_arrow;
}

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
vcombo_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		double h = (coords[3] - coords[1]) + 1.;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		foo_canvas_item_set (view,
			/* put it outside the cell */
			"x",	  ((coords[2] >= 0.) ? coords[2] : (coords[0]-h+1.)),
			"y",	  coords [3] - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}
static void
vcombo_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
vcombo_sov_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= vcombo_destroy;
	sov_iface->set_bounds	= vcombo_set_bounds;
}

/****************************************************************************/

typedef FooCanvasWidget		GnmValidationComboFooView;
typedef FooCanvasWidgetClass	GnmValidationComboFooViewClass;
GSF_CLASS_FULL (GnmValidationComboFooView, gnm_validation_combo_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (vcombo_sov_init, SHEET_OBJECT_VIEW_TYPE)
	GSF_INTERFACE (vcombo_ccombo_init, GNM_CCOMBO_FOO_VIEW_TYPE)
)
