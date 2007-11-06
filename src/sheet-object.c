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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "sheet-object.h"

#include "sheet.h"
#include "dependent.h"
#include "sheet-view.h"
#include "sheet-control.h"
#include "sheet-private.h"
#include "dialogs.h"
#include "sheet-object-impl.h"
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
#include <goffice/app/io-context.h>
#include "application.h"
#include "gutils.h"

#include <libxml/globals.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
#include <goffice/utils/go-glib-extras.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(G_OBJECT_GET_CLASS(so))

enum {
	BOUNDS_CHANGED,
	UNREALIZED,
	LAST_SIGNAL
};
static guint	     signals [LAST_SIGNAL] = { 0 };
static GObjectClass *parent_klass;
static GQuark	sov_so_quark;
static GQuark	sov_container_quark;

static void
cb_so_snap_to_grid (SheetObject *so, SheetControl *sc)
{
	SheetObjectAnchor *snapped =
		sheet_object_anchor_dup	(sheet_object_get_anchor (so));
	snapped->offset[0] = snapped->offset[1] = 0.;
	snapped->offset[2] = snapped->offset[3] = 1.;
	cmd_objects_move (sc_wbc (sc),
		g_slist_prepend (NULL, so),
		g_slist_prepend (NULL, snapped),
		FALSE, _("Snap object to grid"));
}
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
	cmd_objects_delete (sc_wbc (sc),
		go_slist_create (so, NULL), NULL);
}
void
sheet_object_get_editor (SheetObject *so, SheetControl *sc)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (SO_CLASS (so));

	if (SO_CLASS(so)->user_config)
		SO_CLASS(so)->user_config (so, sc);
}
static void
cb_so_cut (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), TRUE, sc_view (sc),
		go_slist_create (so, NULL));
}
static void
cb_so_copy (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), FALSE, sc_view (sc),
		go_slist_create (so, NULL));
}

static void
sheet_object_populate_menu_real (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const so_actions [] = {
		{ "gtk-properties",	NULL,		NULL,  0, sheet_object_get_editor },
		{ NULL,	NULL, NULL, 0, NULL },
		{ "gtk-fullscreen",	N_("_Snap to Grid"),	NULL,  0, cb_so_snap_to_grid },
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
 * sheet_object_populate_menu :
 * @so : #SheetObject optionally NULL
 * @actions : #GPtrArray
 *
 * Get a list of the actions that can be performed on @so, if @so is NULL use
 * the default set.
 **/
void
sheet_object_populate_menu (SheetObject *so, GPtrArray *actions)
{
	if (NULL != so)
		SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so))->populate_menu (so, actions);
	else
		sheet_object_populate_menu (NULL, actions);
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
	GSList *ptr;

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
	if (so->sheet != NULL)
		sheet_object_clear_sheet (so);
	parent_klass->finalize (object);
}

