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
		gtk_object_destroy (GTK_OBJECT (soc->client_site));
	
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

void
sheet_object_container_land (SheetObject *so)
{
	SheetObjectContainer *soc;
	GnomeObjectClient *component;
	GList *l;

	g_return_if_fail (so != NULL);
	g_return_if_fail (IS_SHEET_OBJECT (so));

	soc = SHEET_OBJECT_CONTAINER (so);
	g_return_if_fail (soc->client_site == NULL);
	
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
	if (!gnome_client_site_bind_embeddable (soc->client_site, soc->object_server))
		return;
	
	/*
	 * 4. Instatiate the views of the object across the sheet views
	 */
	for (l = so->sheet->sheet_views; l; l = l->next){
		GnomeCanvasItem *item;
		SheetView *sheet_view = l->data;
		GnomeViewFrame *view_frame;
		GtkWidget *view_widget;

		view_frame = gnome_client_site_new_view (
			soc->client_site);
		view_widget = gnome_view_frame_get_wrapper (view_frame);
		item = make_container_item (so, sheet_view, view_widget);
		so->realized_list = g_list_prepend (so->realized_list, item);
	}
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
		sheet_object_container_land (so);
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
			    char *repoid)
{
	SheetObjectContainer *c;
	SheetObject *so;
	GnomeObjectClient *object_server;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (x1 <= x2, NULL);
	g_return_val_if_fail (y1 <= y2, NULL);

        object_server = gnome_object_activate_with_goad_id (NULL, repoid, 0, NULL);
	if (!object_server)
		return NULL;

#if 0
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
			printf ("it is null");
			abort ();
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

	c->repoid = g_strdup (repoid);
	c->object_server = object_server;

	return SHEET_OBJECT (c);
}
			  
SheetObject *
sheet_object_graphic_new (Sheet *sheet,
			  double x1, double y1,
			  double x2, double y2)
{
	return sheet_object_container_new (sheet, x1, y1, x2, y2, "Guppi_component");
}
