/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks   (mmeeks@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "sheet-object.h"
#include "cursors.h"

#ifdef ENABLE_BONOBO
#    include <bonobo.h>
#    include "sheet-object-container.h"
#    include "sheet-object-item.h"
#endif
#include "sheet-object-widget.h"

/* Pulls the GnumericSheet from a SheetView */
#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS (GTK_OBJECT(so)->klass)

static GtkObjectClass *sheet_object_parent_class;

static void sheet_finish_object_creation (Sheet *sheet, SheetObject *so);
static void sheet_object_start_editing   (SheetObject *so);
static void sheet_object_stop_editing    (SheetObject *so);
static void sheet_object_start_popup     (SheetObject *so, GtkMenu *menu);
static void sheet_object_end_popup       (SheetObject *so, GtkMenu *menu);

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

static void
sheet_object_destroy (GtkObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);
	Sheet *sheet;

	sheet = so->sheet;
	sheet_object_stop_editing (so);
	if (so == sheet->current_object)
		sheet->current_object = NULL;

	while (so->realized_list) {
		GnomeCanvasItem *item = so->realized_list->data;
		gtk_object_destroy (GTK_OBJECT (item));
	}

	sheet->objects  = g_list_remove (sheet->objects, so);
	sheet->modified = TRUE;
	gnome_canvas_points_free (so->bbox_points);

	(*sheet_object_parent_class->destroy)(object);
}

/**
 * sheet_object_update_bounds:
 * @so: The sheet object
 * 
 * This is a default implementation for lightweight objects.
 * The process or re-realizing the object will use the new
 * set of co-ordinates.
 * 
 **/
static void
sheet_object_update_bounds (SheetObject *so)
{
	sheet_object_unrealize (so);
	sheet_object_realize   (so);
}

static void
sheet_object_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	
	sheet_object_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = sheet_object_destroy;
	sheet_object_class->update_bounds = sheet_object_update_bounds;
	sheet_object_class->start_popup   = sheet_object_start_popup;
	sheet_object_class->end_popup     = sheet_object_end_popup;
	sheet_object_class->print         = NULL;
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
	so->bbox_points = gnome_canvas_points_new (2);

	sheet->objects  = g_list_prepend (sheet->objects, so);

	sheet->modified = TRUE;
}

void
sheet_object_drop_file (Sheet *sheet, gdouble x, gdouble y, const char *fname)
{
#ifdef ENABLE_BONOBO
	const char *mime_type;
	const char *mime_goad_id;
	char *msg = NULL;
	
	g_return_if_fail (sheet != NULL);

	if (!(mime_type = gnome_mime_type (fname))) {
		msg = g_strdup_printf ("unknown mime type for '%s'", (char *)fname);
		gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
	} else if (!(mime_goad_id = gnome_mime_get_value (mime_type, "bonobo-goad-id"))) {
		msg = g_strdup_printf ("no mime mapping for '%s'", mime_type);
		gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
	} else {
		SheetObject   *so;
		ObjectCoords   pos;

		so = SHEET_OBJECT (sheet_object_container_new_from_goadid (
			sheet, pos.x, pos.y,
			pos.x + 100.0, pos.y + 100.0,
			mime_goad_id));
		if (!so) {
			msg = g_strdup_printf ("can't create object for '%s'", mime_goad_id);
			gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
		} else {
			sheet_object_bonobo_load_from_file (SHEET_OBJECT_BONOBO (so), fname);
		}
	}
	if (msg)
		g_free (msg);
#endif
}

/**
 * sheet_object_get_bounds:
 * @so: The sheet object we are interested in
 * @tlx: set to the top left x position
 * @tly: set to the top left y position
 * @brx: set to the bottom right x position
 * @bry: set to the bottom right y position
 * 
 *   This function returns the bounds of the sheet object
 * in the pointers it is passed.
 * 
 **/
void
sheet_object_get_bounds (SheetObject *so, double *tlx, double *tly,
			 double *brx, double *bry)
{
	g_return_if_fail (so != NULL);
	g_return_if_fail (tlx != NULL);
	g_return_if_fail (tly != NULL);
	g_return_if_fail (brx != NULL);
	g_return_if_fail (bry != NULL);

	*tlx = MIN (so->bbox_points->coords [0],
		    so->bbox_points->coords [2]);
	*brx = MAX (so->bbox_points->coords [0],
		    so->bbox_points->coords [2]);
	*tly = MIN (so->bbox_points->coords [1],
		    so->bbox_points->coords [3]);
	*bry = MAX (so->bbox_points->coords [1],
		    so->bbox_points->coords [3]);
}

