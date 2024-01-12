/*
 * gnm-validation-combo-view.c: A canvas object for Validate from list
 * 				in cell combos
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
#include <widgets/gnm-validation-combo-view.h>
#include <widgets/gnm-cell-combo-view-impl.h>

#include <validation-combo.h>
#include <commands.h>
#include <gnm-format.h>
#include <workbook-control.h>
#include <workbook.h>
#include <sheet-control-gui.h>
#include <sheet-view.h>
#include <sheet.h>
#include <cell.h>
#include <expr.h>
#include <value.h>

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

static gboolean
vcombo_activate (SheetObject *so, GtkTreeView *list, WBCGtk *wbcg,
		 G_GNUC_UNUSED gboolean button)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);
	GtkTreeIter	    iter;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list), NULL, &iter)) {
		SheetView	*sv  = vcombo->parent.sv;
		char		*strval;
		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
			1, &strval,
			-1);
		cmd_set_text (GNM_WBC (wbcg),
			      sv_sheet (sv), &sv->edit_pos, strval, NULL, TRUE);
		g_free (strval);
	}
	return TRUE;
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
		format_value (fmt, iter->v, -1, uc->date_conv));
	return NULL;
}

static void
cb_hash_domain (GnmValue *key, gpointer value, gpointer accum)
{
	g_ptr_array_add (accum, key);
}

static GtkWidget *
vcombo_create_list (SheetObject *so,
		    GtkTreePath **clip, GtkTreePath **select, gboolean *make_buttons)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (so);
	unsigned	 i;
	UniqueCollection uc;
	GnmEvalPos	 ep;
	GtkTreeIter	 iter;
	GtkWidget	*list;
	GPtrArray	*sorted;
	GtkListStore	*model;
	GnmValue	*v;
	GnmValue const	*cur_val;
	GnmValidation const *val = vcombo->validation;
	SheetView const *sv = vcombo->parent.sv;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (val->type == GNM_VALIDATION_TYPE_IN_LIST, NULL);
	g_return_val_if_fail (val->deps[0].base.texpr != NULL, NULL);
	g_return_val_if_fail (sv != NULL, NULL);

	eval_pos_init_editpos (&ep, sv);
	v = gnm_expr_top_eval_fake_array (val->deps[0].base.texpr, &ep,
					  GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
					  GNM_EXPR_EVAL_PERMIT_EMPTY);
	if (NULL == v)
		return NULL;

	uc.date_conv = sheet_date_conv (sv->sheet);
	uc.hash = g_hash_table_new_full ((GHashFunc)value_hash, (GEqualFunc)value_equal,
		(GDestroyNotify)value_release, (GDestroyNotify)g_free);
	value_area_foreach (v, &ep, CELL_ITER_IGNORE_BLANK,
		 (GnmValueIterFunc) cb_collect_unique, &uc);
	value_release (v);

	sorted = g_ptr_array_new ();
	g_hash_table_foreach (uc.hash, (GHFunc)cb_hash_domain, sorted);
	g_ptr_array_sort (sorted, value_cmp);

	model = gtk_list_store_new (3,
		G_TYPE_STRING, G_TYPE_STRING, gnm_value_get_type ());

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

	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL));
	return list;
}

static GtkWidget *
vcombo_create_arrow (G_GNUC_UNUSED SheetObject *so)
{
	return gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
}

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
vcombo_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocGroup *view = GOC_GROUP (sov);

	if (visible) {
		double scale = goc_canvas_get_pixels_per_unit (GOC_ITEM (view)->canvas);
		double h = (coords[3] - coords[1]) + 1.;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		h /= scale;
		goc_item_set (sheet_object_view_get_item (sov),
			/* put it outside the cell */
			"x",	  ((coords[2] >= 0.)? coords[2] / scale: (coords[0] / scale - h + 1.)),
			"y",	  coords [3] / scale - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);
		goc_item_show (GOC_ITEM (view));
	} else
		goc_item_hide (GOC_ITEM (view));
}

/****************************************************************************/

static void
gnm_validation_view_class_init (GnmCComboViewClass *ccombo_class)
{
	SheetObjectViewClass *sov_class = (SheetObjectViewClass *) ccombo_class;
	ccombo_class->create_list	= vcombo_create_list;
	ccombo_class->create_arrow	= vcombo_create_arrow;
	ccombo_class->activate		= vcombo_activate;
	sov_class->set_bounds		= vcombo_set_bounds;
}

typedef GnmCComboView		GnmValidationComboView;
typedef GnmCComboViewClass	GnmValidationComboViewClass;
GSF_CLASS (GnmValidationComboView, gnm_validation_combo_view,
	gnm_validation_view_class_init, NULL,
	GNM_CCOMBO_VIEW_TYPE)

