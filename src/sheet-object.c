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
#include <math.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "sheet-control-gui.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "sheet-object.h"
#include "workbook-edit.h"

#include <gal/widgets/e-cursors.h>

#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(GTK_OBJECT(so)->klass)

static GtkObjectClass *sheet_object_parent_class;

static void sheet_object_stop_editing  (SheetObject *so);
static void sheet_object_populate_menu (SheetObject *so,
				        GnomeCanvasItem *obj_view,
				        GtkMenu *menu);
static int  control_point_handle_event (GnomeCanvasItem *item,
				        GdkEvent *event,
				        SheetObject *so);
static void update_bbox 	       (SheetObject *so);

static void
sheet_object_destroy (GtkObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);
	Sheet *sheet;

	g_return_if_fail (so != NULL);

	sheet = so->sheet;
	g_return_if_fail (sheet != NULL);

	if (sheet->current_object == so)
		sheet_mode_edit (sheet);
	else
		sheet_object_stop_editing (so);

	gnome_canvas_points_free (so->bbox_points);
	while (so->realized_list) {
		GnomeCanvasItem *item = so->realized_list->data;
		gtk_object_destroy (GTK_OBJECT (item));
	}

	/* If the object has already been inserted then mark sheet as dirty */
	if (NULL != g_list_find	(sheet->sheet_objects, so)) {
		sheet->sheet_objects  = g_list_remove (sheet->sheet_objects, so);
		sheet->modified = TRUE;
	}

	(*sheet_object_parent_class->destroy)(object);
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

GtkType
sheet_object_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObject",
			sizeof (SheetObject),
			sizeof (SheetObjectClass),
			(GtkClassInitFunc) sheet_object_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_object_get_type (), &info);
	}

	return type;
}

void
sheet_object_construct (SheetObject *so, Sheet *sheet)
{
	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	so->type        = SHEET_OBJECT_ACTION_STATIC;
	so->sheet       = sheet;
	so->dragging    = FALSE;
	so->bbox_points = gnome_canvas_points_new (2);

	sheet->sheet_objects  = g_list_prepend (sheet->sheet_objects, so);

	sheet->modified = TRUE;
}

/**
 * sheet_object_get_bounds:
 * @so: The sheet object we are interested in
 * @l: set to the left position
 * @t: set to the top position
 * @r: set to the right position
 * @b: set to the bottom position
 *
 *   This function returns the bounds of the sheet object
 * in the pointers it is passed.
 *
 **/
void
sheet_object_get_bounds (SheetObject *so, double *l, double *t,
			 double *r, double *b)
{
	g_return_if_fail (so != NULL);
	g_return_if_fail (l != NULL);
	g_return_if_fail (t != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (b != NULL);

	*l = MIN (so->bbox_points->coords [0],
		  so->bbox_points->coords [2]);
	*r = MAX (so->bbox_points->coords [0],
		  so->bbox_points->coords [2]);
	*t = MIN (so->bbox_points->coords [1],
		  so->bbox_points->coords [3]);
	*b = MAX (so->bbox_points->coords [1],
		  so->bbox_points->coords [3]);
}

/**
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
 **/
void
sheet_object_set_bounds (SheetObject *so, double tlx, double tly,
			 double brx, double bry)
{
	g_return_if_fail (so != NULL);
	g_return_if_fail (so->bbox_points != NULL);

	/* We do the MIN / MAX business on the get */

	so->bbox_points->coords [0] = tlx;
	so->bbox_points->coords [1] = tly;
	so->bbox_points->coords [2] = brx;
	so->bbox_points->coords [3] = bry;
}

static void
sheet_object_item_destroyed (GnomeCanvasItem *item, SheetObject *so)
{
	so->realized_list = g_list_remove (so->realized_list, item);
}

/*
 * sheet_object_new_view
 *
 * Creates a GnomeCanvasItem for a SheetControlGUI and sets up the event
 * handlers.
 */
GnomeCanvasItem *
sheet_object_new_view (SheetObject *so, SheetControlGUI *sheet_view)
{
	GnomeCanvasItem *item;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (sheet_view), NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	item = SO_CLASS (so)->new_view (so, sheet_view);

	if (item == NULL) {
		g_warning ("Unable to create a view for this object\n");
		return NULL;
	}

	/* Store some useful information */
	gtk_object_set_data (GTK_OBJECT (item),
			     SHEET_OBJ_VIEW_OBJECT_KEY, so);
	gtk_object_set_data (GTK_OBJECT (item),
			     SHEET_OBJ_VIEW_SHEET_CONTROL_GUI_KEY, sheet_view);

	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (sheet_object_canvas_event), so);
	gtk_signal_connect (GTK_OBJECT (item), "destroy",
			    GTK_SIGNAL_FUNC (sheet_object_item_destroyed), so);
	so->realized_list = g_list_prepend (so->realized_list, item);

	return item;
}

