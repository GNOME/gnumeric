/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "sheet-object.h"
#include "cursors.h"

/* Pulls the GnumericSheet from a SheetView */
#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS (GTK_OBJECT(so)->klass)

static GtkObjectClass *sheet_object_parent_class;

static void sheet_finish_object_creation (Sheet *sheet, SheetObject *object);
static void sheet_object_start_editing   (SheetObject *object);
static void sheet_object_stop_editing    (SheetObject *object);
static int  object_event                 (GnomeCanvasItem *item,
					  GdkEvent *event,
					  SheetObject *object);

typedef struct {
	gdouble x, y;
} ObjectCoords;

static void
window_to_world (GnomeCanvas *canvas, gdouble *x, gdouble *y)
{
#define DISPLAY_X1(canvas) (GNOME_CANVAS (canvas)->layout.xoffset)
#define DISPLAY_Y1(canvas) (GNOME_CANVAS (canvas)->layout.yoffset)

	*x = canvas->scroll_x1 +
		(*x + DISPLAY_X1 (canvas) - canvas->zoom_xofs) /
		canvas->pixels_per_unit;
	*y = canvas->scroll_y1 +
		(*y + DISPLAY_Y1 (canvas) - canvas->zoom_yofs) /
		canvas->pixels_per_unit;
}

static void
sheet_release_coords (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->coords; l; l = l->next)
		g_free (l->data);

	g_list_free (sheet->coords);
	sheet->coords = NULL;
}

static void
sheet_object_destroy (GtkObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);
	Sheet *sheet;
	GList *l;

	sheet = so->sheet;
	sheet_object_stop_editing (so);
	if (so == sheet->current_object)
		sheet->current_object = NULL;

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gtk_object_destroy (GTK_OBJECT (item));
	}
	g_list_free (so->realized_list);
	sheet->objects = g_list_remove (sheet->objects, so);
	sheet->modified = TRUE;
	gnome_canvas_points_free (so->points);

	(*sheet_object_parent_class->destroy)(object);
}

static void
sheet_object_class_init (GtkObjectClass *object_class)
{
	sheet_object_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = sheet_object_destroy;
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
sheet_object_construct (SheetObject *sheet_object, Sheet *sheet)
{
	g_return_if_fail (sheet_object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (sheet_object));
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_object->sheet = sheet;
	
}

/*
 * sheet_view_object_realize
 *
 * Creates the actual object on the Canvas of a SheetView
 */
static GnomeCanvasItem *
sheet_view_object_realize (SheetView *sheet_view, SheetObject *object)
{
	GnomeCanvasItem *item;
	
	g_return_val_if_fail (sheet_view != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sheet_view), NULL);
	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT (object), NULL);

	item = NULL;

	item = SO_CLASS (object)->realize (object, sheet_view);

	if (item == NULL){
		g_warning ("We created an unsupported type\n");
		return NULL;
	}
	
	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (object_event), object);
			    
	object->realized_list = g_list_prepend (object->realized_list, item);
	return item;
}

/*
 * sheet_view_object_unrealize
 *
 * Removes the object from the canvas in the SheetView.
 */
static void
sheet_view_object_unrealize (SheetView *sheet_view, SheetObject *object)
{
	GList *l;
	
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));

	for (l = object->realized_list; l; l = l->next){
		GnomeCanvasItem *item = GNOME_CANVAS_ITEM (l->data);
		GnumericSheet *gsheet = GNUMERIC_SHEET (item->canvas);

		if (gsheet->sheet_view != sheet_view)
			continue;

		gtk_object_destroy (GTK_OBJECT (item));
		object->realized_list = g_list_remove (object->realized_list, l->data);
		break;
	}
}

/*
 * sheet_object_realize
 *
 * Realizes the on a Sheet (this in turn realizes the object
 * on every existing SheetView)
 */
void
sheet_object_realize (Sheet *sheet, SheetObject *object)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnomeCanvasItem *item;
		
		item = sheet_view_object_realize (sheet_view, object);
		sheet_view->temp_item = object; 
	}
}

