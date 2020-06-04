/*
 * gnm-sheet-slicer-combo-view.c: A canvas object for data slicer field combos
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
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
#include <widgets/gnm-sheet-slicer-combo-view.h>
#include <widgets/gnm-cell-combo-view-impl.h>

#include <gnm-sheet-slicer-combo.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <value.h>

#include <gnumeric.h>
#include <go-data-slicer-field.h>
#include <go-data-cache-field.h>
#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

enum {
	SSCOMBO_COL_FILTERED,
	SSCOMBO_COL_NAME,
	SSCOMBO_COL_LAST
};

static gboolean
sscombo_activate (SheetObject *so, GtkTreeView *list, WBCGtk *wbcg, gboolean button)
{
	GtkTreeIter	    iter;

	if (!button)
		return FALSE;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list), NULL, &iter)) {
		char		*strval;
		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
			1, &strval,
			-1);
#if 0
	GnmSheetSlicerCombo *sscombo = GNM_SHEET_SLICER_COMBO (so);
		SheetView	*sv  = sscombo->sv;
		cmd_set_text (GNM_WBC (wbcg),
			      sv_sheet (sv), &sv->edit_pos, strval, NULL, TRUE);
#endif
		g_free (strval);
	}
	return TRUE;
}

static void
cb_filter_toggle (GtkCellRendererToggle *cell,
		  const gchar *path_str,
		  gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter  iter;
	GtkTreePath  *path = gtk_tree_path_new_from_string (path_str);
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gboolean val;
		gtk_tree_model_get (model, &iter, SSCOMBO_COL_FILTERED, &val, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SSCOMBO_COL_FILTERED, val ^ 1, -1);
	}
	gtk_tree_path_free (path);
}

static GtkWidget *
sscombo_create_list (SheetObject *so,
		     GtkTreePath **clip, GtkTreePath **select, gboolean *make_buttons)
{
	GnmSheetSlicerCombo *sscombo = GNM_SHEET_SLICER_COMBO (so);
	GODataCacheField const *dcf  = go_data_slicer_field_get_cache_field (sscombo->dsf);
	GtkListStore	*model;
	GtkTreeIter	 iter;
	GtkCellRenderer *renderer;
	GtkWidget	*list;
	GOValArray const*vals;
	GOVal const	*v;
	GString		*str;
	unsigned i;
	GODateConventions const *dconv = sheet_date_conv (sscombo->parent.sv->sheet);

	vals = go_data_cache_field_get_vals (dcf, TRUE);
	if (NULL == vals)
		vals = go_data_cache_field_get_vals (dcf, FALSE);
	g_return_val_if_fail (vals != NULL, NULL);

	model = gtk_list_store_new (SSCOMBO_COL_LAST,
		G_TYPE_BOOLEAN, G_TYPE_STRING);
	str = g_string_sized_new (20);
	for (i = 0; i < vals->len ; i++) {
		v = g_ptr_array_index (vals, i);
		gtk_list_store_append (model, &iter);
		if (VALUE_IS_EMPTY(v))
			g_string_assign (str, _("<Blank>"));
		else if (GO_FORMAT_NUMBER_OK != format_value_gstring (str, NULL, v, -1, dconv))
			g_string_assign (str, "<ERROR>");
		gtk_list_store_set (model, &iter, 0, TRUE, 1, str->str, -1);
		g_string_truncate (str, 0);
	}
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
		G_CALLBACK (cb_filter_toggle), model);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("filter",
			renderer, "active", SSCOMBO_COL_FILTERED,
			NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", SSCOMBO_COL_NAME,
			NULL));

	*make_buttons = TRUE;

	return list;
}

static GtkWidget *
sscombo_create_arrow (G_GNUC_UNUSED SheetObject *so)
{
	return gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
}

/*static void
sscombo_ccombo_init (GnmCComboViewIface void *ccombo_iface)
{
	ccombo_iface->create_list	= sscombo_create_list;
	ccombo_iface->create_arrow	= sscombo_create_arrow;
	ccombo_iface->activate		= sscombo_activate;
}*/

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
sscombo_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
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
			"x",	  ((coords[2] >= 0.) ? coords[2] / scale : (coords[0] / scale - h + 1.)),
			"y",	  coords [3] / scale - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);
		goc_item_show (GOC_ITEM (view));
	} else
		goc_item_hide (GOC_ITEM (view));
}

static void
sscombo_class_init (GnmCComboViewClass *ccombo_class)
{
	SheetObjectViewClass *sov_class = (SheetObjectViewClass *) ccombo_class;
	ccombo_class->create_list	= sscombo_create_list;
	ccombo_class->create_arrow	= sscombo_create_arrow;
	ccombo_class->activate		= sscombo_activate;
	sov_class->set_bounds		= sscombo_set_bounds;
}

/****************************************************************************/

typedef GnmCComboView		GnmSheetSlicerComboView;
typedef GnmCComboViewClass	GnmSheetSlicerComboViewClass;
GSF_CLASS (GnmSheetSlicerComboView, gnm_sheet_slicer_combo_view,
	sscombo_class_init, NULL,
	GNM_CCOMBO_VIEW_TYPE)
