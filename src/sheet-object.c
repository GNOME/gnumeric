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
#include "gnumeric.h"
#include "sheet-object.h"

#include "sheet-control-gui.h"
#include "dialogs.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "sheet.h"
#include "sheet-private.h"
#include "expr.h"
#include "ranges.h"
#include "commands.h"

#include "sheet-object-graphic.h"
#include "sheet-object-cell-comment.h"
#include "sheet-object-widget.h"

#include <libgnome/gnome-i18n.h>
#include <gal/util/e-util.h>

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(G_OBJECT_GET_CLASS(so))

#define	SO_VIEW_SHEET_CONTROL_KEY	"SheetControl"
#define	SO_VIEW_OBJECT_KEY		"SheetObject"

static GtkObjectClass *sheet_object_parent_class;

static void
cb_sheet_object_remove (GtkWidget *widget, GtkObject *so_view)
{
	Sheet *sheet;
	SheetObject *so;
	SheetControlGUI *scg;
	WorkbookControl *wbc;

	so = sheet_object_view_obj (so_view);
	scg = sheet_object_view_control (so_view);
	wbc = sc_wbc (SHEET_CONTROL (scg));
	sheet = sc_sheet (SHEET_CONTROL (scg));

	cmd_object_delete (wbc, so);
	gtk_object_unref (GTK_OBJECT (so));
}

static void
cb_sheet_object_configure (GtkWidget *widget, GtkObject *obj_view)
{
	SheetControlGUI *scg;
	SheetObject *so;

	g_return_if_fail (obj_view != NULL);

	so = sheet_object_view_obj (obj_view);
	scg = sheet_object_view_control (obj_view);

	SO_CLASS(so)->user_config (so, scg);
}

/**
 * sheet_object_populate_menu:
 * @so:  the sheet object
 * @menu: the menu to insert into
 *
 * Add standard items to the object's popup menu.
 */
static void
sheet_object_populate_menu (SheetObject *so,
			    GtkObject *obj_view,
			    GtkMenu *menu)
{
	GtkWidget *item;
	if (SO_CLASS(so)->user_config != NULL) {
		item = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP,
					      _("Properties..."));
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (cb_sheet_object_configure), obj_view);
		gtk_menu_append (menu, item);
	}

	item = gnome_stock_menu_item (GNOME_STOCK_MENU_CLOSE, _("Delete"));
	gtk_menu_append (menu, item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (cb_sheet_object_remove), obj_view);
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
		gtk_object_destroy (GTK_OBJECT (so->realized_list->data));
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
	CellPos max_pos = { 0, 0 };
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
sheet_object_destroy (GtkObject *object)
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
	(*sheet_object_parent_class->destroy)(object);
}

static void
sheet_object_init (GtkObject *object)
{
	int i;
	SheetObject *so = SHEET_OBJECT (object);

	so->type = SHEET_OBJECT_ACTION_STATIC;
	so->sheet = NULL;
	so->is_visible = TRUE;

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
sheet_object_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = sheet_object_destroy;
	sheet_object_class->update_bounds        = NULL;
	sheet_object_class->populate_menu        = sheet_object_populate_menu;
	sheet_object_class->print                = NULL;
	sheet_object_class->user_config          = NULL;
	sheet_object_class->rubber_band_directly = FALSE;

	/* Provide some defaults (derived classes may want to override) */
	sheet_object_class->default_width_pts = 72.;	/* 1 inch */
	sheet_object_class->default_height_pts = 36.;	/* 1/2 inch */
}

E_MAKE_TYPE (sheet_object, "SheetObject", SheetObject,
	     sheet_object_class_init, sheet_object_init,
	     GTK_TYPE_OBJECT);

SheetObject *
sheet_object_view_obj (GtkObject *view)
{
	GtkObject *obj = gtk_object_get_data (view, SO_VIEW_OBJECT_KEY);
	return SHEET_OBJECT (obj);
}

SheetControlGUI *
sheet_object_view_control (GtkObject *view)
{
	GtkObject *obj = gtk_object_get_data (view, SO_VIEW_SHEET_CONTROL_KEY);
	return SHEET_CONTROL_GUI (obj);
}

GtkObject *
sheet_object_get_view (SheetObject *so, SheetControl *sc)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *obj = GTK_OBJECT (l->data);
		if (sc == SHEET_CONTROL (sheet_object_view_control (obj)))
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
 */