/*
 * sheet_object_unrealize
 *
 * Destroys the Canvas Item that represents this SheetObject from
 * every SheetViews.
 */
void
sheet_object_unrealize (Sheet *sheet, SheetObject *object)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		
		sheet_view_object_unrealize (sheet_view, object);
		sheet_view->temp_item = NULL; 
	}
}

/*
 * create_object
 *
 * Creates an object with the data stored from the creation or
 * previous mouse samples to the location to_x, to_y.
 */
static SheetObject *
create_object (Sheet *sheet, gdouble to_x, gdouble to_y)
{
	SheetObject *o = NULL;
	ObjectCoords *oc;

	oc = sheet->coords->data;
	
	switch (sheet->mode){
	case SHEET_MODE_CREATE_LINE:
		o = sheet_object_create_line (
			sheet, FALSE,
			oc->x, oc->y,
			to_x, to_y,
			"black", 1);
		break;

	case SHEET_MODE_CREATE_ARROW:
		o = sheet_object_create_line (
			sheet, TRUE,
			oc->x, oc->y,
			to_x, to_y,
			"black", 1);
		break;
		
	case SHEET_MODE_CREATE_BOX: {
		double x1, x2, y1, y2;

		x1 = MIN (oc->x, to_x);
		x2 = MAX (oc->x, to_x);
		y1 = MIN (oc->y, to_y);
		y2 = MAX (oc->y, to_y);
		
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_RECTANGLE,
			x1, y1, x2, y2,
			NULL, "black", 1);
		break;
	}

	case SHEET_MODE_CREATE_OVAL: {
		double x1, x2, y1, y2;

		x1 = MIN (oc->x, to_x);
		x2 = MAX (oc->x, to_x);
		y1 = MIN (oc->y, to_y);
		y2 = MAX (oc->y, to_y);
		
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_ELLIPSE,
			x1, y1, x2, y2,
			NULL, "black", 1);
		break;
	}
	
	case SHEET_MODE_SHEET:
	case SHEET_MODE_OBJECT_SELECTED:
		g_assert_not_reached ();
	}

	sheet_object_realize (sheet, o);

	return o;
}

/*
 * sheet_motion_notify
 *
 * Invoked when the sheet is in a SHEET_MODE_CREATE mode to keep track
 * of the cursor position.
 */
static int
sheet_motion_notify (GnumericSheet *gsheet, GdkEvent *event, Sheet *sheet)
{
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "motion_notify_event");

	window_to_world (GNOME_CANVAS (gsheet), &event->button.x, &event->button.y);

	SO_CLASS(sheet->current_object)->update (
		sheet->current_object,
		event->button.x, event->button.y);
	
	return 1;
}

/*
 * sheet_button_release
 *
 * Invoked as the last step in object creation.
 */
static int
sheet_button_release (GnumericSheet *gsheet, GdkEventButton *event, Sheet *sheet)
{
	SheetObject *o = sheet->current_object;
	
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_release_event");

	window_to_world (GNOME_CANVAS (gsheet), &event->x, &event->y);
	SO_CLASS (sheet->current_object)->update (o, event->x, event->y);

	sheet_finish_object_creation (sheet, o);
	sheet_object_start_editing (o);
	
	return 1;
}

/*
 * sheet_button_press
 *
 * Starts the process of creating a SheetObject.  Handles the initial
 * button press on the GnumericSheet.
 */
static int
sheet_button_press (GnumericSheet *gsheet, GdkEventButton *event, Sheet *sheet)
{
	ObjectCoords *oc;

	g_assert (sheet->current_object == NULL);
	
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_press_event");

	oc = g_new (ObjectCoords, 1);

	oc->x = event->x;
	oc->y = event->y;
	window_to_world (GNOME_CANVAS (gsheet), &oc->x, &oc->y);
	
	sheet->coords = g_list_append (sheet->coords, oc);

	sheet->current_object = create_object (sheet, oc->x, oc->y);
	
	gtk_signal_connect (GTK_OBJECT (gsheet), "button_release_event",
			    GTK_SIGNAL_FUNC (sheet_button_release), sheet);
	gtk_signal_connect (GTK_OBJECT (gsheet), "motion_notify_event",
			    GTK_SIGNAL_FUNC (sheet_motion_notify), sheet);
	
	return 1;
}

