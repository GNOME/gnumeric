#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "sheet-object.h"

#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

static void sheet_finish_object_creation (Sheet *sheet);

typedef struct {
	gdouble x, y;
} ObjectCoords;

static void
sheet_release_coords (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->coords; l; l = l->next)
		g_free (l->data);

	g_list_free (sheet->coords);
	sheet->coords = NULL;
}

static inline SheetObject *
sheet_object_new (Sheet *sheet)
{
	SheetObject *so;

	so = g_new0 (SheetObject, 1);
	so->signature = SHEET_OBJECT_SIGNATURE;
	so->sheet = sheet;

	return so;
}

static inline SheetObject *
sheet_filled_object_new (Sheet *sheet)
{
	SheetFilledObject *sfo;

	sfo = g_new0 (SheetFilledObject, 1);
	sfo->sheet_object.signature = SHEET_OBJECT_SIGNATURE;
	sfo->sheet_object.sheet = sheet;

	return (SheetObject *) sfo;
}

SheetObject *
sheet_object_create_filled (Sheet *sheet, int type,
			    double x1, double y1,
			    double x2, double y2,
			    char *fill_color, char *outline_color, int w)
{
	SheetFilledObject *sfo;
	SheetObject *so;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	so = sheet_filled_object_new (sheet);
	sfo = (SheetFilledObject *) so;
	
	so->type = type;
	so->points = gnome_canvas_points_new (2);
	so->points->coords [0] = x1;
	so->points->coords [1] = y1;
	so->points->coords [2] = x2;
	so->points->coords [3] = y2;
	so->width = w;
	sfo->pattern = 0;

	if (outline_color)
		so->color = string_get (outline_color);

	if (fill_color)
		sfo->fill_color = string_get (fill_color);

	
	return (SheetObject *) sfo;
}

SheetObject *
sheet_object_create_line (Sheet *sheet, double x1, double y1, double x2, double y2, char *color, int w)
{
	SheetObject *so;
		
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	so = sheet_object_new (sheet);
	so->type = SHEET_OBJECT_LINE;
	so->points = gnome_canvas_points_new (2);
	so->points->coords [0] = (gdouble) x1;
	so->points->coords [1] = (gdouble) y1;
	so->points->coords [2] = (gdouble) x2;
	so->points->coords [3] = (gdouble) y2;
	so->color = string_get (color);
	so->width = w;
	
	return so;
}

void
sheet_object_destroy (SheetObject *object)
{
	SheetFilledObject *sfo = (SheetFilledObject *) object;
	GList *l;
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));
	
	switch (object->type){

	case SHEET_OBJECT_RECTANGLE:
	case SHEET_OBJECT_ELLIPSE:
		if (sfo->fill_color)
			string_unref (sfo->fill_color);
		/* fall down */

	case SHEET_OBJECT_ARROW:
	case SHEET_OBJECT_LINE:
		string_unref (object->color);
		gnome_canvas_points_free (object->points);
		break;
	}

	for (l = object->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gtk_object_destroy (GTK_OBJECT (item));
	}
	g_list_free (l);
	
	g_free (object);
}