/**
 * sheet_object_get_bounds:
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
 * sheet_view_object_realize
 *
 * Creates the actual object on the Canvas of a SheetView
 */
static GnomeCanvasItem *
sheet_view_object_realize (SheetView *sheet_view, SheetObject *so)
{
	GnomeCanvasItem *item;
	
	g_return_val_if_fail (sheet_view != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sheet_view), NULL);
	g_return_val_if_fail (so != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	item = SO_CLASS (so)->realize (so, sheet_view);

	if (item == NULL) {
		g_warning ("We created an unsupported type\n");
		return NULL;
	}
	
	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (sheet_object_canvas_event), so);
	gtk_signal_connect (GTK_OBJECT (item), "destroy",
			    GTK_SIGNAL_FUNC (sheet_object_item_destroyed), so);
	so->realized_list = g_list_prepend (so->realized_list, item);
	return item;
}

/*
 * sheet_view_object_unrealize
 *
 * Removes the object from the canvas in the SheetView.
 */
static void
sheet_view_object_unrealize (SheetView *sheet_view, SheetObject *so)
{
	GList *l;
	
	g_return_if_fail (sheet_view != NULL);
	g_return_if_fail (IS_SHEET_VIEW (sheet_view));
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
 * on every existing SheetView)
 */
void
sheet_object_realize (SheetObject *so)
{
	GList *l;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));
	
	for (l = so->sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;
		GnomeCanvasItem *item;
		
		item = sheet_view_object_realize (sheet_view, so);
		sheet_view->temp_item = so; 
	}
}

/*
 * sheet_object_unrealize
 *
 * Destroys the Canvas Item that represents this SheetObject from
 * every SheetViews.
 */
void
sheet_object_unrealize (SheetObject *so)
{
	GList *l;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	for (l = so->sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;
		
		sheet_view_object_unrealize (sheet_view, so);
		sheet_view->temp_item = NULL; 
	}
}

/*
 * Only for demostration purposes
 */
static GtkWidget *
button_widget_create (SheetObjectWidget *sow, SheetView *sheet_view)
{
	GtkWidget *button;

	button = gtk_button_new_with_label (_("Button"));
	gtk_widget_show (button);
	
	return button;
}

/*
 * Only for demostration purposes
 */
static GtkWidget *
checkbox_widget_create (SheetObjectWidget *sow, SheetView *sheet_view)
{
	GtkWidget *checkbox;

	checkbox = gtk_check_button_new();
	gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (checkbox), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), FALSE);
	gtk_widget_show (checkbox);
	
	return checkbox;
}

SheetObject *
sheet_object_create_button  (Sheet *sheet,
			     double x1, double y1,
			     double x2, double y2)
{
	SheetObject * obj =
	    sheet_object_widget_new (sheet, x1, y1, x2, y2,
				     button_widget_create, NULL);
	sheet_object_realize (obj);
	return obj;
}

SheetObject *
sheet_object_create_checkbox (Sheet *sheet,
			      double x1, double y1,
			      double x2, double y2)
{
	SheetObject * obj =
	    sheet_object_widget_new (sheet, x1, y1, x2, y2,
				     checkbox_widget_create, NULL);
	sheet_object_realize (obj);
	return obj;
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
	double x1, x2, y1, y2;

	oc = sheet->coords->data;

	x1 = MIN (oc->x, to_x);
	x2 = MAX (oc->x, to_x);
	y1 = MIN (oc->y, to_y);
	y2 = MAX (oc->y, to_y);
	
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
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_BOX,
			x1, y1, x2, y2,
			NULL, "black", 1);
		break;
	}

	case SHEET_MODE_CREATE_OVAL: {
		o = sheet_object_create_filled (
			sheet, SHEET_OBJECT_OVAL,
			x1, y1, x2, y2,
			NULL, "black", 1);
		break;
	}


	case SHEET_MODE_CREATE_CANVAS_ITEM:
#ifdef ENABLE_BONOBO
		o = sheet_object_item_new (
			sheet, x1, y1, x2, y2, sheet->mode_data);
		g_free (sheet->mode_data);
		sheet->mode_data = NULL;
		sheet_object_set_bounds (o, x1, y1, x2, y2);
