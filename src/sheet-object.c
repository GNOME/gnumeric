/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "sheet-object.h"

#include "sheet.h"
#include "sheet-view.h"
#include "sheet-control.h"
#include "sheet-private.h"
#include "dialogs.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "expr.h"
#include "ranges.h"
#include "commands.h"

#include "gnm-so-line.h"
#include "gnm-so-filled.h"
#include "sheet-object-cell-comment.h"
#include "sheet-object-widget.h"
#include "sheet-object-graph.h"
#include "sheet-object-image.h"
#include "graph.h"
#include "io-context.h"
#include "application.h"

#include <libxml/globals.h>
#include <libfoocanvas/foo-canvas.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(G_OBJECT_GET_CLASS(so))

#define	SO_VIEW_SHEET_CONTROL_KEY	"SheetControl"
#define	SO_VIEW_OBJECT_KEY		"SheetObject"
#define	SO_VIEW_KEY			"Key"

enum {
	BOUNDS_CHANGED,
	LAST_SIGNAL
};
static guint	     signals [LAST_SIGNAL] = { 0 };
static GObjectClass *parent_klass;

static void
cb_so_pull_to_front (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_pull_to_front);
}
static void
cb_so_pull_forward (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_pull_forward);
}
static void
cb_so_push_backward (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_push_backward);
}
static void
cb_so_push_to_back (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_push_to_back);
}
static void
cb_so_delete (SheetObject *so, SheetControl *sc)
{
	cmd_object_delete (sc_wbc (sc), so);
}
static void
cb_so_configure (SheetObject *so, SheetControl *sc)
{
	SO_CLASS(so)->user_config (so, sc);
}
static void
cb_so_cut (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), TRUE, sc_view (sc), so);
}
static void
cb_so_copy (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), FALSE, sc_view (sc), so);
}

static void
sheet_object_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const so_actions [] = {
		{ "gtk-properties",	NULL,		NULL,  0, cb_so_configure },
		{ NULL,	NULL, NULL, 0, NULL },
		{ NULL,			N_("_Order"),	NULL,  1, NULL },
			{ NULL,			N_("Pul_l to Front"),	NULL,  0, cb_so_pull_to_front },
			{ NULL,			N_("Pull _Forward"),	NULL,  0, cb_so_pull_forward },
			{ NULL,			N_("Push _Backward"),	NULL,  0, cb_so_push_backward },
			{ NULL,			N_("Pus_h to Back"),	NULL,  0, cb_so_push_to_back },
			{ NULL,			NULL,			NULL, -1, NULL },
		{ NULL,	NULL, NULL, 0, NULL },
		{ "gtk-cut",		NULL,		NULL,  0, cb_so_cut },
		{ "gtk-copy",		NULL,		NULL,  0, cb_so_copy },
		{ "gtk-delete",		NULL,		NULL, 0, cb_so_delete },
	};
	unsigned i;
	for (i = 0 ; i < G_N_ELEMENTS (so_actions); i++)
		if (i != 0 || SO_CLASS(so)->user_config != NULL)
			g_ptr_array_add (actions, (gpointer) (so_actions + i));
}

/**
 * sheet_object_unrealize:
 *
 * Clears all views of this object in its current sheet's controls.
 */
static void
sheet_object_unrealize (SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	/* The views remove themselves from the list */
	while (so->realized_list != NULL)
		sc_object_destroy_view (
			sheet_object_view_control (G_OBJECT (so->realized_list->data)),
			so);
}

/**
 * sheet_objects_max_extent :
 * @sheet :
 *
 * Utility routine to calculate the maximum extent of objects in this sheet.
 */
static void
sheet_objects_max_extent (Sheet *sheet)
{
	GnmCellPos max_pos = { 0, 0 };
	GList *ptr;

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);

		if (max_pos.col < so->anchor.cell_bound.end.col)
			max_pos.col = so->anchor.cell_bound.end.col;
		if (max_pos.row < so->anchor.cell_bound.end.row)
			max_pos.row = so->anchor.cell_bound.end.row;
	}

	if (sheet->max_object_extent.col != max_pos.col ||
	    sheet->max_object_extent.row != max_pos.row) {
		sheet->max_object_extent = max_pos;
		sheet_scrollbar_config (sheet);
	}
}