/*
 * sheet_object_remove_view
 *
 * Removes the object from the canvas in the SheetControlGUI.
 */
static void
sheet_object_remove_view (SheetObject *so, SheetControlGUI *sheet_view)
{
	GList *l;

	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_CONTROL_GUI (sheet_view));
	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	for (l = so->realized_list; l; l = l->next) {
		GnomeCanvasItem *item   = GNOME_CANVAS_ITEM (so->realized_list->data);
		GnumericSheet   *gsheet = GNUMERIC_SHEET (item->canvas);

		if (gsheet->sheet_view != sheet_view)
			continue;

		gtk_object_destroy (GTK_OBJECT (item));
		break;
	}
}

/*
 * sheet_object_realize
 *
 * Realizes the on a Sheet (this in turn realizes the object
 * on every existing SheetControlGUI)
 */
void
sheet_object_realize (SheetObject *so)
{
	GList *l;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	for (l = so->sheet->sheet_views; l; l = l->next) {
		SheetControlGUI *sheet_view = l->data;
		GnomeCanvasItem *item;

		item = sheet_object_new_view (so, sheet_view);
	}
}

/*
 * sheet_object_unrealize
 *
 * Destroys the Canvas Item that represents this SheetObject from
 * every SheetControlGUI.
 */
void
sheet_object_unrealize (SheetObject *so)
{
	GList *l;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	for (l = so->sheet->sheet_views; l; l = l->next) {
		SheetControlGUI *sheet_view = l->data;

		sheet_object_remove_view (so, sheet_view);
	}
}

typedef struct {
	Sheet *sheet;
	double x, y;
	gboolean has_been_sized;
	GnomeCanvasItem *item;
} SheetObjectCreationData;

/*
 * cb_obj_create_motion:
 * @gsheet :
 * @event :
 * @closure :
 *
 * Rubber band a rectangle to show where the object is going to go,
 * and support autoscroll.
 *
 * TODO : Add autoscroll.
 */
static gboolean
cb_obj_create_motion (GnumericSheet *gsheet, GdkEventMotion *event,
		      SheetObjectCreationData *closure)
{
	double tmp_x, tmp_y;
	double x1, x2, y1, y2;

	g_return_val_if_fail (gsheet != NULL, TRUE);

	gnome_canvas_window_to_world (GNOME_CANVAS (gsheet),
				      event->x, event->y,
				      &tmp_x, &tmp_y);

	if (tmp_x < closure->x) {
		x1 = tmp_x;
		x2 = closure->x;
	} else {
		x2 = tmp_x;
		x1 = closure->x;
	}
	if (tmp_y < closure->y) {
		y1 = tmp_y;
		y2 = closure->y;
	} else {
		y2 = tmp_y;
		y1 = closure->y;
	}

	if (!closure->has_been_sized) {
		closure->has_been_sized =
		    (fabs (tmp_x - closure->x) > 5.) ||
		    (fabs (tmp_y - closure->y) > 5.);
	}

	gnome_canvas_item_set (closure->item,
			       "x1", x1, "y1", y1,
			       "x2", x2, "y2", y2,
			       NULL);

	return TRUE;
}

/*
 * cb_obj_create_button_release
 *
 * Invoked as the last step in object creation.
 */