void
sheet_object_update_bounds (SheetObject *so, CellPos const *pos)
{
	GList *l;
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

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *view = GTK_OBJECT (l->data);
		SO_CLASS (so)->update_bounds (so, view,
			sheet_object_view_control (view));
	}
}

/**
 * sheet_object_set_sheet :
 * @so :
 * @sheet :
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

	sheet->sheet_objects = g_list_prepend (sheet->sheet_objects, so);
	sheet_object_realize (so);
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
	    SO_CLASS (so)->remove_from_sheet (so)) {
		so->sheet = NULL;
		return TRUE;
	}
	sheet_object_unrealize (so);
	so->sheet->sheet_objects = g_list_remove_link (so->sheet->sheet_objects, ptr);
	so->sheet = NULL;
	g_list_free (ptr);

	return FALSE;
}

static void
sheet_object_view_destroyed (GtkObject *view, SheetObject *so)
{
	SheetControlGUI *scg = sheet_object_view_control (view);

	so->realized_list = g_list_remove (so->realized_list, view);
	gtk_object_remove_data	(view, SO_VIEW_SHEET_CONTROL_KEY);
	gtk_object_unref (GTK_OBJECT (scg));
}

/*
 * sheet_object_new_view
 *
 * Creates a GnomeCanvasItem for a SheetControlGUI and sets up the event
 * handlers.
 */
void
sheet_object_new_view (SheetObject *so, SheetControlGUI *scg)
{
	GtkObject *view;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (IS_SHEET_OBJECT (so));

	view = SO_CLASS (so)->new_view (so, scg);

	g_return_if_fail (GTK_IS_OBJECT (view));

	/* Store some useful information */
	gtk_object_set_data (GTK_OBJECT (view), SO_VIEW_OBJECT_KEY, so);
	gtk_object_set_data (GTK_OBJECT (view), SO_VIEW_SHEET_CONTROL_KEY, scg);
	gtk_object_ref (GTK_OBJECT (scg));

	gtk_signal_connect (GTK_OBJECT (view), "destroy",
			    GTK_SIGNAL_FUNC (sheet_object_view_destroyed), so);
	so->realized_list = g_list_prepend (so->realized_list, view);

	SO_CLASS (so)->update_bounds (so, view, scg);
}

/**
 * sheet_object_realize:
 *
 * Creates a view of an object for every control associated with the object's
 * sheet.
 */
void
sheet_object_realize (SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (IS_SHEET (so->sheet));

	SHEET_FOREACH_CONTROL (so->sheet, control,
		sheet_object_new_view (so, (SheetControlGUI *) control););
}

void
sheet_object_print (SheetObject const *so, GnomePrintContext *ctx, 
		    double base_x, double base_y)
{
	if (SO_CLASS (so)->print)
		SO_CLASS (so)->print (so, ctx, base_x, base_y);
	else
		g_warning ("Un-printable sheet object");
}

SheetObject *
sheet_object_read_xml (XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObject *so;
	char *tmp;
	int tmp_int;
	GtkObject *obj;

	/* Old crufty IO */
	if (!strcmp (tree->name, "Rectangle")){
		so = sheet_object_box_new (FALSE);
	} else if (!strcmp (tree->name, "Ellipse")){
		so = sheet_object_box_new (TRUE);
	} else if (!strcmp (tree->name, "Arrow")){
		so = sheet_object_line_new (TRUE);
	} else if (!strcmp (tree->name, "Line")){
		so = sheet_object_line_new (FALSE);
	} else {
		obj = gtk_object_new (gtk_type_from_name ((gchar *)tree->name), NULL);
		if (!obj)
			return (NULL);
	
		so = SHEET_OBJECT (obj);
	}

	if (SO_CLASS (so)->read_xml &&
	    SO_CLASS (so)->read_xml (so, ctxt, tree)) {
		gtk_object_destroy (GTK_OBJECT (so));
		return NULL;
	}

	tmp = (char *) xmlGetProp (tree, (xmlChar *)"ObjectBound");
	if (tmp != NULL) {
		Range r;
		if (parse_range (tmp, &r))
			so->anchor.cell_bound = r;
		xmlFree (tmp);
	}

	tmp =  (char *) xmlGetProp (tree, (xmlChar *)"ObjectOffset");
	if (tmp != NULL) {
		sscanf (tmp, "%g %g %g %g",
			so->anchor.offset +0, so->anchor.offset +1,
			so->anchor.offset +2, so->anchor.offset +3);
		xmlFree (tmp);
	}

	tmp = (char *) xmlGetProp (tree, (xmlChar *)"ObjectAnchorType");
	if (tmp != NULL) {
		int i[4], count;
		sscanf (tmp, "%d %d %d %d", i+0, i+1, i+2, i+3);

		for (count = 4; count-- > 0 ; )
			so->anchor.type[count] = i[count];
		xmlFree (tmp);
	}

	if (xml_node_get_int (tree, "Direction", &tmp_int))
		so->anchor.direction = tmp_int;
	else
		so->anchor.direction = SO_DIR_UNKNOWN;

	sheet_object_set_sheet (so, ctxt->sheet);
	return so;
}