static void
sheet_object_finalize (GObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);

	g_return_if_fail (so != NULL);

	sheet_object_unrealize (so);

	if (so->sheet != NULL) {
		g_return_if_fail (IS_SHEET (so->sheet));

		/* If the object has already been inserted then mark sheet as dirty */
		if (NULL != g_list_find	(so->sheet->sheet_objects, so)) {
			so->sheet->sheet_objects =
				g_list_remove (so->sheet->sheet_objects, so);
			so->sheet->modified = TRUE;
		}

		if (so->anchor.cell_bound.end.col == so->sheet->max_object_extent.col &&
		    so->anchor.cell_bound.end.row == so->sheet->max_object_extent.row)
			sheet_objects_max_extent (so->sheet);
		so->sheet = NULL;
	}

	(*parent_klass->finalize) (object);
}

static void
sheet_object_init (GObject *object)
{
	int i;
	SheetObject *so = SHEET_OBJECT (object);

	so->type = SHEET_OBJECT_ACTION_STATIC;
	so->sheet = NULL;
	so->is_visible = TRUE;
	so->move_with_cells = TRUE;

	/* Store the logical position as A1 */
	so->anchor.cell_bound.start.col = so->anchor.cell_bound.start.row = 0;
	so->anchor.cell_bound.end.col = so->anchor.cell_bound.end.row = 1;
	so->anchor.direction = SO_DIR_UNKNOWN;

	for (i = 4; i-- > 0 ;) {
		so->anchor.offset [i] = 0.;
		so->anchor.type [i] = SO_ANCHOR_UNKNOWN;
	}
}

static void
so_default_size (SheetObject const *so, double *width, double *height)
{
	/* Provide some defaults (derived classes may want to override) */
	*width  = 72.;
	*height = 72.;
}

static void
sheet_object_class_init (GObjectClass *klass)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);
	klass->finalize = sheet_object_finalize;
	sheet_object_class->populate_menu        = sheet_object_populate_menu;
	sheet_object_class->print                = NULL;
	sheet_object_class->user_config          = NULL;
	sheet_object_class->rubber_band_directly = FALSE;
	sheet_object_class->default_size	 = so_default_size;
	sheet_object_class->xml_export_name 	 = NULL;

	signals [BOUNDS_CHANGED] = g_signal_new ("bounds-changed",
		SHEET_OBJECT_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (SheetObjectClass, bounds_changed),
		(GSignalAccumulator) NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

GSF_CLASS (SheetObject, sheet_object,
	   sheet_object_class_init, sheet_object_init,
	   G_TYPE_OBJECT);

SheetObject *
sheet_object_view_obj (GObject *view)
{
	gpointer obj = g_object_get_data (view, SO_VIEW_OBJECT_KEY);
	return SHEET_OBJECT (obj);
}

SheetControl *
sheet_object_view_control (GObject *view)
{
	gpointer obj = g_object_get_data (view, SO_VIEW_SHEET_CONTROL_KEY);
	return SHEET_CONTROL (obj);
}

gpointer
sheet_object_view_key (GObject *view)
{
	return g_object_get_data (view, SO_VIEW_KEY);
}

GObject *
sheet_object_get_view (SheetObject *so, gpointer key)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		GObject *obj = G_OBJECT (l->data);
		if (key == g_object_get_data (obj, SO_VIEW_KEY))
			return obj;
	}

	return NULL;
}

/**
 * sheet_object_update_bounds :
 *
 * @so  : The sheet object
 * @pos : An optional position marking the top left of the region
 *        needing relocation (default == A1)
 *
 * update the bounds of an object that intersects the region whose top left
 * is @pos.  This is used when an objects position is anchored to cols/rows
 * and they change position.
 **/