static void
sheet_object_init (GObject *object)
{
	int i;
	SheetObject *so = SHEET_OBJECT (object);

	so->sheet = NULL;
	so->flags = SHEET_OBJECT_IS_VISIBLE | SHEET_OBJECT_PRINT |
		SHEET_OBJECT_MOVE_WITH_CELLS | SHEET_OBJECT_SIZE_WITH_CELLS;

	/* Store the logical position as A1 */
	so->anchor.cell_bound.start.col = so->anchor.cell_bound.start.row = 0;
	so->anchor.cell_bound.end.col = so->anchor.cell_bound.end.row = 1;
	so->anchor.base.direction = GOD_ANCHOR_DIR_UNKNOWN;

	for (i = 4; i-- > 0 ;)
		so->anchor.offset [i] = 0.;
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
	sheet_object_class->populate_menu        = sheet_object_populate_menu_real;
	sheet_object_class->user_config          = NULL;
	sheet_object_class->rubber_band_directly = FALSE;
	sheet_object_class->interactive          = FALSE;
	sheet_object_class->default_size	 = so_default_size;
	sheet_object_class->xml_export_name	 = NULL;
	sheet_object_class->foreach_dep          = NULL;

	signals [BOUNDS_CHANGED] = g_signal_new ("bounds-changed",
		SHEET_OBJECT_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (SheetObjectClass, bounds_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals [UNREALIZED] = g_signal_new ("unrealized",
		SHEET_OBJECT_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (SheetObjectClass, unrealized),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

GSF_CLASS (SheetObject, sheet_object,
	   sheet_object_class_init, sheet_object_init,
	   G_TYPE_OBJECT);

SheetObjectView *
sheet_object_get_view (SheetObject const *so, SheetObjectViewContainer *container)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		SheetObjectView *view = SHEET_OBJECT_VIEW (l->data);
		if (container == g_object_get_qdata (G_OBJECT (view), sov_container_quark))
			return view;
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
		so->flags |= SHEET_OBJECT_IS_VISIBLE;
	} else
		so->flags &= ~SHEET_OBJECT_IS_VISIBLE;

	g_signal_emit (so, signals [BOUNDS_CHANGED], 0);
}

/**
 * sheet_object_get_sheet :
 *
 * A small utility to help keep the implementation of SheetObjects modular.
 **/
Sheet *
sheet_object_get_sheet (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return so->sheet;
}

static int
cb_create_views (SheetObject *so)
{
	g_object_set_data (G_OBJECT (so), "create_view_handler", NULL);
	SHEET_FOREACH_CONTROL (so->sheet, view, control,
		sc_object_create_view (control, so););
	sheet_object_update_bounds (so, NULL);
	return FALSE;
}

/**
 * sheet_object_set_sheet :
 * @so :
 * @sheet :
 *
 * Adds a reference to the object.
 *
 * Returns TRUE if there was a problem
 **/
gboolean
sheet_object_set_sheet (SheetObject *so, Sheet *sheet)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (so->sheet == NULL, TRUE);
	g_return_val_if_fail (g_slist_find (sheet->sheet_objects, so) == NULL, TRUE);

	so->sheet = sheet;
	if (SO_CLASS (so)->assign_to_sheet &&
	    SO_CLASS (so)->assign_to_sheet (so, sheet)) {
		so->sheet = NULL;
		return TRUE;
	}

	g_object_ref (G_OBJECT (so));
	sheet->sheet_objects = g_slist_prepend (sheet->sheet_objects, so);
	/* FIXME : add a flag to sheet to have sheet_update do this */
	sheet_objects_max_extent (sheet);

	if (NULL == g_object_get_data (G_OBJECT (so), "create_view_handler")) {
		guint id = g_idle_add ((GSourceFunc) cb_create_views, so);
		g_object_set_data (G_OBJECT (so), "create_view_handler", GUINT_TO_POINTER (id));
	}

	return FALSE;
}

/**
 * sheet_object_clear_sheet :
 * @so : #SheetObject
 *
 * Removes @so from it's container, unrealizes all views, disconects the
 * associated data and unrefs the object
 **/
void
sheet_object_clear_sheet (SheetObject *so)
{
	GSList *ptr;
	gpointer view_handler;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (so->sheet == NULL) /* already removed */
		return;

	g_return_if_fail (IS_SHEET (so->sheet));

	ptr = g_slist_find (so->sheet->sheet_objects, so);
	g_return_if_fail (ptr != NULL);

	/* clear any pending attempts to create views */
	view_handler = g_object_get_data (G_OBJECT (so), "create_view_handler");
	if (NULL != view_handler) {
		g_source_remove (GPOINTER_TO_UINT (view_handler));
		g_object_set_data (G_OBJECT (so), "create_view_handler", NULL);
	}

	/* The views remove themselves from the list */
	while (so->realized_list != NULL)
		sheet_object_view_destroy (so->realized_list->data);
	g_signal_emit (so, signals [UNREALIZED], 0);

	if (SO_CLASS (so)->remove_from_sheet &&
	    SO_CLASS (so)->remove_from_sheet (so))
		return;

	so->sheet->sheet_objects = g_slist_remove_link (so->sheet->sheet_objects, ptr);
	g_slist_free (ptr);

	if (so->anchor.cell_bound.end.col == so->sheet->max_object_extent.col &&
	    so->anchor.cell_bound.end.row == so->sheet->max_object_extent.row)
		sheet_objects_max_extent (so->sheet);

	so->sheet = NULL;
	g_object_unref (G_OBJECT (so));
}

static void
cb_sheet_object_invalidate_sheet (GnmDependent *dep, SheetObject *so, gpointer user)
{
	Sheet *sheet = user;
	GnmExprRelocateInfo rinfo;
	GnmExprTop const *texpr;
	gboolean save_invalidated = sheet->being_invalidated;

	if (!dep->texpr)
		return;

	sheet->being_invalidated = TRUE;
	rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
	texpr = gnm_expr_top_relocate (dep->texpr, &rinfo, FALSE);
	sheet->being_invalidated = save_invalidated;

	if (texpr) {
		gboolean was_linked = dependent_is_linked (dep);
		dependent_set_expr (dep, texpr);
		gnm_expr_top_unref (texpr);
		if (was_linked)
			dependent_link (dep);
	}
}

void
sheet_object_invalidate_sheet (SheetObject *so, Sheet const *sheet)
{
	sheet_object_foreach_dep (so, cb_sheet_object_invalidate_sheet,
				  (gpointer)sheet);
}

/*
 * Loops over each dependent contained in a sheet object and call the handler.
 */
void
sheet_object_foreach_dep (SheetObject *so,
			  SheetObjectForeachDepFunc func,
			  gpointer user)
{
	if (SO_CLASS (so)->foreach_dep)
		SO_CLASS (so)->foreach_dep (so, func, user);
}


static void
cb_sheet_object_view_finalized (SheetObject *so, GObject *view)
{
	so->realized_list = g_list_remove (so->realized_list, view);
}

/**
 * sheet_object_new_view:
 * @so :
 * @sc :
 * @key :
 *
 * Asks @so to create a view for the (@sc,@key) pair.
 **/
SheetObjectView *
sheet_object_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	SheetObjectView *view;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	g_return_val_if_fail (NULL != container, NULL);

	view = sheet_object_get_view (so, container);
	if (view != NULL)
		return NULL;

	view = SO_CLASS (so)->new_view (so, container);

	if (NULL == view)
		return NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT_VIEW (view), NULL);

	/* Store some useful information */
	g_object_set_qdata (G_OBJECT (view), sov_so_quark, so);
	g_object_set_qdata (G_OBJECT (view), sov_container_quark, container);
	g_object_weak_ref (G_OBJECT (view),
		(GWeakNotify) cb_sheet_object_view_finalized, so);
	so->realized_list = g_list_prepend (so->realized_list, view);
	sheet_object_update_bounds (so, NULL);

	return view;
}

