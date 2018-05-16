/*
 * gnm-sheet-slicer.c:
 *
 * Copyright (C) 2008-2009 Jody Goldberg (jody@gnome.org)
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
#include <gnm-sheet-slicer.h>
#include <go-data-slicer-impl.h>
#include <go-data-slicer-field-impl.h>
#include <go-data-cache.h>
#include <sheet.h>
#include <ranges.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#include <glib-object.h>

struct _GnmSheetSlicer {
	GODataSlicer		base;

	Sheet		*sheet;
	GnmRange	 range;

	/* Offsets from the top-left (in LTR) pos range */
	unsigned int	 first_header_row, first_data_row, first_data_col;
	unsigned int	 row_page_count, col_page_count;

	struct {
		gboolean headers_col, headers_row, stripes_col, stripes_row, last_col, last_row;
	} show;

	GnmSheetSlicerLayout	layout;
};
typedef GODataSlicerClass GnmSheetSlicerClass;

#define GNM_SHEET_SLICER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), GNM_SHEET_SLICER_TYPE, GnmSheetSlicerClass))
#define GNM_IS_SHEET_SLICER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GNM_SHEET_SLICER_TYPE))
#define GNM_SHEET_SLICER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GNM_SHEET_SLICER_TYPE, GnmSheetSlicerClass))

enum {
	PROP_0,
	PROP_SHEET,
	PROP_RANGE,

	PROP_FIRST_HEADER_ROW,
	PROP_FIRST_DATA_COL,
	PROP_FIRST_DATA_ROW,

	PROP_SHOW_HEADERS_COL,
	PROP_SHOW_HEADERS_ROW,
	PROP_SHOW_STRIPES_COL,
	PROP_SHOW_STRIPES_ROW,
	PROP_SHOW_LAST_COL,
	PROP_SHOW_LAST_ROW,

	PROP_LAYOUT
};

static GObjectClass *parent_klass;
static void
gnm_sheet_slicer_init (GnmSheetSlicer *gss)
{
	gss->sheet = NULL;
	gss->first_header_row = gss->first_data_row = gss->first_data_col = gss->row_page_count = gss->col_page_count = 0;
}

static void
gnm_sheet_slicer_finalize (GObject *obj)
{
	GnmSheetSlicer *gss = (GnmSheetSlicer *)obj;

	if (NULL != gss->sheet) {
		g_warning ("finalizing a slicer that is still attached to a sheet");
	}

	(parent_klass->finalize) (obj);
}