/*
 * sheet_finish_object_creation
 *
 * Last step of the creation of an object: sets the sheet mode to
 * select the current object, releases the datastructures used
 * during object creation and disconnects the signal handlers
 * used during object creation
 */
static void
sheet_finish_object_creation (Sheet *sheet, SheetObject *o)
{
	GList *l;

	/* Set the mode */
	sheet_set_mode_type (sheet, SHEET_MODE_OBJECT_SELECTED);

	sheet_release_coords (sheet);

	sheet->modified = TRUE;
	
	/* Disconnect the signal handlers for object creation */
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

		sheet_view->temp_item = NULL;
		gtk_signal_disconnect_by_func (
			GTK_OBJECT (gsheet),
			GTK_SIGNAL_FUNC (sheet_button_press), sheet);
		gtk_signal_disconnect_by_func (
			GTK_OBJECT (gsheet),
			GTK_SIGNAL_FUNC (sheet_button_release), sheet);
		gtk_signal_disconnect_by_func (
			GTK_OBJECT (gsheet),
			GTK_SIGNAL_FUNC (sheet_motion_notify), sheet);

	}
}

/*
 * sheet_set_mode_type:
 * @sheet:  The sheet
 * @mode:   The new mode of operation
 *
 * These are the following major mode types:
 *   Object creation (SHEET_MODE_CREATE_*)
 *                   These are used during object creation in the sheeet
 *
 *   Sheet mode      (SHEET_MODE_SHEET)
 *                   Regular spreadsheet operations are in place, sheet
 *                   cursor is displayed.
 *
 *   Object editing  (SHEET_MODE_OBJECT_SELECTED)
 *                   No spreadsheet cursor is active, and edition is directed
 *                   towards the currently selected object
 */
void
sheet_set_mode_type (Sheet *sheet, SheetModeType mode)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->mode == mode)
		return;
	
	sheet->mode = mode;

	if (mode == SHEET_MODE_OBJECT_SELECTED){
		sheet_hide_cursor (sheet);
		return;
	}

	sheet_show_cursor (sheet);
	if (sheet->current_object){
		sheet_object_stop_editing (sheet->current_object);
		sheet->current_object = NULL;
	}

	if (mode == SHEET_MODE_SHEET)
		return;

	switch (sheet->mode){
	case SHEET_MODE_CREATE_LINE:
	case SHEET_MODE_CREATE_ARROW:
	case SHEET_MODE_CREATE_OVAL:
	case SHEET_MODE_CREATE_BOX:		
		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
			
			sheet_view->temp_item = NULL;
			gtk_signal_connect (GTK_OBJECT (gsheet), "button_press_event",
					    GTK_SIGNAL_FUNC (sheet_button_press), sheet);
		}
		break;
		
	default:
		g_assert_not_reached ();
	}
}

/*
 * sheet_object_destroy_control_points
 *
 * Destroys the Canvas Items used as sheet control points
 */
static void
sheet_object_destroy_control_points (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		int i;
		
		for (i = 0; i < 8; i++){
			gtk_object_destroy (GTK_OBJECT (sheet_view->control_points [i]));
			sheet_view->control_points [i] = NULL;
		}
	}
}

static void
sheet_object_stop_editing (SheetObject *object)
{
	Sheet *sheet = object->sheet;

	if (object == sheet->current_object)
		sheet_object_destroy_control_points (sheet);
}

#define POINT(x) (1 << x)

/*
 * set_item_x:
 *
 * chantes the x position of the idxth control point
 */
static void
set_item_x (SheetView *sheet_view, int idx, double x)
{
	gnome_canvas_item_set (
		sheet_view->control_points [idx],
		"x1", x - 2,
		"x2", x + 2,
		NULL);
}

/*
 * set_item_x:
 *
 * chantes the y position of the idxth control point
 */