gboolean
sheet_object_can_print (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);
	return  (so->flags & SHEET_OBJECT_IS_VISIBLE) &&
		(so->flags & SHEET_OBJECT_PRINT) &&
		SO_CLASS (so)->draw_cairo != NULL;
}

/**
 * sheet_object_draw_cairo :
 *
 * Draw a sheet object using cairo.
 *
 *
 * We are assuming that the cairo context is set up so that the top
 * left of the bounds is (0,0). Note that this
 * is the real top left cell, not necessarily the cell with to which we are
 * anchored.
 *
 **/
void
sheet_object_draw_cairo (SheetObject const *so, cairo_t *cr, gboolean rtl)
{
	if (SO_CLASS (so)->draw_cairo) {
		SheetObjectAnchor const *anchor;
		double x = 0., y = 0., width, height, cell_width, cell_height;
		anchor = sheet_object_get_anchor (so);
		width = sheet_col_get_distance_pts (so->sheet,
					anchor->cell_bound.start.col,
					anchor->cell_bound.end.col + 1);
		height = sheet_row_get_distance_pts (so->sheet,
					anchor->cell_bound.start.row,
					anchor->cell_bound.end.row + 1);
		cell_width = sheet_col_get_distance_pts (so->sheet,
					anchor->cell_bound.start.col,
					anchor->cell_bound.start.col + 1);
		cell_height = sheet_row_get_distance_pts (so->sheet,
					anchor->cell_bound.start.row,
					anchor->cell_bound.start.row + 1);
		x = cell_width * anchor->offset[0];
		width -= x;

		y = cell_height * anchor->offset[1];
		height -= y;
		cell_width = sheet_col_get_distance_pts (so->sheet,
					anchor->cell_bound.end.col,
					anchor->cell_bound.end.col + 1);
		cell_height = sheet_row_get_distance_pts (so->sheet,
					anchor->cell_bound.end.row,
					anchor->cell_bound.end.row + 1);
		width -= cell_width * (1. - anchor->offset[2]);
		height -= cell_height * (1 - anchor->offset[3]);

		if (rtl) {
			x = cell_width * (1 - anchor->offset[2]);
		}

		/* we don't need to save/restore cairo, the caller must do it */
		cairo_translate (cr, x, y);
		SO_CLASS (so)->draw_cairo (so, cr, width, height);
	}
}