void
sheet_object_update_bounds (SheetObject *so, GnmCellPos const *pos)
{
	gboolean is_hidden = TRUE;
	int i, end;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (pos != NULL &&
	    so->anchor.cell_bound.end.col < pos->col &&
	    so->anchor.cell_bound.end.row < pos->row)
		return;

	/* Are all cols hidden ? */
	end = so->anchor.cell_bound.end.col;
	i = so->anchor.cell_bound.start.col;
	while (i <= end && is_hidden)
		is_hidden &= sheet_col_is_hidden (so->sheet, i++);

	/* Are all rows hidden ? */
	if (!is_hidden) {
		is_hidden = TRUE;
		end = so->anchor.cell_bound.end.row;
		i = so->anchor.cell_bound.start.row;
		while (i <= end && is_hidden)
			is_hidden &= sheet_row_is_hidden (so->sheet, i++);
	}

	so->is_visible = !is_hidden;

	g_signal_emit (so, signals [BOUNDS_CHANGED], 0);
}

/**
 * sheet_object_set_sheet :
 * @so :
 * @sheet :
 *
 * Adds a reference to the object.
 */
gboolean
sheet_object_set_sheet (SheetObject *so, Sheet *sheet)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	if (so->sheet == sheet)
		return TRUE;

	g_return_val_if_fail (g_list_find (sheet->sheet_objects, so) == NULL, TRUE);

	so->sheet = sheet;
	if (SO_CLASS (so)->assign_to_sheet &&
	    SO_CLASS (so)->assign_to_sheet (so, sheet)) {
		so->sheet = NULL;
		return TRUE;
	}

	g_object_ref (G_OBJECT (so));
	sheet->sheet_objects = g_list_prepend (sheet->sheet_objects, so);
	SHEET_FOREACH_CONTROL (so->sheet, view, control,
		sc_object_create_view (control, so););
	sheet_object_update_bounds (so, NULL);

	/* FIXME : add a flag to sheet to have sheet_update do this */
	sheet_objects_max_extent (sheet);

	return FALSE;
}

/**
 * sheet_object_get_sheet :
 *
 * A small utility to help keep the implementation of SheetObjects modular.
 */
Sheet *
sheet_object_get_sheet (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return so->sheet;
}

/**
 * sheet_object_clear_sheet :
 * @so :
 */
gboolean
sheet_object_clear_sheet (SheetObject *so)
{
	GList *ptr;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);

	if (!IS_SHEET (so->sheet))
		return FALSE;

	ptr = g_list_find (so->sheet->sheet_objects, so);
	g_return_val_if_fail (ptr != NULL, TRUE);

	if (SO_CLASS (so)->remove_from_sheet &&
	    SO_CLASS (so)->remove_from_sheet (so))
		return TRUE;

	sheet_object_unrealize (so);
	so->sheet->sheet_objects = g_list_remove_link (so->sheet->sheet_objects, ptr);
	so->sheet = NULL;
	g_list_free (ptr);
	g_object_unref (G_OBJECT (so));

	return FALSE;
}

static void
cb_sheet_object_view_finalized (SheetObject *so, GObject *view)
{
	so->realized_list = g_list_remove (so->realized_list, view);
}

/*
 * sheet_object_new_view
 *
 * Creates a FooCanvasItem for a SheetControlGUI and sets up the event
 * handlers.
 */
void
sheet_object_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GObject *view;

	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (IS_SHEET_OBJECT (so));

	view = sheet_object_get_view (so, key);
	if (view != NULL)
		return;

	view = SO_CLASS (so)->new_view (so, sc, key);

	g_return_if_fail (GTK_IS_OBJECT (view));

	/* Store some useful information */
	g_object_set_data (G_OBJECT (view), SO_VIEW_OBJECT_KEY, so);
	g_object_set_data (G_OBJECT (view), SO_VIEW_SHEET_CONTROL_KEY, sc);
	g_object_set_data (G_OBJECT (view), SO_VIEW_KEY, key);
	g_object_weak_ref (G_OBJECT (view),
		(GWeakNotify) cb_sheet_object_view_finalized, so);
	so->realized_list = g_list_prepend (so->realized_list, view);
}

void
sheet_object_print (SheetObject const *so, GnomePrintContext *ctx,
		    double width, double height)
{
	if (SO_CLASS (so)->print)
		SO_CLASS (so)->print (so, ctx, width, height);
}

GnmRange const *
sheet_object_range_get (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->anchor.cell_bound;
}

SheetObjectAnchor const *
sheet_object_anchor_get (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->anchor;
}