static void
gnm_sheet_slicer_set_property (GObject *obj, guint property_id,
			       GValue const *value, GParamSpec *pspec)
{
	GnmSheetSlicer *gss = (GnmSheetSlicer *)obj;

	switch (property_id) {
	case PROP_SHEET : gnm_sheet_slicer_set_sheet (gss, g_value_get_object (value)); break;
	case PROP_RANGE : gnm_sheet_slicer_set_range (gss, g_value_get_boxed (value)); break;
	case PROP_FIRST_HEADER_ROW : gss->first_header_row = g_value_get_uint (value); break;
	case PROP_FIRST_DATA_COL : gss->first_data_col = g_value_get_uint (value); break;
	case PROP_FIRST_DATA_ROW : gss->first_data_row = g_value_get_uint (value); break;

	case PROP_SHOW_HEADERS_COL : gss->show.headers_col = g_value_get_boolean (value); break;
	case PROP_SHOW_HEADERS_ROW : gss->show.headers_row = g_value_get_boolean (value); break;
	case PROP_SHOW_STRIPES_COL : gss->show.stripes_col = g_value_get_boolean (value); break;
	case PROP_SHOW_STRIPES_ROW : gss->show.stripes_row = g_value_get_boolean (value); break;
	case PROP_SHOW_LAST_COL    : gss->show.last_col    = g_value_get_boolean (value); break;
	case PROP_SHOW_LAST_ROW    : gss->show.last_row    = g_value_get_boolean (value); break;

	case PROP_LAYOUT: gnm_sheet_slicer_set_layout (gss, g_value_get_enum (value)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_sheet_slicer_get_property (GObject *obj, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
	GnmSheetSlicer const *gss = (GnmSheetSlicer const *)obj;
	switch (property_id) {
	case PROP_SHEET : g_value_set_object (value, gss->sheet); break;
	case PROP_RANGE : g_value_set_boxed (value, &gss->range); break;
	case PROP_FIRST_HEADER_ROW : g_value_set_uint (value, gss->first_header_row ); break;
	case PROP_FIRST_DATA_COL : g_value_set_uint (value, gss->first_data_col); break;
	case PROP_FIRST_DATA_ROW : g_value_set_uint (value, gss->first_data_row); break;

	case PROP_SHOW_HEADERS_COL : g_value_set_boolean (value, gss->show.headers_col); break;
	case PROP_SHOW_HEADERS_ROW : g_value_set_boolean (value, gss->show.headers_row); break;
	case PROP_SHOW_STRIPES_COL : g_value_set_boolean (value, gss->show.stripes_col); break;
	case PROP_SHOW_STRIPES_ROW : g_value_set_boolean (value, gss->show.stripes_row); break;
	case PROP_SHOW_LAST_COL    : g_value_set_boolean (value, gss->show.last_col); break;
	case PROP_SHOW_LAST_ROW    : g_value_set_boolean (value, gss->show.last_row); break;

	case PROP_LAYOUT : g_value_set_enum (value, gss->layout); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_sheet_slicer_class_init (GnmSheetSlicerClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= gnm_sheet_slicer_set_property;
	gobject_class->get_property	= gnm_sheet_slicer_get_property;
	gobject_class->finalize		= gnm_sheet_slicer_finalize;

	g_object_class_install_property (gobject_class, PROP_SHEET,
		 g_param_spec_object ("sheet", NULL, NULL, GNM_SHEET_TYPE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_RANGE,
		 g_param_spec_boxed ("range", NULL, NULL, gnm_range_get_type (),
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_FIRST_HEADER_ROW,
		 g_param_spec_uint ("first-header-row", NULL, NULL, 0, GNM_MAX_ROWS, 0,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_FIRST_DATA_COL,
		 g_param_spec_uint ("first-data-col", NULL, NULL, 0, GNM_MAX_COLS, 0,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_FIRST_DATA_ROW,
		 g_param_spec_uint ("first-data-row", NULL, NULL, 0, GNM_MAX_ROWS, 0,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_SHOW_HEADERS_COL,
		g_param_spec_boolean ("show-headers-col", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_SHOW_HEADERS_ROW,
		g_param_spec_boolean ("show-headers-row", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_SHOW_STRIPES_COL,
		g_param_spec_boolean ("show-stripes-col", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_SHOW_STRIPES_ROW,
		g_param_spec_boolean ("show-stripes-row", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_SHOW_LAST_COL,
		g_param_spec_boolean ("show-last-col", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_SHOW_LAST_ROW,
		g_param_spec_boolean ("show-last-row", NULL, NULL, TRUE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_LAYOUT,
		 g_param_spec_enum ("layout", NULL, NULL, gnm_sheet_slicer_layout_get_type (), GSS_LAYOUT_XL_OUTLINE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GnmSheetSlicer, gnm_sheet_slicer,
	   gnm_sheet_slicer_class_init, gnm_sheet_slicer_init,
	   GO_DATA_SLICER_TYPE)

void
gnm_sheet_slicer_set_sheet (GnmSheetSlicer *gss, Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (GNM_IS_SHEET_SLICER (gss));
	g_return_if_fail (NULL == gss->sheet);

	g_object_ref (gss);
	gss->sheet = sheet;
	sheet->slicers = g_slist_prepend (sheet->slicers, gss);
}

void
gnm_sheet_slicer_clear_sheet (GnmSheetSlicer *gss)
{
	g_return_if_fail (GNM_IS_SHEET_SLICER (gss));
	g_return_if_fail (NULL != gss->sheet);

	gss->sheet->slicers = g_slist_remove (gss->sheet->slicers, gss);
	gss->sheet = NULL;
	g_object_unref (gss);
}

GnmRange const	*
gnm_sheet_slicer_get_range (GnmSheetSlicer const *gss)
{
	g_return_val_if_fail (GNM_IS_SHEET_SLICER (gss), NULL);
	return &gss->range;
}

void
gnm_sheet_slicer_set_range (GnmSheetSlicer *gss, GnmRange const *r)
{
	g_return_if_fail (GNM_IS_SHEET_SLICER (gss));
	gss->range = *r;
}

/**
 * gnm_sheet_slicer_overlaps_range:
 * @gss: #GnmSheetSlicer
 * @r: #GnmRange
 *
 * Returns: %TRUE if @gss overlaps @r.
 **/
gboolean
gnm_sheet_slicer_overlaps_range (GnmSheetSlicer const *gss, GnmRange const *r)
{
	g_return_val_if_fail (GNM_IS_SHEET_SLICER (gss), FALSE);
	return range_overlap (&gss->range, r);
}

/**
 * gnm_sheet_slicer_field_header_at_pos:
 * @gss: #GnmSheetSlicer const
 * @pos: #GnmCellPos const
 *
 * Checks to see if @pos (in absolute position, not relative to @gss' corner)
 * corresponds to a field header.  [Does not add a reference]
 *
 * Returns a #GODataSlicerField or %NULL.
 **/
GODataSlicerField *
gnm_sheet_slicer_field_header_at_pos (GnmSheetSlicer const *gss,
				      GnmCellPos const *pos)
{
	int res = -1;
	unsigned int c, r;

	g_return_val_if_fail (GNM_IS_SHEET_SLICER (gss), NULL);

	/* 0) TODO page fields */
	if (pos->col < gss->range.start.col || pos->row < gss->range.start.row)
		return NULL;

	c = pos->col - gss->range.start.col;
	r = pos->row - gss->range.start.row;

	/* TODO other layouts */

	/* col headers along the top starting at first_data_col */
	if (r == 0 &&
	    c >= gss->first_data_col) {
		c -= gss->first_data_col;
		if (c < gss->base.fields[GDS_FIELD_TYPE_COL]->len)
			res = g_array_index (gss->base.fields[GDS_FIELD_TYPE_COL], int, c);


	/* row headers just about data starting at 0th col */
	} else if (r >= (gss->first_data_row - 1) &&	/* -1 for the headers */
		   c < gss->first_data_col) {
		if (c < gss->base.fields[GDS_FIELD_TYPE_ROW]->len)
			res = g_array_index (gss->base.fields[GDS_FIELD_TYPE_ROW], int, c);
	}

	return (res >= 0) ? go_data_slicer_get_field (&gss->base, res) : NULL;
}

/************************************************************/

/**
 * gnm_sheet_slicers_at_pos:
 * @sheet: #Sheet
 * @pos: #GnmCellPos
 *
 * Returns: (transfer none): %NULL or the #GnmSheetSlicer in @sheet that overlaps with @pos.
 **/
GnmSheetSlicer *
gnm_sheet_slicers_at_pos (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *ptr;
	GnmRange r;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (NULL != pos, NULL);

	range_init_cellpos (&r, pos);
	for (ptr = sheet->slicers; ptr != NULL ; ptr = ptr->next)
		if (gnm_sheet_slicer_overlaps_range (ptr->data, &r))
			return ptr->data;

	return NULL;
}

#if 0
static void
gss_append_field_indicies (GnmSheetSlicer const *gss, GODataSlicerFieldType type,
			   GArray *field_order)
{
	GArray *tmp = gss->base.fields [type];
	unsigned int i, n = tmp->len;
	for (i = 0 ; i < n; i++)
		g_array_append_val (field_order, g_array_index (tmp, int, i));
}

static void
gnm_sheet_slicer_test_sort (GnmSheetSlicer *gss)
{
	/* quick test to sort the cache based on the row/col */
	GArray *permutation, *field_order;
	unsigned int i, n;

	field_order = g_array_sized_new (FALSE, FALSE, sizeof (unsigned int), gss->base.all_fields->len);
	gss_append_field_indicies (gss, GDS_FIELD_TYPE_ROW, field_order);
	gss_append_field_indicies (gss, GDS_FIELD_TYPE_COL, field_order);

	n = go_data_cache_num_items (gss->base.cache);
	permutation = g_array_sized_new (FALSE, FALSE, sizeof (int), n);
	for (i = 0 ; i < n ; i++)
		g_array_append_val (permutation, i);
	go_data_cache_permute (gss->base.cache, field_order, permutation);
	go_data_cache_dump (gss->base.cache, field_order, permutation);

	g_array_free (field_order, TRUE);
	g_array_free (permutation, TRUE);
}
#endif

/**
 * gnm_sheet_slicer_regenerate:
 * @gss: #GnmSheetSlicer
 *
 * Do some work!
 * See what we need to do then think about when portions belong in the GODataSlicer base.
 *
 **/
void
gnm_sheet_slicer_regenerate (GnmSheetSlicer *gss)
{
#if 0
	GArray *permutation, *rows;
	unsigned int i, n;

	g_return_if_fail (GNM_IS_SHEET_SLICER (gss));
	g_return_if_fail (IS_SHEET (gss->sheet));
	g_return_if_fail (NULL != gss->base.cache);

	field_order = g_array_sized_new (FALSE, FALSE, sizeof (unsigned int), gss->base.all_fields->len);
	gss_append_field_indicies (gss, GDS_FIELD_TYPE_ROW, field_order);
	gss_append_field_indicies (gss, GDS_FIELD_TYPE_COL, field_order);

	n = go_data_cache_num_items (gss->base.cache);
#endif
}

GnmSheetSlicerLayout
gnm_sheet_slicer_get_layout (GnmSheetSlicer const *gss)
{
	g_return_val_if_fail (GNM_IS_SHEET_SLICER (gss), GSS_LAYOUT_XL_OUTLINE);
	return gss->layout;
}

void
gnm_sheet_slicer_set_layout (GnmSheetSlicer *gss, GnmSheetSlicerLayout l)
{
	g_return_if_fail (GNM_IS_SHEET_SLICER (gss));
	gss->layout = l;
}

GType
gnm_sheet_slicer_layout_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GSS_LAYOUT_XL_OUTLINE, "GSS_LAYOUT_XL_OUTLINE", "xl-outline" },
			{ GSS_LAYOUT_XL_COMPACT, "GSS_LAYOUT_XL_COMPACT", "xl-compact" },
			{ GSS_LAYOUT_XL_TABULAR, "GSS_LAYOUT_XL_TABULAR", "xl-tabular" },
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmSheetSlicerLayout", values);
	}
	return etype;
}