GnmRange const *
sheet_object_get_range (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->anchor.cell_bound;
}

SheetObjectAnchor const *
sheet_object_get_anchor (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->anchor;
}

void
sheet_object_set_anchor (SheetObject *so, SheetObjectAnchor const *anchor)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	sheet_object_anchor_assign (&so->anchor, anchor);
	if (so->sheet != NULL) {
		sheet_objects_max_extent (so->sheet);
		sheet_object_update_bounds (so, NULL);
	}
}

SheetObjectAnchor *
sheet_object_anchor_dup	(SheetObjectAnchor const *src)
{
	SheetObjectAnchor *res = g_new0 (SheetObjectAnchor, 1);
	sheet_object_anchor_assign (res, src);
	return res;
}

void
sheet_object_anchor_assign (SheetObjectAnchor *dst, SheetObjectAnchor const *src)
{
	g_return_if_fail (src != NULL);
	g_return_if_fail (dst != NULL);

	memcpy (dst, src, sizeof (SheetObjectAnchor));
}

static double
cell_offset_calc_pt (Sheet const *sheet, int i, gboolean is_col,
		     float offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	return offset * cri->size_pts;
}

/**
 * sheet_object_default_size
 * @so : The sheet object
 * @w : a ptr into which to store the default_width.
 * @h : a ptr into which to store the default_height.
 *
 * Measurements are in pts.
 **/
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
 **/
void
sheet_object_position_pts_get (SheetObject const *so, double *coords)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	sheet_object_anchor_to_pts (&so->anchor, so->sheet, coords);
}

void
sheet_object_anchor_to_pts (SheetObjectAnchor const *anchor,
			    Sheet const *sheet, double *res_pts)
{
	GnmRange const *r;

	g_return_if_fail (res_pts != NULL);

	r = &anchor->cell_bound;

	res_pts [0] = sheet_col_get_distance_pts (sheet, 0,
		r->start.col);
	res_pts [2] = res_pts [0] + sheet_col_get_distance_pts (sheet,
		r->start.col, r->end.col);
	res_pts [1] = sheet_row_get_distance_pts (sheet, 0,
		r->start.row);
	res_pts [3] = res_pts [1] + sheet_row_get_distance_pts (sheet,
		r->start.row, r->end.row);

	res_pts [0] += cell_offset_calc_pt (sheet, r->start.col,
		TRUE, anchor->offset [0]);
	res_pts [1] += cell_offset_calc_pt (sheet, r->start.row,
		FALSE, anchor->offset [1]);
	res_pts [2] += cell_offset_calc_pt (sheet, r->end.col,
		TRUE, anchor->offset [2]);
	res_pts [3] += cell_offset_calc_pt (sheet, r->end.row,
		FALSE, anchor->offset [3]);
}

