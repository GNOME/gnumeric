/*
 * sheet-object-container.c:
 *   SheetObject for containers (Bonobo, Graphics)
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object-container.h"
#include <bonobo/gnome-container.h>
#include <bonobo/gnome-view-frame.h>
#include <bonobo/gnome-client-site.h>
#include <bonobo/gnome-embeddable.h>

#if 0
#include "PlotComponent.h"
#endif

static SheetObject *sheet_object_container_parent_class;

static void
sheet_object_container_destroy (GtkObject *object)
{
	SheetObjectContainer *soc = SHEET_OBJECT_CONTAINER (object);

	g_free (soc->repoid);

	if (soc->client_site)
		gnome_object_destroy (GNOME_OBJECT (soc->client_site));
	
	/* Call parent's destroy method */
	GTK_OBJECT_CLASS(sheet_object_container_parent_class)->destroy (object);
}

static GnomeCanvasItem *
make_container_item (SheetObject *so, SheetView *sheet_view, GtkWidget *w)
{
	GnomeCanvasItem *item;
	double *c;

	c = so->bbox_points->coords;	
	item = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", w,
		"x",      MIN (c [0], c [2]),
		"y",      MIN (c [1], c [3]),
		"width",  fabs (c [0] - c [2]),
		"height", fabs (c [1] - c [3]),
		"size_pixels", FALSE,
		NULL);

	gtk_widget_show (w);
	return item;
}

static void
sheet_object_container_destroy_views (SheetObject *so)
{
	GList *l;

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gtk_object_unref (GTK_OBJECT (item));
	}
	g_list_free (so->realized_list);
	so->realized_list = NULL;
}

static gint
user_activation_request_cb (GnomeViewFrame *view_frame, SheetObject *so)
{
	Sheet *sheet = so->sheet;

	printf ("user activation request\n");
	if (sheet->active_object_frame){
		gnome_view_frame_view_deactivate (sheet->active_object_frame);
		if (sheet->active_object_frame != NULL)
                        gnome_view_frame_set_covered (sheet->active_object_frame, TRUE);
		sheet->active_object_frame = NULL;
	}

	gnome_view_frame_view_activate (view_frame);
	sheet_object_make_current (sheet, so);
	
	return FALSE;
}

static gint
view_activated_cb (GnomeViewFrame *view_frame, gboolean activated, SheetObject *so)
{
	Sheet *sheet = so->sheet;
	
        if (activated) {
                if (sheet->active_object_frame != NULL) {
                        g_warning ("View requested to be activated but there is already "
                                   "an active View!\n");
                        return FALSE;
                }

                /*
                 * Otherwise, uncover it so that it can receive
                 * events, and set it as the active View.
                 */
                gnome_view_frame_set_covered (view_frame, FALSE);
                sheet->active_object_frame = view_frame;
        } else {
                /*
                 * If the View is asking to be deactivated, always
                 * oblige.  We may have already deactivated it (see
                 * user_activation_request_cb), but there's no harm in
                 * doing it again.  There is always the possibility
                 * that a View will ask to be deactivated when we have
                 * not told it to deactivate itself, and that is
                 * why we cover the view here.
                 */
                gnome_view_frame_set_covered (view_frame, TRUE);

                if (view_frame == sheet->active_object_frame)
			sheet->active_object_frame = NULL;
	}
	return FALSE;
}