static gboolean
cb_obj_create_button_release (GnumericSheet *gsheet, GdkEventButton *event,
			      SheetObjectCreationData *closure)
{
	double x1, y1, x2, y2;
	SheetObject *so;
	Sheet *sheet;

	g_return_val_if_fail (gsheet != NULL, 1);
	g_return_val_if_fail (closure != NULL, -1);
	g_return_val_if_fail (closure->sheet != NULL, -1);
	g_return_val_if_fail (closure->sheet->new_object != NULL, -1);

	sheet = closure->sheet;
	so = sheet->new_object;
	sheet_set_dirty (sheet, TRUE);

	/* If there has been some motion use the press and release coords */
	if (closure->has_been_sized) {
		x1 = closure->x;
		y1 = closure->y;
		gnome_canvas_window_to_world (GNOME_CANVAS (gsheet),
					      event->x, event->y, &x2, &y2);
	} else {
	/* Otherwise translate default size to use release point as top left */
		sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);
		x2 -= x1;	 y2 -= y1;
		x1 = closure->x; y1 = closure->y;
		x2 += x1;	 y2 += y1;
	}
	sheet_object_set_bounds (so, x1, y1, x2, y2);
	SO_CLASS(so)->update_bounds (so);
	sheet_object_realize (so);

	gtk_signal_disconnect_by_func (
		GTK_OBJECT (gsheet),
		GTK_SIGNAL_FUNC (cb_obj_create_motion), closure);
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (gsheet),
		GTK_SIGNAL_FUNC (cb_obj_create_button_release), closure);

	gtk_object_destroy (GTK_OBJECT (closure->item));
	g_free (closure);

	/* move object from creation to edit mode */
	sheet->new_object = NULL;
	sheet_mode_edit_object (so);

	return TRUE;
}

/*
 * sheet_object_begin_creation
 *
 * Starts the process of creating a SheetObject.  Handles the initial
 * button press on the GnumericSheet.
 */
gboolean
sheet_object_begin_creation (GnumericSheet *gsheet, GdkEventButton *event)
{
	Sheet *sheet;
	SheetObject *so;
	SheetObjectCreationData *closure;

	g_return_val_if_fail (gsheet != NULL, TRUE);
	g_return_val_if_fail (gsheet->sheet_view != NULL, TRUE);

	sheet = gsheet->sheet_view->sheet;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (sheet->current_object == NULL, TRUE);
	g_return_val_if_fail (sheet->new_object != NULL, TRUE);

	so = sheet->new_object;

	closure = g_new (SheetObjectCreationData, 1);
	closure->sheet = sheet;
	closure->has_been_sized = FALSE;
	closure->x = event->x;
	closure->y = event->y;

	closure->item = gnome_canvas_item_new (
		gsheet->sheet_view->object_group,
		gnome_canvas_rect_get_type (),
		"outline_color", "black",
		"width_units",   2.0,
		NULL);

	gtk_signal_connect (GTK_OBJECT (gsheet), "button_release_event",
			    GTK_SIGNAL_FUNC (cb_obj_create_button_release), closure);
	gtk_signal_connect (GTK_OBJECT (gsheet), "motion_notify_event",
			    GTK_SIGNAL_FUNC (cb_obj_create_motion), closure);

	return TRUE;
}

static gboolean
sheet_mode_clear (Sheet *sheet)
{
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (sheet->new_object != NULL) {
		gtk_object_unref (GTK_OBJECT (sheet->new_object));
		sheet->new_object = NULL;
	}
	sheet_object_stop_editing (sheet->current_object);

	return TRUE;
}

/*
 * sheet_mode_edit:
 * @sheet:  The sheet
 *
 * Put @sheet into the standard state 'edit mode'.  This shuts down
 * any object editing and frees any objects that are created but not
 * realized.
 */
void
sheet_mode_edit	(Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);

	sheet_mode_clear (sheet);
	sheet_show_cursor (sheet);

#warning FIXME : this function has no business operating on the model.
#if 0
	if (workbook_edit_has_guru (sheet->workbook))
		workbook_finish_editing (sheet->workbook, FALSE);
#endif
}

/*
 * sheet_mode_edit_object
 * @so : The SheetObject to select.
 *
 * Makes @so the currently selected object and prepares it for
 * user editing.
 */
void
sheet_mode_edit_object (SheetObject *so)
{
	Sheet *sheet;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	sheet = so->sheet;
	if (sheet_mode_clear (sheet)) {
		sheet->current_object = so;
		if (SO_CLASS (so)->set_active != NULL)
			SO_CLASS (so)->set_active (so, TRUE);
		update_bbox (so);
		sheet_hide_cursor (sheet);
	}
}