static void
clear_sheet (SheetObject *so, GOUndo **pundo)
{
	if (pundo) {
		GOUndo *u = go_undo_binary_new
			(g_object_ref (so),
			 so->sheet,
			 (GOUndoBinaryFunc)sheet_object_set_sheet,
			 g_object_unref,
			 NULL);
		*pundo = go_undo_combine (*pundo, u);
	}

	sheet_object_clear_sheet (so);
}


/**
 * sheet_objects_relocate :
 *
 * @rinfo : details on what should be moved.
 * @update : Should we do the bound_update now, or leave it for later.
 *		if FALSE honour the move_with_cells flag.
 * @undo : if non-NULL add dropped objects to ::objects
 *
 * Uses the relocation info and the anchors to decide whether or not, and how
 * to relocate objects when the grid moves (eg ins/del col/row).
 **/
void
sheet_objects_relocate (GnmExprRelocateInfo const *rinfo, gboolean update,
			GOUndo **pundo)
{
	GSList   *ptr, *next;
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
		GSList *copy = g_slist_copy (rinfo->target_sheet->sheet_objects);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			GnmRange const *r  = &so->anchor.cell_bound;
			if (range_contains (&dest, r->start.col, r->start.row)) {
				clear_sheet (so, pundo);
			}
		}
		g_slist_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		GnmRange r = so->anchor.cell_bound;

		next = ptr->next;
		if (update && 0 == (so->flags & SHEET_OBJECT_MOVE_WITH_CELLS))
			continue;
		if (range_contains (&rinfo->origin, r.start.col, r.start.row)) {
			/* FIXME : just moving the range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (&r, rinfo->col_offset, rinfo->row_offset)) {
				clear_sheet (so, pundo);
				continue;
			}
			so->anchor.cell_bound = r;

			if (change_sheets) {
				g_object_ref (so);
				sheet_object_clear_sheet (so);
				sheet_object_set_sheet (so, rinfo->target_sheet);
				g_object_unref (so);
			} else if (update)
				sheet_object_update_bounds (so, NULL);
		} else if (!change_sheets &&
			   range_contains (&dest, r.start.col, r.start.row)) {
			clear_sheet (so, pundo);
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
 * Containing all objects of exactly the specified type (inheritence does not count)
 * that are completely contained by @r.
 **/
GSList *
sheet_objects_get (Sheet const *sheet, GnmRange const *r, GType t)
{
	GSList *res = NULL;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GObject *obj = G_OBJECT (ptr->data);

		if (t == G_TYPE_NONE || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_contained (&so->anchor.cell_bound, r))
				res = g_slist_prepend (res, so);
		}
	}
	return g_slist_reverse (res);
}

/**
 * sheet_objects_clear :
 *
 * @sheet : the sheet.
 * @r     : an optional range to look in
 *
 * removes the objects in the region.
 **/
void
sheet_objects_clear (Sheet const *sheet, GnmRange const *r, GType t,
		     GOUndo **pundo)
{
	GSList *ptr, *next;

	g_return_if_fail (IS_SHEET (sheet));

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = next ) {
		GObject *obj = G_OBJECT (ptr->data);
		next = ptr->next;
		if (t == G_TYPE_NONE || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_contained (&so->anchor.cell_bound, r))
				clear_sheet (so, pundo);
		}
	}
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
	new_so->flags = so->flags;
	sheet_object_anchor_assign (&new_so->anchor, &so->anchor);

	return new_so;
}

static void
cb_sheet_objects_dup (GnmDependent *dep, SheetObject *so, gpointer user)
{
	Sheet *src = user;
	Sheet *dst = sheet_object_get_sheet (so);
	GnmExprTop const *texpr;

	if (!dep->texpr)
		return;

	texpr = gnm_expr_top_relocate_sheet (dep->texpr, src, dst);
	if (texpr != dep->texpr) {
		gboolean was_linked= dependent_is_linked (dep);
		dependent_set_expr (dep, texpr);
		if (was_linked)
			dependent_link (dep);
	}
	gnm_expr_top_unref (texpr);
}