xmlNodePtr
sheet_object_write_xml (SheetObject const *so, XmlParseContext const *ctxt)
{
	GtkObject *obj;
	xmlNodePtr tree;
	char buffer[4*(DBL_DIG+10)];

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	obj = GTK_OBJECT (so);

	if (SO_CLASS (so)->write_xml == NULL)
		return NULL;

	tree = xmlNewDocNode (ctxt->doc, ctxt->ns,
			      (xmlChar *)gtk_type_name (GTK_OBJECT_TYPE (obj)), NULL);

	if (tree == NULL)
		return NULL;
 
	if (SO_CLASS (so)->write_xml (so, ctxt, tree)) {
		xmlUnlinkNode (tree);
		xmlFreeNode (tree);
		return NULL;
	}

	xml_node_set_cstr (tree, "ObjectBound", range_name (&so->anchor.cell_bound));
	snprintf (buffer, sizeof (buffer), "%.*g %.*g %.*g %.*g",
		  DBL_DIG, so->anchor.offset [0], DBL_DIG, so->anchor.offset [1],
		  DBL_DIG, so->anchor.offset [2], DBL_DIG, so->anchor.offset [3]);
	xml_node_set_cstr (tree, "ObjectOffset", buffer);
	snprintf (buffer, sizeof (buffer), "%d %d %d %d",
		  so->anchor.type [0], so->anchor.type [1],
		  so->anchor.type [2], so->anchor.type [3]);
	xml_node_set_cstr (tree, "ObjectAnchorType", buffer);
	xml_node_set_int (tree, "Direction", so->anchor.direction);

	return tree;
}

Range const *
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