static void
set_item_y (SheetView *sheet_view, int idx, double y)
{
	gnome_canvas_item_set (
		sheet_view->control_points [idx],
		"y1", y - 2,
		"y2", y + 2,
		NULL);
}

/*
 * control_point_handle_event
 *
 * Event handler for the control points.
 *
 * The index for this control point is retrieved from the Gtk user object_data
 *
 */
static int
control_point_handle_event (GnomeCanvasItem *item, GdkEvent *event, SheetObject *object)
{
	int idx;
	GList *l;
	static int last_x, last_y;
	double dx, dy;
	
	switch (event->type){
	case GDK_ENTER_NOTIFY:
		cursor_set_widget (item->canvas, GNUMERIC_CURSOR_ARROW);
		break;
		
	case GDK_BUTTON_RELEASE:
		if (!object->dragging)
			return FALSE;
		
		object->dragging = 0;
		gnome_canvas_item_ungrab (item, event->button.time);
		break;

	case GDK_BUTTON_PRESS:
		object->dragging = 1;
		gnome_canvas_item_grab (item,
					GDK_POINTER_MOTION_MASK |
					GDK_BUTTON_RELEASE_MASK,
					NULL, event->button.time);
		last_x = event->button.x;
		last_y = event->button.y;
		break;

	case GDK_MOTION_NOTIFY: {
		double *coords = object->points->coords;
		int change = 0;
		
		if (!object->dragging)
			return FALSE;

		idx = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (item)));
		gnome_canvas_c2w (item->canvas,
				  event->button.x - last_x,
				  event->button.y - last_y,
				  &dx, &dy);
		
		last_x = event->button.x;
		last_y = event->button.y;

		switch (idx){
		case 0:
			change = POINT (0) | POINT (1);
			break;
			
		case 1:
			change = POINT (1);
			break;
			
		case 2:
			change = POINT (1) | POINT (2);
			break;
				
		case 3:
			change = POINT (0);
			break;
			
		case 4:
			change = POINT (2);
			break;
			
		case 5:
			change = POINT (0) | POINT (3);
			break;
			
		case 6:
			change = POINT (3);
			break;
			
		case 7:
			change = POINT (2) | POINT (3);
			break;
			
		default:
			g_warning ("Should not happen");
		}
		
		for (l = object->sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GnomeCanvasItem *object_item = NULL;
			GList *ll;

			/* Find the object in this sheet view */
			for (ll = object->realized_list; ll; ll = ll->next){
				GnomeCanvasItem *oi = ll->data;
				
				if (oi->canvas == GNOME_CANVAS (sheet_view->sheet_view)){
					object_item = oi;
					break;
				}
			}
			if (change & POINT (0)){
				set_item_x (sheet_view, 0, coords [0] + dx);
				set_item_x (sheet_view, 3, coords [0] + dx);
				set_item_x (sheet_view, 5, coords [0] + dx);
			} else if (change & POINT (2)){
				set_item_x (sheet_view, 2, coords [2] + dx);
				set_item_x (sheet_view, 4, coords [2] + dx);
				set_item_x (sheet_view, 7, coords [2] + dx);
			}

			if (change & POINT (1)){
				set_item_y (sheet_view, 0, coords [1] + dy);
				set_item_y (sheet_view, 1, coords [1] + dy);
				set_item_y (sheet_view, 2, coords [1] + dy);
			} else if (change & POINT (3)){
				set_item_y (sheet_view, 5, coords [3] + dy);
				set_item_y (sheet_view, 6, coords [3] + dy);
				set_item_y (sheet_view, 7, coords [3] + dy);
			}

			if (change & (POINT (0) | POINT (2))){
				set_item_x (sheet_view, 1, (coords [0] + dx + coords [2])/2);
				set_item_x (sheet_view, 6, (coords [0] + dx + coords [2])/2);
			}

			if (change & (POINT (1) | POINT (3))){
				set_item_y (sheet_view, 3, (coords [1] + dy + coords [3])/2);
				set_item_y (sheet_view, 4, (coords [1] + dy + coords [3])/2);
			}

			sheet_view_object_unrealize (sheet_view, object);
			coords [0] += change & POINT (0) ? dx : 0;
			coords [1] += change & POINT (1) ? dy : 0;
			coords [2] += change & POINT (2) ? dx : 0;
			coords [3] += change & POINT (3) ? dy : 0;
			sheet_view_object_realize (sheet_view, object);
		}
		break;
	}
	
	default:
		return FALSE;
	}		
	return TRUE;
}