GnomeCanvasItem *
sheet_view_object_realize (SheetView *sheet_view, SheetObject *object)
{
	SheetFilledObject *filled_object = (SheetFilledObject *) object;
	GnomeCanvasItem *item;
	
	g_return_val_if_fail (sheet_view != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sheet_view), NULL);
	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT (object), NULL);

	item = NULL;
	
	switch (object->type){
	case SHEET_OBJECT_LINE:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        object->points,
			"fill_color",    object->color->str,
			"width_pixels",  object->width,
			NULL);
		break;

	case SHEET_OBJECT_ARROW:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_line_get_type (),
			"points",        object->points,
			"fill_color",    object->color->str,
			"width_pixels",  object->width,
			"arrow_shape_a", 1.0,
			"arrow_shape_b", 1.0,
			"arrow_shape_c", 1.0,
			NULL);
		break;

	case SHEET_OBJECT_RECTANGLE:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_rect_get_type (),
			"x1",            object->points->coords [0],
			"y1",            object->points->coords [1],
			"x2",            object->points->coords [2],
			"y2",            object->points->coords [3],
			"fill_color",    filled_object->fill_color ?
			filled_object->fill_color->str : NULL,
			"outline_color", object->color->str,
			"width_pixels",  object->width,
			NULL);
		break;

	case SHEET_OBJECT_ELLIPSE:
		item = gnome_canvas_item_new (
			sheet_view->object_group,
			gnome_canvas_ellipse_get_type (),
			"x1",            object->points->coords [0],
			"y1",            object->points->coords [1],
			"x2",            object->points->coords [2],
			"y2",            object->points->coords [3],
			"fill_color",    filled_object->fill_color ?
			filled_object->fill_color->str : NULL,
			"outline_color", object->color->str,
			"width_pixels",  object->width,
			NULL);
		break;
	}

	if (item == NULL)
		g_warning ("We created an unsupported type\n");
	
	object->realized_list = g_list_prepend (object->realized_list, item);
	return item;
}

void
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

static void
sheet_object_realize (Sheet *sheet, SheetObject *object)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnomeCanvasItem *item;
		
		item = sheet_view_object_realize (sheet_view, object);
		sheet_view->temp_item = object; 
	}
}

static SheetObject *
create_object (Sheet *sheet, gdouble to_x, gdouble to_y)
{
	SheetObject *o = NULL;
	ObjectCoords *oc;

	oc = sheet->coords->data;
	
	switch (sheet->mode){
	case SHEET_MODE_CREATE_LINE:
		o = sheet_object_create_line (
			sheet,
			oc->x, oc->y,
			to_x, to_y,
			"black", 1);
		break;
		
	case SHEET_MODE_CREATE_BOX:
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_RECTANGLE,
			oc->x, oc->y,
			to_x, to_y,
			NULL, "black", 1);
		break;

	case SHEET_MODE_CREATE_OVAL:
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_ELLIPSE,
			oc->x, oc->y,
			to_x, to_y,
			NULL, "black", 1);
		break;

	case SHEET_MODE_SHEET:
		g_warning ("This sould not happen\n");
	}

	sheet_object_realize (sheet, o);

	return o;
}

static int
sheet_motion_notify (GnumericSheet *gsheet, GdkEvent *event, Sheet *sheet)
{
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "motion_notify_event");

	if (gsheet->sheet_view->temp_item)
		sheet_object_destroy (gsheet->sheet_view->temp_item);

	create_object (sheet, event->motion.x, event->motion.y);
	
	return 1;
}

static int
sheet_button_release (GnumericSheet *gsheet, GdkEventButton *event, Sheet *sheet)
{
	SheetObject *o;
	
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_release_event");

	if (gsheet->sheet_view->temp_item)
		sheet_object_destroy (gsheet->sheet_view->temp_item);
	
	o = create_object (sheet, event->x, event->y);

	sheet_object_make_current (sheet, o);
	
	sheet_finish_object_creation (sheet);

	return 1;
}

static int
sheet_button_press (GnumericSheet *gsheet, GdkEvent *event, Sheet *sheet)
{
	ObjectCoords *oc;

	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_press_event");
	
	oc = g_new (ObjectCoords, 1);
	oc->x = event->button.x;
	oc->y = event->button.y;

	sheet->coords = g_list_append (sheet->coords, oc);

	gtk_signal_connect (GTK_OBJECT (gsheet), "button_release_event",
			    GTK_SIGNAL_FUNC (sheet_button_release), sheet);
	gtk_signal_connect (GTK_OBJECT (gsheet), "motion_notify_event",
			    GTK_SIGNAL_FUNC (sheet_motion_notify), sheet);
	
	return 1;
}