static int
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col,
			SheetObjectAnchorType anchor_type, float offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return .5 + (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_position_pixels_get :
 *
 * @so : The sheet object
 * @coords : array of 4 ints where we return the coordinates in pixels
 *
 * Calculate the position of the object @so in pixels from the logical position
 * in the object.
 */
void
sheet_object_position_pixels_get (SheetObject const *so,
				  SheetControl const *sc, double *coords)
{
	Range const *r;

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

	*w = SO_CLASS(so)->default_width_pts;
	*h = SO_CLASS(so)->default_height_pts;
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
	Range const *r;

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
 *
 * Uses the relocation info and the anchors to decide whether or not, and how
 * to relocate objects when the grid moves (eg ins/del col/row).
 */
void
sheet_objects_relocate (ExprRelocateInfo const *rinfo, gboolean update)
{
	GList   *ptr, *next;
	Range	 dest;
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
			Range const *r  = &so->anchor.cell_bound;
			if (range_contains (&dest, r->start.col, r->start.row))
				gtk_object_destroy (GTK_OBJECT (so));
		}
		g_list_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		Range       *r  = &so->anchor.cell_bound;

		next = ptr->next;
		if (range_contains (&rinfo->origin,
				    r->start.col, r->start.row)) {
			/* FIXME : just moving the range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (r, rinfo->col_offset, rinfo->row_offset)) {
				gtk_object_destroy (GTK_OBJECT (so));
				continue;
			}
			if (change_sheets) {
				sheet_object_clear_sheet (so);
				sheet_object_set_sheet (so, rinfo->target_sheet);
			} else if (update)
				sheet_object_update_bounds (so, NULL);
		} else if (!change_sheets &&
			   range_contains (&dest, r->start.col, r->start.row)) {
			gtk_object_destroy (GTK_OBJECT (so));
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
sheet_objects_get (Sheet const *sheet, Range const *r, GtkType t)
{
	GSList *res = NULL;
	GList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GtkObject *obj = GTK_OBJECT (ptr->data);

		if (t == GTK_TYPE_NONE || t == GTK_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->anchor.cell_bound))
				res = g_slist_prepend (res, so);
		}
	}
	return res;
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
sheet_objects_clear (Sheet const *sheet, Range const *r, GtkType t)
{
	GList *ptr, *next;

	g_return_if_fail (IS_SHEET (sheet));

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = next ) {
		GtkObject *obj = GTK_OBJECT (ptr->data);
		next = ptr->next;
		if (t == GTK_TYPE_NONE || t == GTK_OBJECT_TYPE (obj)) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->anchor.cell_bound))
				gtk_object_destroy (GTK_OBJECT (so));
		}
	}
}

#ifdef ENABLE_BONOBO
/* Do NOT include the relevant header files,
 * they introduce automake depends in the non-bonobo build.
 */
extern GtkType sheet_object_bonobo_get_type (void);
extern GtkType gnm_graph_get_type (void);
#endif

void
sheet_object_register (void)
{
	SHEET_OBJECT_GRAPHIC_TYPE;
	SHEET_OBJECT_FILLED_TYPE;
	CELL_COMMENT_TYPE;
#ifdef ENABLE_BONOBO
	sheet_object_bonobo_get_type ();
	gnm_graph_get_type ();
#endif
	sheet_object_widget_register ();
}

/**
 * sheet_object_clone:
 * @so: The Sheet Object to clone
 * @sheet: The sheet that we should attach the sheet object to
 * 
 * Clones a sheet object and attaches it to @sheet
 * 
 * Return Value: 
 **/
static SheetObject *
sheet_object_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObject *new_so = NULL;

	if (!SO_CLASS (so)->clone) {
		static gboolean warned = FALSE;
		if (!warned) {
			g_warning ("Some objects are lacking a clone method and will not"
				   "be duplicated.");
			warned = TRUE;
		}
		return NULL;
	}
		
	new_so = SO_CLASS (so)->clone (so, sheet);

	new_so->type = so->type;
	sheet_object_anchor_cpy (&new_so->anchor, &so->anchor);
	sheet_object_set_sheet (new_so, sheet);
		
	return new_so;
}


/**
 * sheet_object_clone_sheet:
 * @src: The source sheet to read the objects from
 * @dst: The destination sheet to attach the objects to
 * 
 * Clones the objects of the src sheet and attaches them into the dst sheet
 **/
void
sheet_object_clone_sheet (const Sheet *src, Sheet *dst)
{
	SheetObject *so;
	SheetObject *new_so;
	GList *list;
	GList *new_list = NULL;

	g_return_if_fail (IS_SHEET (dst));
	g_return_if_fail (dst->sheet_objects == NULL);
	
	list = src->sheet_objects;
	for (; list != NULL; list = list->next) {
		so = (SheetObject *) list->data;
		new_so = sheet_object_clone (so, dst);
		if (new_so != NULL)
			new_list = g_list_prepend (new_list, new_so);
	}

	dst->sheet_objects = g_list_reverse (new_list);
}


/**
 * sheet_object_direction_set:
 * @so: The sheet object that we are calculating the direction for
 * @coords: array of coordinates in L,T,R,B order
 * 
 * Sets the object direction from the given the new coordinates
 * The original coordinates are assumed to be normalized (so that top
 * is above bottom and right is at the right of left)
 * 
 **/
void
sheet_object_direction_set (SheetObject *so, gdouble *coords)
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

/*****************************************************************************/

/**
 * sheet_object_anchor_init :
 *
 * A utility routine to initialize an anchor.  Useful in case we add
 * fields in the future and want to ensure that everything is initialized.
 */
void
sheet_object_anchor_init (SheetObjectAnchor *anchor,
			  Range const *r, float const *offsets,
			  SheetObjectAnchorType const *types,
			  SheetObjectDirection direction)
{
	int i;

	if (r == NULL) {
		static Range const defaultVal = { { 0, 0 }, { 1, 1 } };
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