void
sheet_object_anchor_set (SheetObject *so, SheetObjectAnchor const *anchor)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	sheet_object_anchor_cpy (&so->anchor, anchor);
	if (so->sheet != NULL) {
		sheet_objects_max_extent (so->sheet);
		sheet_object_update_bounds (so, NULL);
	}
}

void
sheet_object_anchor_cpy	(SheetObjectAnchor *dst, SheetObjectAnchor const *src)
{
	g_return_if_fail (src != NULL);
	g_return_if_fail (dst != NULL);

	memcpy (dst, src, sizeof (SheetObjectAnchor));
}

static double
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col,
			SheetObjectAnchorType anchor_type, float offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_position_pixels_get :
 * @so : The sheet object
 * @coords : array of 4 ints where we return the coordinates in pixels
 *
 * Calculate the position of the object @so in pixels from the logical position
 * in the object.
 **/
void
sheet_object_position_pixels_get (SheetObject const *so,
				  SheetControl const *sc, double *coords)
{
	GnmRange const *r;

	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (IS_SHEET (so->sheet));

	r = &so->anchor.cell_bound;

	coords [0] = sc_colrow_distance_get (sc, TRUE, 0,  r->start.col);
	coords [2] = coords [0] + sc_colrow_distance_get (sc, TRUE,
		r->start.col, r->end.col);
	coords [1] = sc_colrow_distance_get (sc, FALSE, 0, r->start.row);
	coords [3] = coords [1] + sc_colrow_distance_get (sc, FALSE,
		r->start.row, r->end.row);

	coords [0] += cell_offset_calc_pixel (so->sheet, r->start.col,
		TRUE, so->anchor.type [0], so->anchor.offset [0]);
	coords [1] += cell_offset_calc_pixel (so->sheet, r->start.row,
		FALSE, so->anchor.type [1], so->anchor.offset [1]);
	coords [2] += cell_offset_calc_pixel (so->sheet, r->end.col,
		TRUE, so->anchor.type [2], so->anchor.offset [2]);
	coords [3] += cell_offset_calc_pixel (so->sheet, r->end.row,
		FALSE, so->anchor.type [3], so->anchor.offset [3]);
}

static double
cell_offset_calc_pt (Sheet const *sheet, int i, gboolean is_col,
		     SheetObjectAnchorType anchor_type, float offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pts;
	return offset * cri->size_pts;
}

/**
 * sheet_object_default_size
 * @so : The sheet object
 * @w : a ptr into which to store the default_width.
 * @h : a ptr into which to store the default_height.
 *
 * Measurements are in pts.
 */
void
sheet_object_default_size (SheetObject *so, double *w, double *h)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (w != NULL);
	g_return_if_fail (h != NULL);

	SO_CLASS (so)->default_size (so, w, h);
}

/**
 * sheet_object_position_pts_get :
 *
 * @so : The sheet object
 * @coords : array of 4 doubles
 *
 * Calculate the position of the object @so in pts from the logical position in
 * the object.
 */
void
sheet_object_position_pts_get (SheetObject const *so, double *coords)
{
	GnmRange const *r;

	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (coords != NULL);

	r = &so->anchor.cell_bound;

	coords [0] = sheet_col_get_distance_pts (so->sheet, 0,
		r->start.col);
	coords [2] = coords [0] + sheet_col_get_distance_pts (so->sheet,
		r->start.col, r->end.col);
	coords [1] = sheet_row_get_distance_pts (so->sheet, 0,
		r->start.row);
	coords [3] = coords [1] + sheet_row_get_distance_pts (so->sheet,
		r->start.row, r->end.row);

	coords [0] += cell_offset_calc_pt (so->sheet, r->start.col,
		TRUE, so->anchor.type [0], so->anchor.offset [0]);
	coords [1] += cell_offset_calc_pt (so->sheet, r->start.row,
		FALSE, so->anchor.type [1], so->anchor.offset [1]);
	coords [2] += cell_offset_calc_pt (so->sheet, r->end.col,
		TRUE, so->anchor.type [2], so->anchor.offset [2]);
	coords [3] += cell_offset_calc_pt (so->sheet, r->end.row,
		FALSE, so->anchor.type [3], so->anchor.offset [3]);
}