/**
 * sheet_mode_create_object :
 * @so : The object the needs to be placed
 *
 * Takes a newly created SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 */
void
sheet_mode_create_object (SheetObject *so)
{
	Sheet *sheet;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	sheet = so->sheet;
	if (sheet_mode_clear (sheet)) {
		sheet->new_object = so;
		sheet_hide_cursor (sheet);
	}
}

/*
 * sheet_object_destroy_control_points
 *
 * Destroys the canvas items used as sheet control points
 */
static void
sheet_object_destroy_control_points (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next) {
		int i;
		SheetControlGUI *sheet_view = SHEET_CONTROL_GUI (l->data);

		if (sheet_view == NULL)
			return;

		i = sizeof (sheet_view->control_points)/sizeof(GnomeCanvasItem *);
		while (i-- > 0) {
			gtk_object_destroy (GTK_OBJECT (sheet_view->control_points [i]));
			sheet_view->control_points [i] = NULL;
		}
	}
}

static void
sheet_object_stop_editing (SheetObject *so)
{
	if (so != NULL) {
		Sheet *sheet = so->sheet;

		if (so == sheet->current_object) {
			sheet_object_destroy_control_points (sheet);
			sheet->current_object = NULL;
			if (SO_CLASS (so)->set_active != NULL)
				SO_CLASS (so)->set_active (so, FALSE);
#ifdef ENABLE_BONOBO
	/* FIXME FIXME FIXME : JEG 11/Sep/2000 */
	if (sheet->active_object_frame) {
		bonobo_view_frame_view_deactivate (sheet->active_object_frame);
		if (sheet->active_object_frame != NULL)
			bonobo_view_frame_set_covered (sheet->active_object_frame, TRUE);
		sheet->active_object_frame = NULL;
	}
#endif
		}
	}
}

/*
 * new_control_point
 * @group:  The canvas group to which this control point belongs
 * @so:     The sheet object
 * @idx:    control point index to be created
 * @x:      x coordinate of control point
 * @y:      y coordinate of control point
 *
 * This is used to create a number of control points in a sheet
 * object, the meaning of them is used in other parts of the code
 * to belong to the following locations:
 *
 *     0 -------- 1 -------- 2
 *     |                     |
 *     3                     4
 *     |                     |
 *     5 -------- 6 -------- 7
 */
static GnomeCanvasItem *
new_control_point (GnomeCanvasGroup *group, SheetObject *so,
		   int idx, double x, double y, ECursorType ct)
{
	GnomeCanvasItem *item;

	item = gnome_canvas_item_new (
		group,
		gnome_canvas_rect_get_type (),
		"x1",    x - 2,
		"y1",    y - 2,
		"x2",    x + 2,
		"y2",    y + 2,
		"outline_color", "black",
		"fill_color",    "black",
		NULL);

	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (control_point_handle_event),
			    so);

	gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (idx));
	gtk_object_set_data (GTK_OBJECT (item), "cursor", GINT_TO_POINTER (ct));

	return item;
}

/*
 * set_item_x_y:
 *
 * changes the x and y position of the idxth control point,
 * creating the control point if necessary.
 */
static void
set_item_x_y (SheetObject *so, SheetControlGUI *sheet_view, int idx,
	      double x, double y, ECursorType ct)
{
	if (sheet_view->control_points [idx] == NULL)
		sheet_view->control_points [idx] = new_control_point (
			sheet_view->object_group, so, idx, x, y, ct);
	else
		gnome_canvas_item_set (
		       sheet_view->control_points [idx],
		       "x1", x - 2,
		       "x2", x + 2,
		       "y1", y - 2,
		       "y2", y + 2,
		       NULL);
}