#endif
		break;
		
	case SHEET_MODE_CREATE_BUTTON:
		o = sheet_object_create_button (sheet, x1, y1, x2, y2);
		break;
		
	case SHEET_MODE_CREATE_CHECKBOX:
		o = sheet_object_create_checkbox (sheet, x1, y1, x2, y2);
		break;

	case SHEET_MODE_SHEET:
	case SHEET_MODE_OBJECT_SELECTED:
		g_assert_not_reached ();

	case SHEET_MODE_CREATE_GRAPHIC:
#ifdef ENABLE_BONOBO
		/* Bug 9984 : do not start objects with size 0,0 */
		if (x1 == x2)
			x2 += 50.;
		if (y1 == y2)
			y2 += 50.;
		g_warning ("Ugly API name follows, fix it");
		o = sheet_object_container_new_bonobo (
			sheet, x1, y1, x2, y2, sheet->mode_data);

		g_warning ("Possible leak");
		/*
		 * Ie, who "owns" mode_data when it is a BonoboObjectClient?
		 */
		sheet->mode_data = NULL;
		break;
#endif
			
	case SHEET_MODE_CREATE_COMPONENT:
#ifdef ENABLE_BONOBO
		/* Bug 9984 : do not start objects with size 0,0 */
		if (x1 == x2)
			x2 += 50.;
		if (y1 == y2)
			y2 += 50.;
		o = sheet_object_container_new_from_goadid (
			sheet, x1, y1, x2, y1, sheet->mode_data);
		g_free (sheet->mode_data);
		sheet->mode_data = NULL;
		break;
#endif
	}

	sheet_object_realize (o);

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
	double  brx, bry;
	ObjectCoords *tl;
	SheetObject  *so;

	g_return_val_if_fail (sheet != NULL, 1);
	g_return_val_if_fail (gsheet != NULL, 1);
	g_return_val_if_fail (sheet->coords != NULL, 1);
	g_return_val_if_fail (sheet->coords->data != NULL, 1);
	g_return_val_if_fail (sheet->current_object != NULL, 1);

	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "motion_notify_event");

	so = SHEET_OBJECT (sheet->current_object);
	gnome_canvas_window_to_world (GNOME_CANVAS (gsheet),
				      event->button.x, event->button.y,
				      &brx, &bry);
	
	tl = (ObjectCoords *)sheet->coords->data;

	sheet->current_object = so;
	sheet_object_set_bounds (so, tl->x, tl->y, brx, bry);
	

	SO_CLASS(so)->update_bounds (so);
	
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
	double  brx, bry;
	ObjectCoords *tl;
	SheetObject *so;

	g_return_val_if_fail (sheet != NULL, 1);
	g_return_val_if_fail (gsheet != NULL, 1);
	g_return_val_if_fail (sheet->coords != NULL, 1);
	g_return_val_if_fail (sheet->coords->data != NULL, 1);
	g_return_val_if_fail (sheet->current_object != NULL, 1);

	so = sheet->current_object;
	
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_release_event");

	gnome_canvas_window_to_world (GNOME_CANVAS (gsheet), event->x, event->y,&brx, &bry);
	
	tl = (ObjectCoords *)sheet->coords->data;

	sheet_object_set_bounds (so, tl->x, tl->y, brx, bry);

	SO_CLASS (sheet->current_object)->update_bounds (so);

	sheet_finish_object_creation (sheet, so);

#ifdef ENABLE_BONOBO
	/*
	 * Bonobo objects might want to load state from somewhere
	 * to be useful
	 */
	if (IS_SHEET_OBJECT_BONOBO (so))
		sheet_object_bonobo_load_from_file (
			SHEET_OBJECT_BONOBO (so), NULL);