static char *
get_file_name (void)
{
	GtkFileSelection *fs;
	gchar *filename;
 
	fs = GTK_FILE_SELECTION (gtk_file_selection_new ("Select filename"));
	
        gtk_signal_connect (GTK_OBJECT (fs->ok_button),
                            "clicked",
                            GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

        gtk_signal_connect (GTK_OBJECT (fs->cancel_button),
                            "clicked",
                            GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

	gtk_widget_show (GTK_WIDGET (fs));
	gtk_main ();
	
        filename = g_strdup (gtk_file_selection_get_filename (fs));

	gtk_object_destroy (GTK_OBJECT (fs));

	return filename;
}

/**
 * sheet_object_container_land:
 * @so: Sheet Object
 * @fname: Optional file name
 * @own_size: Whether the component is sized to the size it
 *            requests.
 * 
 * Creates client site for object, and binds object to it.
 * Loads data from specified file. If no filename specified
 * we are prompted for it.
 * 
 * Returns: success.
 **/
gboolean
sheet_object_container_land (SheetObject *so, const gchar *fname,
			     gboolean own_size)
{
	SheetObjectContainer *soc;
	GList *l;
	
	g_return_val_if_fail (so != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	soc = SHEET_OBJECT_CONTAINER (so);
	g_return_val_if_fail (soc->client_site == NULL, FALSE);
	
	/*
	 * 1. Kill the temporary objects we used for
	 *    the interactive creation.
	 */
	sheet_object_container_destroy_views (so);

	/*
	 * 2. Create our Client Site.
	 */
	soc->client_site = gnome_client_site_new (so->sheet->workbook->gnome_container);

	/*
	 * 3. Bind it to our object
	 */
	if (!soc->repoid)
		soc->repoid = gnome_bonobo_select_goad_id (_("Select an object"), NULL);
	if (!soc->repoid)
		return FALSE;
	
	soc->object_server = gnome_object_activate_with_goad_id (NULL, soc->repoid, 0, NULL);
	if (!soc->object_server)
		return FALSE;
	
	if (!gnome_client_site_bind_embeddable (soc->client_site, soc->object_server))
		return FALSE;

	/*
	 * 3.a
	 */
	{
		CORBA_Environment ev;
		GNOME_PersistFile ret;
		
		CORBA_exception_init (&ev);
		ret = GNOME_Unknown_query_interface (
			gnome_object_corba_objref (GNOME_OBJECT (soc->object_server)),
			"IDL:GNOME/PersistFile:1.0", &ev);
		if (ev._major == CORBA_NO_EXCEPTION && ret != CORBA_OBJECT_NIL){
			char *file;
			if (!fname)
				file = get_file_name ();
			else
				file = g_strdup (fname);
			if (file){
				GNOME_PersistFile_load (ret, file, &ev);
			}
			GNOME_Unknown_unref ((GNOME_Unknown) ret, &ev);
			CORBA_Object_release (ret, &ev);
			g_free (file);
		} else {
			GNOME_PersistStream ret;
			
			ret = GNOME_Unknown_query_interface (
				gnome_object_corba_objref (GNOME_OBJECT (soc->object_server)),
				"IDL:GNOME/PersistStream:1.0", &ev);
			if (ev._major == CORBA_NO_EXCEPTION){
				if (ret != CORBA_OBJECT_NIL){
					char *file;
					
					if (!fname)
						file = get_file_name ();
					else
						file = g_strdup (fname);
					if (file) {
						GnomeStream *stream;
						
						stream = gnome_stream_fs_open (file, GNOME_Storage_READ);
						if (stream){
							GNOME_PersistStream_load (
								ret,
								(GNOME_Stream) gnome_object_corba_objref (
									GNOME_OBJECT (stream)), &ev);
						}
					}
					GNOME_Unknown_unref ((GNOME_Unknown) ret, &ev);
					CORBA_Object_release (ret, &ev);
					g_free (file);
				}
			}
		}
		
		CORBA_exception_free (&ev);
	}

	/*
	 * 4. Ask the component how big it wants to be, if it is allowed.
	 */
	if (own_size) {
		int dx = -1, dy = -1;
		gtk_signal_emit_by_name (GTK_OBJECT (soc->client_site),
					 "size_query", &dx, &dy);
		if (dx > 0 && dy > 0)
			g_warning ("unimplemented auto size to %d, %d", dx, dy);
	}
	
	/*
	 * 5. Instatiate the views of the object across the sheet views
	 */
	for (l = so->sheet->sheet_views; l; l = l->next){
		GnomeCanvasItem *item;
		SheetView *sheet_view = l->data;
		GnomeViewFrame *view_frame;
		GtkWidget *view_widget;

		view_frame = gnome_client_site_new_view (
			soc->client_site);

		gnome_view_frame_set_ui_handler (
			view_frame,
			so->sheet->workbook->uih);
		
		gtk_signal_connect (GTK_OBJECT (view_frame), "user_activate",
				    GTK_SIGNAL_FUNC (user_activation_request_cb), so);
		gtk_signal_connect (GTK_OBJECT (view_frame), "view_activated",
				    GTK_SIGNAL_FUNC (view_activated_cb), so);
		
		view_widget = gnome_view_frame_get_wrapper (view_frame);
		item = make_container_item (so, sheet_view, view_widget);
		so->realized_list = g_list_prepend (so->realized_list, item);
	}

	return TRUE;
}

static GnomeCanvasItem *
sheet_object_container_realize (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectContainer *soc;
	GnomeCanvasItem *i;
	GnomeViewFrame *view_frame;
	GtkWidget *view_widget;

	soc = SHEET_OBJECT_CONTAINER (so);
	
	if (soc->client_site == NULL)
		view_widget = gtk_button_new_with_label (_("Object server"));
	else {
		view_frame = gnome_client_site_new_view (
			soc->client_site);

		view_widget = gnome_view_frame_get_wrapper (view_frame);
	}

	i = make_container_item (so, sheet_view, view_widget);

	return i;
}

static void
sheet_object_container_set_coords (SheetObject *so,
				   gdouble x1, gdouble y1,
				   gdouble x2, gdouble y2)
{
	GList *l;
	double *c;

	c = so->bbox_points->coords;

	c [0] = x1;
	c [1] = y1;
	c [2] = x2;
	c [3] = y2;

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",      x1,
			"y",      y1,
			"width",  fabs (x2-x1),
			"height", fabs (y2-y1),
			NULL);
	}
}

static void
sheet_object_container_update (SheetObject *so, gdouble to_x, gdouble to_y)
{
	double x1, x2, y1, y2;
	double *c;

	c = so->bbox_points->coords;
	
	x1 = MIN (c [0], to_x);
	x2 = MAX (c [0], to_x);
	y1 = MIN (c [1], to_y);
	y2 = MAX (c [1], to_y);

	sheet_object_container_set_coords (so, x1, y1, x2, y2);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_container_update_coords (SheetObject *so, 
				      gdouble x1d, gdouble y1d,
				      gdouble x2d, gdouble y2d)
{
	double *c = so->bbox_points->coords;
	gdouble x1, y1, x2, y2;
	
	/* Update coordinates */
	c [0] += x1d;
	c [1] += y1d;
	c [2] += x2d;
	c [3] += y2d;

	/* Normalize it */
	x1 = MIN (c [0], c [2]);
	y1 = MIN (c [1], c [3]);
	x2 = MAX (c [0], c [2]);
	y2 = MAX (c [1], c [3]);

	sheet_object_container_set_coords (so, x1, y1, x2, y2);
}

static void
sheet_object_container_creation_finished (SheetObject *so)
{
	SheetObjectContainer *soc = SHEET_OBJECT_CONTAINER (so);
	
	if (soc->client_site == NULL)
		if (!sheet_object_container_land (so, NULL, FALSE)) {
			char *msg = g_strdup_printf ("Failed trying to instantiate '%s'",
						     soc->repoid?soc->repoid:"No ID!");
			gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
			g_free (msg);
		}
}

static void
sheet_object_container_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_container_parent_class = gtk_type_class (sheet_object_get_type ());

	/* Object class method overrides */
	object_class->destroy = sheet_object_container_destroy;
	
	/* SheetObject class method overrides */
	sheet_object_class->realize = sheet_object_container_realize;
	sheet_object_class->update = sheet_object_container_update;
	sheet_object_class->update_coords = sheet_object_container_update_coords;
	sheet_object_class->creation_finished = sheet_object_container_creation_finished;
}

GtkType
sheet_object_container_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectContainer",
			sizeof (SheetObjectContainer),
			sizeof (SheetObjectContainerClass),
			(GtkClassInitFunc) sheet_object_container_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_get_type (), &info);
	}

	return type;
}