static void
sheet_finish_object_creation (Sheet *sheet)
{
	GList *l;

	/* Reset the mode */
	sheet->mode = SHEET_MODE_SHEET;

	sheet_release_coords (sheet);
	
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
			    
void
sheet_set_mode_type (Sheet *sheet, SheetModeType mode)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet->mode = mode;
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);

		sheet_view->temp_item = NULL;
		gtk_signal_connect (GTK_OBJECT (gsheet), "button_press_event",
				    GTK_SIGNAL_FUNC (sheet_button_press), sheet);
	}
}

static void
sheet_object_stop_editing (SheetObject *object)
{
	Sheet *sheet = object->sheet;
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		GList *items;
		SheetView *sheet_view = l->data;

		for (items = sheet_view->control_points; items; items = items->next){
			GnomeCanvasItem *item = items->data;

			gtk_object_destroy (GTK_OBJECT (item));
		}
		g_list_free (sheet_view->control_points);
		sheet_view->control_points = NULL;
	}
	sheet->current_object = NULL;
}

/*
 * This hooks to the event for the handlebox
 */
static int
object_event (GnomeCanvasItem *item, GdkEvent *event, SheetObject *object)
{
	int idx;
	double *points = object->points->coords;
	double x1, y1, x2, y2;
	static int last_x, last_y, dx, dy;
	
	switch (event->type){
	case GDK_BUTTON_RELEASE:
		object->dragging = 0;
		break;

	case GDK_BUTTON_PRESS:
		object->dragging = 1;
		last_x = event->button.x;
		last_y = event->button.y;
		break;

	case GDK_MOTION_NOTIFY:
		if (!object->dragging)
			return FALSE;

		idx = GPOINTER_TO_INT (gtk_object_get_user_data (item));
		printf ("HERE: %d\n", idx);
		switch (idx){
		case 0:
		case 2:
		case 5:
		case 7:
			/* borders */
			dx = event->button.x - last_x;
			dy = event->button.y - last_y;
			break;
			
		case 1:
		case 6:
			dx = 0;
			dy = event->button.y - last_y;
			break;
			
		case 3:
		case 4:
			dy = 0;
			dx = event->button.x - last_x;
			break;
		}
		last_x = event->button.x;
		last_y = event->button.y;
		
		gnome_canvas_item_move (item, dx, dy);
		
		break;
		
	default:
		return FALSE;
	}		
	return TRUE;
}

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
			    GTK_SIGNAL_FUNC (object_event), object);
	
	gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (idx));
	
	return item;
}

static void
sheet_object_start_editing (SheetObject *object)
{
	Sheet *sheet = object->sheet;
	double *points = object->points->coords;
	GList *l;
	int i;
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		GnomeCanvasGroup *group = sheet_view->object_group;
		GnomeCanvasItem *ul [8];
		
		ul [0] = new_control_point (group, object, 0, points [0], points [1]);
		ul [1] = new_control_point (group, object, 1, (points [0] + points [2]) / 2, points [1]);
		ul [2] = new_control_point (group, object, 2, points [2], points [1]);
		ul [3] = new_control_point (group, object, 3, points [0], (points [1] + points [3]) / 2);
		ul [4] = new_control_point (group, object, 4, points [2], (points [1] + points [3]) / 2);
		ul [5] = new_control_point (group, object, 5, points [0], points [3]);
		ul [6] = new_control_point (group, object, 6, (points [0] + points [2]) / 2, points [3]);
		ul [7] = new_control_point (group, object, 7, points [2], points [3]);

		for (i = 0; i < 8; i++)
			sheet_view->control_points = g_list_prepend (sheet_view->control_points, ul [i]);
	}
}

void
sheet_object_make_draggable (SheetObject *object)
{
}

void
sheet_object_make_current (Sheet *sheet, SheetObject *object)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (object));

	if (sheet->current_object == object)
		return;
	
	if (sheet->current_object)
		sheet_object_stop_editing (sheet->current_object);

	sheet_object_make_draggable (object);
	sheet_object_start_editing (object);

	sheet->current_object = object;
}