static void
set_acetate_coords (SheetObject *so, SheetControlGUI *sheet_view,
		    double l, double t, double r, double b)
{
	l -= 10.; r += 10.;
	t -= 10.; b += 10.;

	if (sheet_view->control_points [8] == NULL) {
		GnomeCanvasItem *item;
		GtkWidget *event_box = gtk_event_box_new ();

		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_widget_get_type (),
			"widget", event_box,
			"x",      l,
			"y",      t,
			"width",  r - l + 1.,
			"height", b - t + 1.,
			NULL);
		gtk_signal_connect (GTK_OBJECT (item), "event",
				    GTK_SIGNAL_FUNC (control_point_handle_event),
				    so);
		gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (8));
		gtk_object_set_data (GTK_OBJECT (item), "cursor",
				     GINT_TO_POINTER (E_CURSOR_MOVE));

		sheet_view->control_points [8] = item;
	} else
		gnome_canvas_item_set (
		       sheet_view->control_points [8],
		       "x",      l,
		       "y",      t,
		       "width",  r - l + 1.,
		       "height", b - t + 1.,
		       NULL);
}
/**
 * update_bbox:
 * @sheet_object: The selected object.
 *
 *  This updates all the views this object appears in. It
 * re-aligns the control points so that they appear at the
 * correct verticies.
 **/
static void
update_bbox (SheetObject *so)
{
	GList *ptr;
	const double *c = so->bbox_points->coords;
	double l, t, r, b;

	l = c[0];
	t = c[1];
	r = c[2];
	b = c[3];
	for (ptr = so->sheet->sheet_views; ptr != NULL; ptr = ptr->next) {
		SheetControlGUI *sheet_view = ptr->data;

		/* set the acetate 1st so that the other points
		 * will override it
		 */
		set_acetate_coords (so, sheet_view, l, t, r, b);

		set_item_x_y (so, sheet_view, 0, l, t,
			      E_CURSOR_SIZE_TL);
		set_item_x_y (so, sheet_view, 1, (l + r) / 2, t,
			      E_CURSOR_SIZE_Y);
		set_item_x_y (so, sheet_view, 2, r, t,
			      E_CURSOR_SIZE_TR);
		set_item_x_y (so, sheet_view, 3, l, (t + b) / 2,
			      E_CURSOR_SIZE_X);
		set_item_x_y (so, sheet_view, 4, r, (t + b) / 2,
			      E_CURSOR_SIZE_X);
		set_item_x_y (so, sheet_view, 5, l, b,
			      E_CURSOR_SIZE_TR);
		set_item_x_y (so, sheet_view, 6, (l + r) / 2, b,
			      E_CURSOR_SIZE_Y);
		set_item_x_y (so, sheet_view, 7, r, b,
			      E_CURSOR_SIZE_TL);
	}
}

static void
sheet_object_remove_cb (GtkWidget *widget, SheetObject *so)
{
	gtk_object_destroy (GTK_OBJECT (so));
}

static void
cb_sheet_object_configure (GtkWidget *widget, GnomeCanvasItem *obj_view)
{
	SheetControlGUI *sheet_view;
	SheetObject *so;

	g_return_if_fail (obj_view != NULL);

	so = gtk_object_get_data (GTK_OBJECT (obj_view),
				  SHEET_OBJ_VIEW_OBJECT_KEY);
	sheet_view = gtk_object_get_data (GTK_OBJECT (obj_view),
					  SHEET_OBJ_VIEW_SHEET_CONTROL_GUI_KEY);

	SO_CLASS(so)->user_config (so, sheet_view);
}

/**
 * sheet_object_populate_menu:
 * @so:  the sheet object
 * @menu: the menu to insert into
 *
 * Add standard items to the object's popup menu.
 **/
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

static void
menu_unrealize_cb (GtkMenu *menu, SheetObject *so)
{
}

static void
display_object_menu (SheetObject *so, GnomeCanvasItem *item,
		     GdkEvent *event)
{
	GtkMenu *menu;

	sheet_mode_edit_object (so);
	menu = GTK_MENU (gtk_menu_new ());
	SO_CLASS (so)->populate_menu (so, item, menu);
	gtk_signal_connect (GTK_OBJECT (menu), "unrealize",
			    GTK_SIGNAL_FUNC (menu_unrealize_cb), so);

	gtk_widget_show_all (GTK_WIDGET (menu));
	gnumeric_popup_menu (menu, &event->button);
}

/*
 * control_point_handle_event :
 *
 * Event handler for the control points.
 * Index & cursor type are stored as user data associated with the CanvasItem
 */