/**
 * sheet_objects_dup:
 * @src: The source sheet to read the objects from
 * @dst: The destination sheet to attach the objects to
 * @range: Optionally NULL region of interest
 *
 * Clones the objects of the src sheet and attaches them into the dst sheet
 **/
void
sheet_objects_dup (Sheet const *src, Sheet *dst, GnmRange *range)
{
	SheetObject *so;
	SheetObject *new_so;
	GSList *list;

	g_return_if_fail (IS_SHEET (dst));
	g_return_if_fail (dst->sheet_objects == NULL);

	for (list = src->sheet_objects; list != NULL; list = list->next) {
		so = (SheetObject *) list->data;
		if (range == NULL || range_overlap (range, &so->anchor.cell_bound)) {
			new_so = sheet_object_dup (so);
			if (new_so != NULL) {
				sheet_object_set_sheet (new_so, dst);
				sheet_object_foreach_dep (new_so, cb_sheet_objects_dup,
							  (gpointer)src);
				g_object_unref (new_so);
			}
		}
	}

	dst->sheet_objects = g_slist_reverse (dst->sheet_objects);
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
	if (so->anchor.base.direction == GOD_ANCHOR_DIR_UNKNOWN)
		return;

	so->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;

	if (coords [1] < coords [3])
		so->anchor.base.direction |= GOD_ANCHOR_DIR_DOWN;
	if (coords [0] < coords [2])
		so->anchor.base.direction |= GOD_ANCHOR_DIR_RIGHT;
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
	return FALSE;
}

/**
 * sheet_object_anchor_init :
 * @anchor : #SheetObjectAnchor
 * @r : #GnmRange
 * @offsets : float[4]
 * @direction : #GODrawingAnchorDir
 *
 * A utility routine to initialize an anchor.  Useful in case we change fields
 * in the future and want to ensure that everything is initialized.
 **/
void
sheet_object_anchor_init (SheetObjectAnchor *anchor,
			  GnmRange const *r, float const *offsets,
			  GODrawingAnchorDir direction)
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

	anchor->base.direction = direction;
	/* TODO : add sanity checking to handle offsets past edges of col/row */
}

/*****************************************************************************/

/**
 * sheet_object_get_stacking :
 * @so : #SheetObject
 *
 * Returns @so's position in the stack of sheet objects.
 **/
gint
sheet_object_get_stacking (SheetObject *so)
{
	GSList *ptr;
	int	pos = 0;

	g_return_val_if_fail (so != NULL, 0);
	g_return_val_if_fail (so->sheet != NULL, 0);

	for (ptr = so->sheet->sheet_objects ;  ptr ; ptr = ptr->next, pos++)
		if (ptr->data == so)
			return pos;

	g_warning ("Object not found??");
	return 0;
}

gint
sheet_object_adjust_stacking (SheetObject *so, gint offset)
{
	GList	 *l;
	GSList	**ptr, *node = NULL;
	int	  i, target, cur = 0;

	g_return_val_if_fail (so != NULL, 0);
	g_return_val_if_fail (so->sheet != NULL, 0);

	for (ptr = &so->sheet->sheet_objects ; *ptr ; ptr = &(*ptr)->next, cur++)
		if ((*ptr)->data == so) {
			node = *ptr;
			*ptr = (*ptr)->next;
			break;
		}

	g_return_val_if_fail (node != NULL, 0);

	/* Start at the begining when moving things towards the front */
	if (offset > 0) {
		ptr = &so->sheet->sheet_objects;
		i = 0;
	} else
		i = cur;

	for (target = cur - offset; *ptr && i < target ; ptr = &(*ptr)->next)
		i++;

	node->next = *ptr;
	*ptr = node;

	/* TODO : Move this to the container */
	for (l = so->realized_list; l; l = l->next) {
		FooCanvasItem *item = FOO_CANVAS_ITEM (l->data);
		if (offset > 0)
			foo_canvas_item_raise (item, offset);
		else
			foo_canvas_item_lower (item, - offset);
	}
	return cur - i;
}