/**
 * sheet_objects_relocate :
 *
 * @rinfo : details on what should be moved.
 * @update : Should we do the bound_update now, or leave it for later.
 * 		if FALSE honour the move_with_cells flag.
 *
 * Uses the relocation info and the anchors to decide whether or not, and how
 * to relocate objects when the grid moves (eg ins/del col/row).
 */
void
sheet_objects_relocate (GnmExprRelocateInfo const *rinfo, gboolean update)
{
	GList   *ptr, *next;
	GnmRange	 dest;
	gboolean clear, change_sheets;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	dest = rinfo->origin;
	clear = range_translate (&dest, rinfo->col_offset, rinfo->row_offset);
	change_sheets = (rinfo->origin_sheet != rinfo->target_sheet);

	/* Clear the destination range on the target sheet */
	if (change_sheets) {
		GList *copy = g_list_copy (rinfo->target_sheet->sheet_objects);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			GnmRange const *r  = &so->anchor.cell_bound;
			if (range_contains (&dest, r->start.col, r->start.row))
				g_object_unref (G_OBJECT (so));
		}
		g_list_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		GnmRange       *r  = &so->anchor.cell_bound;

		next = ptr->next;
		if (update && !so->move_with_cells)
			continue;
		if (range_contains (&rinfo->origin,
				    r->start.col, r->start.row)) {
			/* FIXME : just moving the range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (r, rinfo->col_offset, rinfo->row_offset)) {
				g_object_unref (G_OBJECT (so));
				continue;
			}
			if (change_sheets) {
				sheet_object_clear_sheet (so);
				sheet_object_set_sheet (so, rinfo->target_sheet);
			} else if (update)
				sheet_object_update_bounds (so, NULL);
		} else if (!change_sheets &&
			   range_contains (&dest, r->start.col, r->start.row)) {
			g_object_unref (G_OBJECT (so));
			continue;
		}
	}

	sheet_objects_max_extent (rinfo->origin_sheet);
	if (change_sheets)
		sheet_objects_max_extent (rinfo->target_sheet);
}

/**
 * sheet_objects_get :
 *
 * @sheet : the sheet.
 * @r     : an optional range to look in
 * @t     : The type of object to lookup
 *
 * Returns a list of which the caller must free (just the list not the content).
 * Containing all objects of exactly the specified type (inheritence does not count).
 */
GSList *
sheet_objects_get (Sheet const *sheet, GnmRange const *r, GType t)
{
	GSList *res = NULL;
	GList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GObject *obj = G_OBJECT (ptr->data);

		if (t == G_TYPE_NONE || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->anchor.cell_bound))
				res = g_slist_prepend (res, so);
		}
	}
	return g_slist_reverse (res);
}

/**
 * sheet_object_clear :
 *
 * @sheet : the sheet.
 * @r     : an optional range to look in
 *
 * removes the objects in the region.
 */
void
sheet_objects_clear (Sheet const *sheet, GnmRange const *r, GType t)
{
	GList *ptr, *next;

	g_return_if_fail (IS_SHEET (sheet));

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = next ) {
		GObject *obj = G_OBJECT (ptr->data);
		next = ptr->next;
		if (t == G_TYPE_NONE || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->anchor.cell_bound))
				g_object_unref (G_OBJECT (so));
		}
	}
}

void
sheet_object_register (void)
{
	GNM_SO_LINE_TYPE;
	GNM_SO_FILLED_TYPE;
	SHEET_OBJECT_GRAPH_TYPE;
	SHEET_OBJECT_IMAGE_TYPE;
	GNM_GO_DATA_SCALAR_TYPE;
	GNM_GO_DATA_VECTOR_TYPE;
	CELL_COMMENT_TYPE;

	sheet_object_widget_register ();
}

/**
 * sheet_object_dup:
 * @so: a #SheetObject to duplicate
 *
 * Returns : A copy of @so that is not attached to a sheet.
 *    Caller is responsible for the reference.
 **/
SheetObject *
sheet_object_dup (SheetObject const *so)
{
	SheetObject *new_so = NULL;

	if (!SO_CLASS (so)->copy)
		return NULL;

	new_so = g_object_new (G_OBJECT_TYPE (so), NULL);

	g_return_val_if_fail (new_so != NULL, NULL);

	SO_CLASS (so)->copy (new_so, so);
	new_so->type = so->type;
	sheet_object_anchor_cpy (&new_so->anchor, &so->anchor);

	return new_so;
}


