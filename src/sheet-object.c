/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg   (jgoldberg@home.com)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "sheet-control-gui.h"
#include "gnumeric-type-util.h"
#include "dialogs.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "sheet.h"
#include "expr.h"
#include "ranges.h"

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(GTK_OBJECT(so)->klass)

#define	SO_VIEW_SHEET_CONTROL_KEY	"SheetControl"
#define	SO_VIEW_OBJECT_KEY		"SheetObject"

GtkType    sheet_object_get_type  (void);
static GtkObjectClass *sheet_object_parent_class;

static void
sheet_object_remove_cb (GtkWidget *widget, SheetObject *so)
{
	gtk_object_destroy (GTK_OBJECT (so));
}

static void
cb_sheet_object_configure (GtkWidget *widget, GnomeCanvasItem *obj_view)
{
	SheetControlGUI *scg;
	SheetObject *so;

	g_return_if_fail (obj_view != NULL);

	so = gtk_object_get_data (GTK_OBJECT (obj_view),
				  SO_VIEW_OBJECT_KEY);
	scg = gtk_object_get_data (GTK_OBJECT (obj_view),
				   SO_VIEW_SHEET_CONTROL_KEY);

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
			    GnomeCanvasItem *obj_view,
			    GtkMenu *menu)
{
	GtkWidget *item = gnome_stock_menu_item (GNOME_STOCK_MENU_CLOSE,
						 _("Delete"));

	gtk_menu_append (menu, item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (sheet_object_remove_cb), so);

	if (SO_CLASS(so)->user_config != NULL) {
		item = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP,
					      _("Configure"));
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (cb_sheet_object_configure), obj_view);
		gtk_menu_append (menu, item);
	}
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

static void
sheet_object_destroy (GtkObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET (so->sheet));

	gnome_canvas_points_free (so->bbox_points);
	sheet_object_unrealize (so);

	/* If the object has already been inserted then mark sheet as dirty */
	if (NULL != g_list_find	(so->sheet->sheet_objects, so)) {
		so->sheet->sheet_objects  = g_list_remove (so->sheet->sheet_objects, so);
		so->sheet->modified = TRUE;
	}

	so->sheet = NULL;
	(*sheet_object_parent_class->destroy)(object);
}

static void
sheet_object_init (GtkObject *object)
{
	int i;
	SheetObject *so = SHEET_OBJECT (object);

	so->type = SHEET_OBJECT_ACTION_STATIC;

	/* Store the logical position as A1 */
	so->cell_bound.start.col = so->cell_bound.start.row = 0;
	so->cell_bound.end.col = so->cell_bound.end.row = 1;

	for (i = 4; i-- > 0 ;) {
		so->offset [i] = 0.;
		so->anchor_type [i] = SO_ANCHOR_UNKNOWN;
	}

	so->bbox_points = gnome_canvas_points_new (2);
}

static void
sheet_object_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = sheet_object_destroy;
	sheet_object_class->update_bounds = NULL;
	sheet_object_class->populate_menu = sheet_object_populate_menu;
	sheet_object_class->print         = NULL;
	sheet_object_class->user_config   = NULL;
}

GNUMERIC_MAKE_TYPE (sheet_object, "SheetObject", SheetObject,
		    sheet_object_class_init, sheet_object_init,
		    gtk_object_get_type ())

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
sheet_object_get_view (SheetObject *so, SheetControlGUI *scg)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *obj = GTK_OBJECT (l->data);
		if (scg == sheet_object_view_control (obj))
			return obj;
	}

	return NULL;
}

void
sheet_object_position (SheetObject *so, CellPos const *pos)
{
	GList *l;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (pos != NULL &&
	    so->cell_bound.end.col <= pos->col &&
	    so->cell_bound.end.row <= pos->row)
		return;

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *view = GTK_OBJECT (l->data);
		SO_CLASS (so)->update_bounds (so, view,
			sheet_object_view_control (view));
	}
}

/**
 * sheet_object_construct :
 *
 * TODO : decide what those units are and enable the spcification
 * of different anchor formats.
 */