#endif
	
	sheet_object_start_editing   (so);
	
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

	if (sheet->current_object) {
		sheet_object_stop_editing (sheet->current_object);
		sheet->current_object = NULL;
	}
	
	/* Do not propagate this event further */
	gtk_signal_emit_stop_by_name (GTK_OBJECT (gsheet), "button_press_event");

	oc = g_new (ObjectCoords, 1);

	gnome_canvas_window_to_world (GNOME_CANVAS (gsheet), event->x, event->y, &oc->x, &oc->y);
	
	sheet->coords = g_list_append (sheet->coords, oc);

	sheet->current_object = create_object (sheet, oc->x, oc->y);

	/*
	 * If something fails during object creation,
	 * set the mode to the normal sheet mode
	 */
	if (!sheet->current_object) {
		sheet_set_mode_type (sheet, SHEET_MODE_SHEET);
		return 1;
	}
	
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
	for (l = sheet->sheet_views; l; l = l->next) {
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

static void
sheet_object_bind_button_events (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->sheet_views; l; l = l->next) {
		
		SheetView *sheet_view = l->data;
		GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
		
		sheet_view->temp_item = NULL;
		gtk_signal_connect (GTK_OBJECT (gsheet), "button_press_event",
				    GTK_SIGNAL_FUNC (sheet_button_press), sheet);
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
sheet_set_mode_type_full (Sheet *sheet, SheetModeType mode, void *mode_data)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->mode == mode)
		return;
	
	sheet->mode = mode;

	if (mode == SHEET_MODE_OBJECT_SELECTED) {
		sheet_hide_cursor (sheet);
		return;
	}

	if (mode == SHEET_MODE_SHEET) {
#ifdef ENABLE_BONOBO
		if (sheet->active_object_frame) {
			bonobo_view_frame_view_deactivate (sheet->active_object_frame);
			if (sheet->active_object_frame != NULL)
				bonobo_view_frame_set_covered (sheet->active_object_frame, TRUE);
			sheet->active_object_frame = NULL;
		}
#endif
		sheet_show_cursor (sheet);
		if (sheet->current_object) {
			sheet_object_stop_editing (sheet->current_object);
			sheet->current_object = NULL;
		}
		return;
	}

	switch (sheet->mode) {
	case SHEET_MODE_CREATE_GRAPHIC:
#ifdef ENABLE_BONOBO
		g_assert (BONOBO_IS_CLIENT_SITE (mode_data));
		sheet_object_bind_button_events (sheet);
		sheet->mode_data = mode_data;
#endif
		break;
		
	case SHEET_MODE_CREATE_COMPONENT:
	case SHEET_MODE_CREATE_CANVAS_ITEM:
#ifdef ENABLE_BONOBO
	{
		char *required_interfaces [2];
		char *obj_id;

		if (sheet->mode == SHEET_MODE_CREATE_CANVAS_ITEM)
			required_interfaces [0] = "IDL:Bonobo/Canvas/Item:1.0";
		else
			required_interfaces [0] = "IDL:Bonobo/Embeddable:1.0";
		required_interfaces [1] = NULL;

#if USING_OAF
		obj_id = gnome_bonobo_select_oaf_id (
			_("Select an object to add"), required_interfaces);
#else
		obj_id = gnome_bonobo_select_goad_id (
			_("Select an object to add"), required_interfaces);
#endif
		if (obj_id == NULL) {
			sheet_set_mode_type (sheet, SHEET_MODE_SHEET);
			return;
		}
		sheet->mode_data = g_strdup (obj_id);

		sheet_object_bind_button_events (sheet);
		break;
	}
#endif

	case SHEET_MODE_CREATE_LINE:
	case SHEET_MODE_CREATE_ARROW:
	case SHEET_MODE_CREATE_OVAL:
	case SHEET_MODE_CREATE_BOX:
	case SHEET_MODE_CREATE_BUTTON:
	case SHEET_MODE_CREATE_CHECKBOX:
		sheet_object_bind_button_events (sheet);
		break;
		
	default:
		g_assert_not_reached ();
	}
}

void
sheet_set_mode_type (Sheet *sheet, SheetModeType mode)
{
	sheet_set_mode_type_full (sheet, mode, NULL);
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

	for (l = sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;
		int i;
		
		for (i = 0; i < 8; i++) {
			gtk_object_destroy (GTK_OBJECT (sheet_view->control_points [i]));
			sheet_view->control_points [i] = NULL;
		}
	}
}

static void
sheet_object_stop_editing (SheetObject *so)
{
	Sheet *sheet = so->sheet;

	if (so == sheet->current_object)
		sheet_object_destroy_control_points (sheet);
}

/*
 * set_item_x:
 *
 * changes the x position of the idxth control point
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
 * set_item_y:
 *
 * changes the y position of the idxth control point
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

/**
 * update_bbox:
 * @sheet_object: The selected object.
 * 
 *  This updates all the views this object appears in. It
 * re-aligns the control points so that they appear at the
 * correct verticies.
 * 
 **/