SheetObject *
sheet_object_container_new (Sheet *sheet,
			    double x1, double y1,
			    double x2, double y2,
			    const char *objref)
{
	SheetObjectContainer *c;
	SheetObject *so;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (x1 <= x2, NULL);
	g_return_val_if_fail (y1 <= y2, NULL);

#if 0
	GnomeObjectClient *object_server;

	if (!object_server)
		return NULL;

	{
		CORBA_Environment ev;
		GNOME_Plot_VectorFactory vf;
		GNOME_Plot_Vector v;
		GNOME_Plot_DoubleSeq seq;
		SheetSelection *ss;
		double *values;
		int rows, i;
		CORBA_Object component;

		/* Hackety hack */
		CORBA_exception_init (&ev);
		printf ("10\n");

		component = GNOME_obj_query_interface (
			gnome_object_corba_objref (GNOME_OBJECT (object_server)->object),
			"IDL:GNOME/Plot/PlotComponent:1.0", &ev);
		
		if (ev._major != CORBA_NO_EXCEPTION)
			g_error ("Failed");

		GNOME_Plot_PlotComponent_set_plot_type(component, "Bar", &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_error ("Failed");

		vf = GNOME_Plot_PlotComponent_get_vector_factory (component, &ev);
		if (vf == NULL){
			printf ("it is null");			abort ();
		}

		printf ("11\n");
		v = GNOME_Plot_VectorFactory_create_numeric_vector (vf, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_error ("Exception");
		printf ("1\n");
		ss = sheet->selections->data;
		printf ("2\n");

		rows = ss->end_row - ss->start_row;
		values = g_new (double, rows);
		printf ("3\n");
		printf ("Rows: %d\n", rows);
		for (i = 0; i < rows; i++){
			Cell *cell;
			
			cell = sheet_cell_get (sheet, ss->start_col, ss->start_row + i);
			if (cell && cell->value)
				values [i] = value_get_as_float (cell->value);
			else
				values [i] = 0.0;
		}
					       
		seq._length = rows;
		seq._maximum = rows;
		seq._buffer = values;

		GNOME_Plot_Vector_set_from_double_seq (v, 0, &seq, &ev);

		{
			CORBA_Object plotstate = GNOME_Plot_PlotComponent_get_plot(component, &ev);
			
			GNOME_Plot_CategoricalPlot_set_scalar_data(plotstate, v, &ev);
		}

		CORBA_exception_free (&ev);
		printf ("leaving\n");
	}
#endif
	
	c = gtk_type_new (sheet_object_container_get_type ());
	so = SHEET_OBJECT (c);
	
	sheet_object_construct (so, sheet);
	so->bbox_points->coords [0] = x1;
	so->bbox_points->coords [1] = y1;
	so->bbox_points->coords [2] = x2;
	so->bbox_points->coords [3] = y2;

	c->repoid = g_strdup (objref);
	
	return SHEET_OBJECT (c);
}
			  
SheetObject *
sheet_object_graphic_new (Sheet *sheet,
			  double x1, double y1,
			  double x2, double y2)
{
	return sheet_object_container_new (sheet, x1, y1, x2, y2, NULL);
}