static int
control_point_handle_event (GnomeCanvasItem *item, GdkEvent *event,
			    SheetObject *so)
{
	int idx;
	static gdouble last_x, last_y;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
	{
		gpointer p = gtk_object_get_data (GTK_OBJECT (item), "cursor");
		e_cursor_set_widget (item->canvas, GPOINTER_TO_UINT (p));
		break;
	}

	case GDK_BUTTON_RELEASE:
		if (!so->dragging)
			return FALSE;

		so->dragging = FALSE;
		gnome_canvas_item_ungrab (item, event->button.time);
		break;

	case GDK_BUTTON_PRESS:
		switch (event->button.button) {
		case 1:
		case 2:
			so->dragging = TRUE;
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL, event->button.time);
			last_x = event->button.x;
			last_y = event->button.y;
			break;

		case 3:
			display_object_menu (so, item, event);
			break;

		default:
			/* Ignore mouse wheel events */
			return FALSE;
		}
		break;

	case GDK_MOTION_NOTIFY: {
		double   *coords = so->bbox_points->coords;
		double    dx, dy;

		if (!so->dragging)
			return FALSE;

		idx = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (item)));

		dx = event->button.x - last_x;
		dy = event->button.y - last_y;
		last_x = event->button.x;
		last_y = event->button.y;

		switch (idx) {
		case 0:
			coords[0] += dx;
			coords[1] += dy;
			break;
		case 1:
			coords[1] += dy;
			break;
		case 2:
			coords[1] += dy;
			coords[2] += dx;
			break;
		case 3:
			coords[0] += dx;
			break;
		case 4:
			coords[2] += dx;
			break;
		case 5:
			coords[0] += dx;
			coords[3] += dy;
			break;
		case 6:
			coords[3] += dy;
			break;
		case 7:
			coords[2] += dx;
			coords[3] += dy;
			break;
		case 8:
			coords[0] += dx;
			coords[1] += dy;
			coords[2] += dx;
			coords[3] += dy;
			break;

		default:
			g_warning ("Should not happen %d", idx);
		}

		update_bbox (so);

		/* Tell the object to update its co-ordinates */
		SO_CLASS (so)->update_bounds (so);

		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

void
sheet_object_print (SheetObject *so, SheetObjectPrintInfo *pi)

{
	if (SO_CLASS (so)->print)
		SO_CLASS (so)->print (so, pi);
	else
		g_warning ("Un-printable sheet object");
}

/*
 * sheet_object_canvas_event
 *
 * Event handler for a SheetObject
 */
int
sheet_object_canvas_event (GnomeCanvasItem *item, GdkEvent *event,
			   SheetObject *so)
{
	static int event_last_x,  event_last_y;
	static int event_total_x, event_total_y;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		if (so->type == SHEET_OBJECT_ACTION_STATIC)
			e_cursor_set_widget (item->canvas, E_CURSOR_ARROW);
		else
			e_cursor_set_widget (item->canvas, E_CURSOR_PRESS);
		break;

	case GDK_BUTTON_PRESS:
	{
		switch (event->button.button) {
		case 1:
		case 2:
			sheet_object_stop_editing (so->sheet->current_object);

			so->dragging = TRUE;
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL, event->button.time);
			event_last_x = event->button.x;
			event_last_y = event->button.y;
			event_total_x = 0;
			event_total_y = 0;
			break;
		case 3:
			display_object_menu (so, item, event);
			break;

		default:
			/* Ignore mouse wheel events */
			return FALSE;
		}
		break;
	}

	case GDK_BUTTON_RELEASE:
		if (!so->dragging)
			return FALSE;

		so->dragging = FALSE;

		gnome_canvas_item_ungrab (item, event->button.time);

		sheet_object_unrealize (so);
		so->bbox_points->coords [0] += event_total_x;
		so->bbox_points->coords [1] += event_total_y;
		so->bbox_points->coords [2] += event_total_x;
		so->bbox_points->coords [3] += event_total_y;
		sheet_object_realize (so);

		sheet_mode_edit_object (so);
		break;

	case GDK_MOTION_NOTIFY:
	{
		int        dx, dy;

		if (!so->dragging)
			return FALSE;

		dx = event->button.x - event_last_x;
		dy = event->button.y - event_last_y;
		event_total_x += dx;
		event_total_y += dy;
		event_last_x = event->button.x;
		event_last_y = event->button.y;
		gnome_canvas_item_move (item, dx, dy);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}