void
sheet_object_construct (SheetObject *so, Sheet *sheet,
			int default_width, int default_height)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (IS_SHEET (sheet));

	sheet->sheet_objects = g_list_prepend (sheet->sheet_objects, so);
	so->sheet       = sheet;
	sheet_object_position (so, NULL);
}

/**
 * DEPRECATED USELESS
 *
 * sheet_object_set_bounds:
 * @so: The sheet object we are interested in
 * @tlx: top left x position
 * @tly: top left y position
 * @brx: bottom right x position
 * @bry: bottom right y position
 *
 *  This sets the co-ordinates of the bounding box and
 * does any neccessary housekeeping.
 *
 * DEPRECATED USELESS
 **/
void
sheet_object_set_bounds (SheetObject *so,
			 double l, double t, double r, double b)
{
	g_return_if_fail (so != NULL);
	g_return_if_fail (so->bbox_points != NULL);

	/* We do the MIN / MAX business on the get */
	so->bbox_points->coords [0] = l;
	so->bbox_points->coords [1] = t;
	so->bbox_points->coords [2] = r;
	so->bbox_points->coords [3] = b;

	sheet_object_position (so, NULL);
}

static void
sheet_object_view_destroyed (GtkObject *view, SheetObject *so)
{
	so->realized_list = g_list_remove (so->realized_list, view);
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

	SHEET_FOREACH_CONTROL (so->sheet, control,
		sheet_object_new_view (so, control););
}

void
sheet_object_print (SheetObject const *so, SheetObjectPrintInfo const *pi)
{
	if (SO_CLASS (so)->print)
		SO_CLASS (so)->print (so, pi);
	else
		g_warning ("Un-printable sheet object");
}

gboolean
sheet_object_read_xml (CommandContext *cc, SheetObject *so, xmlNodePtr tree)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);
	return SO_CLASS (so)->read_xml (cc, so, tree);
}

xmlNodePtr
sheet_object_write_xml (SheetObject const *so, xmlDocPtr doc, xmlNsPtr ns,
			XmlSheetObjectWriteFn write_fn)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	return SO_CLASS (so)->write_xml (so, doc, ns, write_fn);
}
Range const *
sheet_object_range_get (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->cell_bound;
}

void
sheet_object_range_set (SheetObject *so, Range const *r,
			float const *offsets, SheetObjectAnchor const *types)
{
	int i;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (r != NULL)
		so->cell_bound = *r;
	if (offsets != NULL)
		for (i = 4; i-- > 0 ; )
			so->offset [i] = offsets [i];
	if (types != NULL)
		for (i = 4; i-- > 0 ; )
			so->anchor_type [i] = types [i];

	sheet_object_position (so, NULL);
}