/*****************************************************************************/

void
sheet_object_view_set_bounds (SheetObjectView *sov,
			      double const *coords, gboolean visible)
{
	SheetObjectViewIface *iface;

	g_return_if_fail (IS_SHEET_OBJECT_VIEW (sov));
	iface = SHEET_OBJECT_VIEW_GET_CLASS (sov);
	if (NULL != iface->set_bounds)
		(iface->set_bounds) (sov, coords, visible);
}

SheetObject *
sheet_object_view_get_so (SheetObjectView *view)
{
	return g_object_get_qdata (G_OBJECT (view), sov_so_quark);
}

void
sheet_object_view_destroy (SheetObjectView *sov)
{
	SheetObjectViewIface *iface = SHEET_OBJECT_VIEW_GET_CLASS (sov);
	g_return_if_fail (iface != NULL);
	if (iface->destroy)
		iface->destroy (sov);
}

GType
sheet_object_view_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (SheetObjectViewIface),	/* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"SheetObjectView", &type_info, 0);
	}

	return type;
}

/*****************************************************************************/

GType
sheet_object_imageable_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (SheetObjectImageableIface), /* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"SheetObjectImageable", &type_info, 0);
	}

	return type;
}

#define SHEET_OBJECT_IMAGEABLE_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), SHEET_OBJECT_IMAGEABLE_TYPE, SheetObjectImageableIface))

GtkTargetList *
sheet_object_get_target_list (SheetObject const *so)
{
	if (!IS_SHEET_OBJECT_IMAGEABLE (so))
		return NULL;

	return SHEET_OBJECT_IMAGEABLE_CLASS (so)->get_target_list (so);
}

void
sheet_object_write_image (SheetObject const *so, char const *format, double resolution,
			  GsfOutput *output, GError **err)
{
	g_return_if_fail (IS_SHEET_OBJECT_IMAGEABLE (so));

	SHEET_OBJECT_IMAGEABLE_CLASS (so)->write_image (so, format, resolution,
							output, err);

}

/*****************************************************************************/

GType
sheet_object_exportable_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (SheetObjectExportableIface), /* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"SheetObjectExportable", &type_info, 0);
	}

	return type;
}

#define SHEET_OBJECT_EXPORTABLE_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), SHEET_OBJECT_EXPORTABLE_TYPE, SheetObjectExportableIface))

GtkTargetList *
sheet_object_exportable_get_target_list (SheetObject const *so)
{
	if (!IS_SHEET_OBJECT_EXPORTABLE (so))
		return NULL;

	return SHEET_OBJECT_EXPORTABLE_CLASS (so)->get_target_list (so);
}

void
sheet_object_write_object (SheetObject const *so, char const *format,
			  GsfOutput *output, GError **err)
{
	GnmLocale *locale;

	g_return_if_fail (IS_SHEET_OBJECT_EXPORTABLE (so));

	locale = gnm_push_C_locale ();
	SHEET_OBJECT_EXPORTABLE_CLASS (so)->write_object (so, format,
							  output, err);
	gnm_pop_C_locale (locale);
}

/*****************************************************************************/

void
sheet_objects_init (void)
{
	GNM_SO_LINE_TYPE;
	GNM_SO_FILLED_TYPE;
	SHEET_OBJECT_GRAPH_TYPE;
	SHEET_OBJECT_IMAGE_TYPE;
	GNM_GO_DATA_SCALAR_TYPE;
	GNM_GO_DATA_VECTOR_TYPE;
	GNM_GO_DATA_MATRIX_TYPE;
	CELL_COMMENT_TYPE;

	sheet_object_widget_register ();
	sov_so_quark = g_quark_from_static_string ("SheetObject");
	sov_container_quark = g_quark_from_static_string ("SheetObjectViewContainer");
}