/*
 * object_event
 *
 * Event handler for a SheetObject
 */
static int
object_event (GnomeCanvasItem *item, GdkEvent *event, SheetObject *object)
{
	static int last_x, last_y;
	static int total_x, total_y;
	int dx, dy;
	
	switch (event->type){
	case GDK_ENTER_NOTIFY:
		cursor_set_widget (item->canvas, GNUMERIC_CURSOR_ARROW);
		break;
		
	case GDK_BUTTON_PRESS:
		if (object->sheet->current_object){
			sheet_object_stop_editing (object->sheet->current_object);
			object->sheet->current_object = NULL;
		}
		
		object->dragging = 1;
		gnome_canvas_item_grab (item,
					GDK_POINTER_MOTION_MASK |
					GDK_BUTTON_RELEASE_MASK,
					NULL, event->button.time);
		last_x = event->button.x;
		last_y = event->button.y;
		total_x = 0;
		total_y = 0;
		break;

	case GDK_BUTTON_RELEASE:
		if (!object->dragging)
			return FALSE;
		
		object->dragging = 0;
		
		gnome_canvas_item_ungrab (item, event->button.time);

		sheet_object_unrealize (object->sheet, object);
		object->points->coords [0] += total_x;
		object->points->coords [1] += total_y;
		object->points->coords [2] += total_x;
		object->points->coords [3] += total_y;
		sheet_object_realize (object->sheet, object);
		
		sheet_object_make_current (object->sheet, object);
		break;

	case GDK_MOTION_NOTIFY:
		if (!object->dragging)
			return FALSE;
		
		dx = event->button.x - last_x;
		dy = event->button.y - last_y;
		total_x += dx;
		total_y += dy;
		last_x = event->button.x;
		last_y = event->button.y;
		gnome_canvas_item_move (item, dx, dy);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/*
 * new_control_point
 * @group:  The canvas group to which this control point belongs
 * @object: The object
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
new_control_point (GnomeCanvasGroup *group, SheetObject *object, int idx, double x, double y)
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
			    object);
	
	gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (idx));
	
	return item;
}

/*
 * sheet_object_start_editing
 *
 * Makes an object editable (adds its control points).
 */
static void
sheet_object_start_editing (SheetObject *object)
{
	Sheet *sheet = object->sheet;
	double *points = object->points->coords;
	GList *l;
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnomeCanvasGroup *group = sheet_view->object_group;
#define ul(x) sheet_view->control_points [x]
		
		ul (0) = new_control_point (group, object, 0, points [0], points [1]);
		ul (1) = new_control_point (group, object, 1, (points [0] + points [2]) / 2, points [1]);
		ul (2) = new_control_point (group, object, 2, points [2], points [1]);
		ul (3) = new_control_point (group, object, 3, points [0], (points [1] + points [3]) / 2);
		ul (4) = new_control_point (group, object, 4, points [2], (points [1] + points [3]) / 2);
		ul (5) = new_control_point (group, object, 5, points [0], points [3]);
		ul (6) = new_control_point (group, object, 6, (points [0] + points [2]) / 2, points [3]);
		ul (7) = new_control_point (group, object, 7, points [2], points [3]);
	}
}

/*
 * sheet_object_make_current
 *
 * Makes the object the currently selected object and prepares it for
 * user edition.
 */
void
sheet_object_make_current (Sheet *sheet, SheetObject *object)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));

	if (sheet->current_object == object)
		return;

	sheet_set_mode_type (sheet, SHEET_MODE_OBJECT_SELECTED);
	
	if (sheet->current_object)
		sheet_object_stop_editing (sheet->current_object);

	sheet_object_start_editing (object);

	sheet->current_object = object;
}