static int
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col,
			SheetObjectAnchor anchor_type, float offset)
{
	ColRowInfo const *cri = is_col
		? sheet_col_get_info (sheet, i)
		: sheet_row_get_info (sheet, i);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_position_pixels :
 *
 * @so : The sheet object
 * @coords : array of 4 ints
 *
 * Calculate the position of the object @so in pixels from the logical position
 * in the object.
 */
void
sheet_object_position_pixels (SheetObject const *so, int *coords)
{
	coords [0] = sheet_col_get_distance_pixels (so->sheet, 0,
		so->cell_bound.start.col);
	coords [1] = sheet_row_get_distance_pixels (so->sheet, 0,
		so->cell_bound.start.row);

	coords [2] = coords [0] + sheet_col_get_distance_pixels (so->sheet,
		so->cell_bound.start.col, so->cell_bound.end.col);
	coords [3] = coords [1] + sheet_row_get_distance_pixels (so->sheet,
		so->cell_bound.start.row, so->cell_bound.end.row);

	coords [0] += cell_offset_calc_pixel (so->sheet, so->cell_bound.start.col,
		TRUE, so->anchor_type [0], so->offset [0]);
	coords [1] += cell_offset_calc_pixel (so->sheet, so->cell_bound.start.row,
		FALSE, so->anchor_type [1], so->offset [1]);
	coords [2] += cell_offset_calc_pixel (so->sheet, so->cell_bound.end.col,
		TRUE, so->anchor_type [2], so->offset [2]);
	coords [3] += cell_offset_calc_pixel (so->sheet, so->cell_bound.end.row,
		FALSE, so->anchor_type [3], so->offset [3]);
}

static double
cell_offset_calc_pt (Sheet const *sheet, int i, gboolean is_col,
		     SheetObjectAnchor anchor_type, float offset)
{
	ColRowInfo const *cri = is_col
		? sheet_col_get_info (sheet, i)
		: sheet_row_get_info (sheet, i);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_position_pts :
 *
 * @so : The sheet object
 * @coords : array of 4 doubles
 *
 * Calculate the position of the object @so in pts from the logical position in
 * the object.
 */
void
sheet_object_position_pts (SheetObject const *so, double *coords)
{
	coords [0] = sheet_col_get_distance_pts (so->sheet, 0,
		so->cell_bound.start.col);
	coords [2] = coords [0] + sheet_col_get_distance_pts (so->sheet,
		so->cell_bound.start.col, so->cell_bound.end.col);
	coords [1] = sheet_row_get_distance_pts (so->sheet, 0,
		so->cell_bound.start.row);
	coords [3] = coords [1] + sheet_row_get_distance_pts (so->sheet,
		so->cell_bound.start.row, so->cell_bound.end.row);

	coords [0] += cell_offset_calc_pt (so->sheet, so->cell_bound.start.col,
		TRUE, so->anchor_type [0], so->offset [0]);
	coords [1] += cell_offset_calc_pt (so->sheet, so->cell_bound.start.row,
		FALSE, so->anchor_type [1], so->offset [1]);
	coords [2] += cell_offset_calc_pt (so->sheet, so->cell_bound.end.col,
		TRUE, so->anchor_type [2], so->offset [2]);
	coords [3] += cell_offset_calc_pt (so->sheet, so->cell_bound.end.row,
		FALSE, so->anchor_type [3], so->offset [3]);
}

/**
 * sheet_relocate_objects :
 *
 * @sheet : the sheet.
 * @rinfo : details on what should be moved.
 */
void
sheet_relocate_objects (ExprRelocateInfo const *rinfo)
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
		GList *copy = rinfo->target_sheet->sheet_objects;
		for (ptr = copy; ptr != NULL ; ptr = next ) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			next = ptr->next;
			if (range_contains (&dest,
					    so->cell_bound.start.col,
					    so->cell_bound.start.row))
				gtk_object_destroy (GTK_OBJECT (so));
		}
		g_list_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		next = ptr->next;
		if (range_contains (&rinfo->origin,
				    so->cell_bound.start.col,
				    so->cell_bound.start.row)) {
			/* FIXME : just movingthe range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (&so->cell_bound, rinfo->col_offset, rinfo->row_offset)) {
				gtk_object_destroy (GTK_OBJECT (so));
				continue;
			}
		} else if (!change_sheets &&
			   range_contains (&dest,
					   so->cell_bound.start.col,
					   so->cell_bound.start.row)) {
			gtk_object_destroy (GTK_OBJECT (so));
			continue;
		}

		if (change_sheets) {
			sheet_object_unrealize (so);
			so->sheet = rinfo->target_sheet;
			sheet_object_realize (so);
		}
		sheet_object_position (so, NULL);
	}
}

/**
 * sheet_get_objects :
 *
 * @sheet : the sheet.
 * @r     : an optional range to look in
 * @t     : The type of object to lookup
 *
 * Returns a list of which the caller must free (just the list not the content).
 * Containing all objects of exactly the specified type (inheritence does not count).
 */
GList *
sheet_get_objects (Sheet const *sheet, Range const *r, GtkType t)
{
	GList *res = NULL;
	GList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GtkObject *obj = GTK_OBJECT (ptr->data);

		if (obj->klass->type == t) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->cell_bound))
				res = g_list_prepend (res, so);
		}
	}
	return res;
}