/**
 * sheet_object_clone_sheet:
 * @src: The source sheet to read the objects from
 * @dst: The destination sheet to attach the objects to
 * @range: Optionally NULL region of interest
 *
 * Clones the objects of the src sheet and attaches them into the dst sheet
 **/
void
sheet_object_clone_sheet (Sheet const *src, Sheet *dst, GnmRange *range)
{
	SheetObject *so;
	SheetObject *new_so;
	GList *list;

	g_return_if_fail (IS_SHEET (dst));
	g_return_if_fail (dst->sheet_objects == NULL);

	list = src->sheet_objects;
	for (; list != NULL; list = list->next) {
		so = (SheetObject *) list->data;
		if (range == NULL || range_overlap (range, &so->anchor.cell_bound)) {
			new_so = sheet_object_dup (so);
			if (new_so != NULL) {
				sheet_object_set_sheet (new_so, dst);
				g_object_unref (new_so);
			}
		}
	}

	dst->sheet_objects = g_list_reverse (dst->sheet_objects);
}


/**
 * sheet_object_direction_set:
 * @so: The sheet object that we are calculating the direction for
 * @coords: array of coordinates in L,T,R,B order
 *
 * Sets the object direction from the given the new coordinates
 * The original coordinates are assumed to be normalized (so that top
 * is above bottom and right is at the right of left)
 **/
void
sheet_object_direction_set (SheetObject *so, gdouble const *coords)
{
	if (so->anchor.direction == SO_DIR_UNKNOWN)
		return;

	so->anchor.direction = SO_DIR_NONE_MASK;

	if (coords [1] < coords [3])
		so->anchor.direction |= SO_DIR_DOWN;
	if (coords [0] < coords [2])
		so->anchor.direction |= SO_DIR_RIGHT;

}

/**
 * sheet_object_rubber_band_directly:
 * @so:
 *
 * Returns TRUE if we should draw the object as we are laying it out on
 * an sheet. If FLASE we draw a rectangle where the object is going to go
 *
 * Return Value:
 **/
gboolean
sheet_object_rubber_band_directly (SheetObject const *so)
{
	return SO_CLASS (so)->rubber_band_directly;
}

/**
 * sheet_object_anchor_init :
 *
 * A utility routine to initialize an anchor.  Useful in case we add
 * fields in the future and want to ensure that everything is initialized.
 **/
void
sheet_object_anchor_init (SheetObjectAnchor *anchor,
			  GnmRange const *r, float const *offsets,
			  SheetObjectAnchorType const *types,
			  SheetObjectDirection direction)
{
	int i;

	if (r == NULL) {
		static GnmRange const defaultVal = { { 0, 0 }, { 1, 1 } };
		r = &defaultVal;
	}
	anchor->cell_bound = *r;

	if (offsets == NULL) {
		static float const defaultVal [4] = { 0., 0., 0., 0. };
		offsets = defaultVal;
	}
	for (i = 4; i-- > 0 ; )
		anchor->offset [i] = offsets [i];

	if (types == NULL) {
		static SheetObjectAnchorType const defaultVal [4] = {
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START
		};
		types = defaultVal;
	}
	for (i = 4; i-- > 0 ; )
		anchor->type [i] = types [i];

	anchor->direction = direction;
	/* TODO : add sanity checking to handle offsets past edges of col/row */
}

/*****************************************************************************/

gint
sheet_object_adjust_stacking (SheetObject *so, gint positions)
{
	GList *l;
	gint before = -1;
	gint after = -1;

	for (l = so->realized_list; l; l = l->next) {
		FooCanvasItem *item = FOO_CANVAS_ITEM (l->data);
		FooCanvasGroup *parent = FOO_CANVAS_GROUP (item->parent);
		GList *link = g_list_find (parent->item_list, item);
		before = g_list_position (parent->item_list, link);
		if (positions > 0)
			foo_canvas_item_raise (item, positions);
		else
			foo_canvas_item_lower (item, - positions);
		link = g_list_find (parent->item_list, item);
		after = g_list_position (parent->item_list, link);
	}
	return ((before == -1 || after == -1) ? positions : (after - before));
}