static void
update_bbox (SheetObject *so)
{
	GList *l;
	const double *c = so->bbox_points->coords;

	for (l = so->sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;

		set_item_x (sheet_view, 0, c[0]);
		set_item_x (sheet_view, 3, c[0]);
		set_item_x (sheet_view, 5, c[0]);
		set_item_x (sheet_view, 2, c[2]);
		set_item_x (sheet_view, 4, c[2]);
		set_item_x (sheet_view, 7, c[2]);

		set_item_x (sheet_view, 1, (c[0] + c[2]) / 2.0);
		set_item_x (sheet_view, 6, (c[0] + c[2]) / 2.0);

		set_item_y (sheet_view, 0, c[1]);
		set_item_y (sheet_view, 1, c[1]);
		set_item_y (sheet_view, 2, c[1]);
		set_item_y (sheet_view, 5, c[3]);
		set_item_y (sheet_view, 6, c[3]);
		set_item_y (sheet_view, 7, c[3]);

		set_item_y (sheet_view, 3, (c[1] + c[3]) / 2.0);
		set_item_y (sheet_view, 4, (c[1] + c[3]) / 2.0);
	}
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
control_point_handle_event (GnomeCanvasItem *item, GdkEvent *event, SheetObject *so)
{
	int idx;
	static gdouble last_x, last_y;
	
	switch (event->type) {
	case GDK_ENTER_NOTIFY:
	{
		gpointer p = gtk_object_get_data (GTK_OBJECT (item), "cursor");
		cursor_set_widget (item->canvas, GPOINTER_TO_UINT (p));
		break;
	}
		
	case GDK_BUTTON_RELEASE:
		if (!so->dragging)
			return FALSE;
		
		so->dragging = FALSE;
		gnome_canvas_item_ungrab (item, event->button.time);
		break;

	case GDK_BUTTON_PRESS:
		so->dragging = TRUE;
		gnome_canvas_item_grab (item,
					GDK_POINTER_MOTION_MASK |
					GDK_BUTTON_RELEASE_MASK,
					NULL, event->button.time);
		last_x = event->button.x;
		last_y = event->button.y;
		sheet_object_make_current (so);
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

static void
sheet_object_remove_cb (GtkWidget *widget, SheetObject *so)
{
	gtk_signal_emit_by_name (GTK_OBJECT (so), "destroy");
}

/**
 * sheet_object_start_popup:
 * @so:  the sheet object
 * @menu: the menu to insert into
 * 
 * Add standard items to the object's popup menu.
 **/
static void
sheet_object_start_popup (SheetObject *so, GtkMenu *menu)
{
	GtkWidget *item = gtk_menu_item_new_with_label (_("Remove"));

	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (sheet_object_remove_cb), so);

	gtk_menu_append (menu, item);
}

/**
 * sheet_object_end_popup:
 * @so: the sheet object
 * @menu: the menu to remove from.
 * 
 * clean standard items from the objects popup menu.
 **/
static void
sheet_object_end_popup (SheetObject *so, GtkMenu *menu)
{
}

static void
menu_unrealize_cb (GtkMenu *menu, SheetObject *so)
{
	SO_CLASS (so)->end_popup (so, menu);
}

static GtkMenu *
create_popup_menu (SheetObject *so)
{
	GtkMenu *menu = GTK_MENU (gtk_menu_new ());

	SO_CLASS (so)->start_popup (so, menu);
	gtk_signal_connect (GTK_OBJECT (menu), "unrealize",
			    GTK_SIGNAL_FUNC (menu_unrealize_cb), so);

	return menu;
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
sheet_object_canvas_event (GnomeCanvasItem *item, GdkEvent *event, SheetObject *so)
{
	static int event_last_x,  event_last_y;
	static int event_total_x, event_total_y;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		if (so->type == SHEET_OBJECT_ACTION_STATIC)
			cursor_set_widget (item->canvas, GNUMERIC_CURSOR_ARROW);
		else
			cursor_set_widget (item->canvas, GNUMERIC_CURSOR_PRESS);
		break;
		
	case GDK_BUTTON_PRESS:
	{
		switch (event->button.button) {
		case 1:
		case 2:
			if (so->sheet->current_object) {
				sheet_object_stop_editing (so->sheet->current_object);
				so->sheet->current_object = NULL;
			}
			
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
		{
			GtkMenu *menu;

			sheet_object_make_current (so);
			menu = create_popup_menu (so);
			gtk_widget_show_all (GTK_WIDGET (menu));
			gnumeric_popup_menu (menu, (GdkEventButton *)event);
			break;
		}
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
		
		sheet_object_make_current (so);
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

/**
 * sheet_object_widget_event:
 * @widget: The widget it happens on
 * @event:  The event.
 * @item:   The canvas item.
 * 
 *  This handles an event on the object stored in the 
 * "sheet_object" data on the canvas item, it passes the
 * event if button 3 is pressed to the standard sheet-object
 * handler otherwise it passes it on.
 * 
 * Return value: event handled ?
 **/
static int
sheet_object_widget_event (GtkWidget *widget, GdkEvent *event,
			   GnomeCanvasItem *item)
{
	SheetObject *so = gtk_object_get_data (GTK_OBJECT (item),
					       "sheet_object");

	g_return_val_if_fail (so != NULL, FALSE);

	switch (event->type) {
		
	case GDK_BUTTON_PRESS:
	{
		switch (event->button.button) {
		case 1:
		case 2:
			return FALSE;
		case 3:
		default:
			return sheet_object_canvas_event (item, event, so);
		}
		break;
	}
	default:
		break;
	}
	return FALSE;
}

void
sheet_object_widget_handle (SheetObject *so, GtkWidget *widget,
			    GnomeCanvasItem *item)
{
	gtk_object_set_data (GTK_OBJECT (item), "sheet_object", so);
	gtk_signal_connect  (GTK_OBJECT (widget), "event",
			     GTK_SIGNAL_FUNC (sheet_object_widget_event),
			     item);
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
		   int idx, double x, double y)
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
	
	return item;
}

/*
 * sheet_object_start_editing
 *
 * Makes an object editable (adds its control points).
 */
static void
sheet_object_start_editing (SheetObject *so)
{
	Sheet *sheet;
	double *box;
	GList *l;
	
	g_return_if_fail (so != NULL);
	g_return_if_fail (so->sheet != NULL);
	g_return_if_fail (so->bbox_points != NULL);

	sheet = so->sheet;
	box   = so->bbox_points->coords;
	for (l = sheet->sheet_views; l; l = l->next) {
		SheetView *sheet_view = l->data;
		GnomeCanvasGroup *group = sheet_view->object_group;
		GnomeCanvasItem **p = sheet_view->control_points;
		
		p [0] = new_control_point (group, so, 0, box [0], box [1]);
		gtk_object_set_data (GTK_OBJECT (p [0]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_TL));
		p [1] = new_control_point (group, so, 1, (box [0] + box [2]) / 2, box [1]);
		gtk_object_set_data (GTK_OBJECT (p [1]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_Y));
		p [2] = new_control_point (group, so, 2, box [2], box [1]);
		gtk_object_set_data (GTK_OBJECT (p [2]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_TR));
		p [3] = new_control_point (group, so, 3, box [0], (box [1] + box [3]) / 2);
		gtk_object_set_data (GTK_OBJECT (p [3]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_X));
		p [4] = new_control_point (group, so, 4, box [2], (box [1] + box [3]) / 2);
		gtk_object_set_data (GTK_OBJECT (p [4]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_X));
		p [5] = new_control_point (group, so, 5, box [0], box [3]);
		gtk_object_set_data (GTK_OBJECT (p [5]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_TR));
		p [6] = new_control_point (group, so, 6, (box [0] + box [2]) / 2, box [3]);
		gtk_object_set_data (GTK_OBJECT (p [6]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_Y));
		p [7] = new_control_point (group, so, 7, box [2], box [3]);
		gtk_object_set_data (GTK_OBJECT (p [7]), "cursor",
				     GINT_TO_POINTER (GNUMERIC_CURSOR_SIZE_TL));
	}
}

/*
 * sheet_object_make_current
 *
 * Makes the object the currently selected object and prepares it for
 * user edition.
 */
void
sheet_object_make_current (SheetObject *so)
{
	Sheet *sheet;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	sheet = so->sheet;
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	if (sheet->current_object == so)
		return;

	sheet_set_mode_type (sheet, SHEET_MODE_OBJECT_SELECTED);
	
	if (sheet->current_object)
		sheet_object_stop_editing (sheet->current_object);

	sheet_object_start_editing (so);

	sheet->current_object = so;
}
