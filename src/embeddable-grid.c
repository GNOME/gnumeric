/*
 * embeddable-grid.c: An Embeddable Grid part of Gnumeric
 *
 * Contains:
 *    1. EmbeddableGrid Gtk object implementation.
 *    2. CORBA server routines for GNOME::Gnumeric::Grid
 *    3. GridView Gtk object implementation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <bonobo/gnome-embeddable.h>
#include <bonobo/gnome-embeddable-factory.h>
#include "Gnumeric.h"
#include "sheet.h"
#include "embeddable-grid.h"

/*
 * Our vectors for providing methods
 */
POA_GNOME_Gnumeric_Grid__epv embeddable_grid_epv;
POA_GNOME_Gnumeric_Grid__vepv embeddable_grid_vepv;

static GnomeEmbeddableFactory *gnome_embeddable_grid_factory;

static inline EmbeddableGrid *
embeddable_grid_from_servant (PortableServer_Servant servant)
{
	return EMBEDDABLE_GRID (gnome_object_from_servant (servant));
}

static GNOME_Gnumeric_Sheet
Grid_get_sheet (PortableServer_Servant servant, CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	return CORBA_Object_duplicate (eg->sheet->corba_server, ev);
}

void
embeddable_grid_set_header_visibility (EmbeddableGrid *eg,
				       gboolean col_headers_visible,
				       gboolean row_headers_visible)
{
	GList *l;

	g_return_if_fail (eg != NULL);
	g_return_if_fail (IS_EMBEDDABLE_GRID (eg));
	
	for (l = eg->views; l; l = l->next){
		GridView *grid_view = GRID_VIEW (l->data);

	        sheet_view_set_header_visibility (
			grid_view->sheet_view,
			col_headers_visible,
			row_headers_visible);
	}
}
				       
static void
Grid_set_header_visibility (PortableServer_Servant servant,
			    const CORBA_boolean col_headers_visible,
			    const CORBA_boolean row_headers_visible,
			    CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	embeddable_grid_set_header_visibility (
		eg, col_headers_visible, row_headers_visible);
}

static void
Grid_get_header_visibility (PortableServer_Servant servant,
			    CORBA_boolean *col_headers_visible,
			    CORBA_boolean *row_headers_visible,
			    CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	*col_headers_visible = eg->show_col_title;
	*row_headers_visible = eg->show_row_title;
}

static GnomeView *
embeddable_grid_view_factory (GnomeEmbeddable *embeddable,
			      const GNOME_ViewFrame view_frame,
			      void *data)
{
	GnomeView *view;
	EmbeddableGrid *eg = EMBEDDABLE_GRID (embeddable);
	
	view = grid_view_new (eg);

	return GNOME_VIEW (view);
}

static void
init_embeddable_grid_corba_class (void)
{
	/*
	 * vepv
	 */
	embeddable_grid_vepv.GNOME_Unknown_epv = &gnome_object_epv;
	embeddable_grid_vepv.GNOME_Embeddable_epv = &gnome_embeddable_epv;
	embeddable_grid_vepv.GNOME_Gnumeric_Grid_epv = &embeddable_grid_epv;

	/*
	 * epv
	 */
	embeddable_grid_epv.get_sheet    = Grid_get_sheet;
	embeddable_grid_epv.set_header_visibility = Grid_set_header_visibility;
	embeddable_grid_epv.get_header_visibility = Grid_get_header_visibility;
}

static void
embeddable_grid_class_init (GtkObjectClass *Class)
{
	init_embeddable_grid_corba_class ();
}

static void
embeddable_grid_init (GtkObject *object)
{
}

static CORBA_Object
create_embeddable_grid (GnomeObject *object)
{
	POA_GNOME_Gnumeric_Grid *servant;
	CORBA_Environment ev;
	
	servant = (POA_GNOME_Gnumeric_Grid *) g_new0 (GnomeObjectServant, 1);
	servant->vepv = &embeddable_grid_vepv;

	CORBA_exception_init (&ev);
	POA_GNOME_Gnumeric_Grid__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}
	CORBA_exception_free (&ev);

	return gnome_object_activate_servant (object, servant);
}

static void
embeddable_grid_init_anon (EmbeddableGrid *eg)
{
	eg->workbook = gtk_type_new (WORKBOOK_TYPE);
	eg->sheet = sheet_new (eg->workbook, _("Embedded Sheet"));
	workbook_attach_sheet (eg->workbook, eg->sheet);

	/*
	 * Workaround code.  Sheets are born with a sheet_view,
	 * but we do not need it at all.  So manually get rid of it.
	 *
	 * When sheet_new semantics change (see TODO), the code below
	 * will warn about this condition to remove this piece of code
	 */
	if (eg->sheet->sheet_views != NULL){
		sheet_destroy_sheet_view (eg->sheet, SHEET_VIEW (eg->sheet->sheet_views->data));
	} else
		g_error ("Remove workaround code here");
}

EmbeddableGrid *
embeddable_grid_new_anon (void)
{
	EmbeddableGrid *embeddable_grid;
	GNOME_Gnumeric_Grid corba_embeddable_grid;
		
	embeddable_grid = gtk_type_new (EMBEDDABLE_GRID_TYPE);
	embeddable_grid_init_anon (embeddable_grid);
	corba_embeddable_grid = create_embeddable_grid (GNOME_OBJECT (embeddable_grid));

	if (corba_embeddable_grid == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (embeddable_grid));
		return NULL;
	}
	
	gnome_embeddable_construct (
		GNOME_EMBEDDABLE (embeddable_grid), corba_embeddable_grid,
		embeddable_grid_view_factory, NULL);

	return embeddable_grid;
}

EmbeddableGrid *
embeddable_grid_new (Workbook *wb, Sheet *sheet)
{
	EmbeddableGrid *embeddable_grid;
	GNOME_Gnumeric_Grid corba_embeddable_grid;
		
	embeddable_grid = gtk_type_new (EMBEDDABLE_GRID_TYPE);
	corba_embeddable_grid = create_embeddable_grid (GNOME_OBJECT (embeddable_grid));

	if (corba_embeddable_grid == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (embeddable_grid));
		return NULL;
	}

	embeddable_grid->workbook = wb;
	embeddable_grid->sheet = sheet;

	/*
	 * We keep a handle to the Workbook
	 */
	gnome_object_ref (GNOME_OBJECT (embeddable_grid->workbook));
	
	gnome_embeddable_construct (
		GNOME_EMBEDDABLE (embeddable_grid), corba_embeddable_grid,
		embeddable_grid_view_factory, NULL);

	return embeddable_grid;
}

static GnomeObject *
embeddable_grid_factory (GnomeEmbeddableFactory *This, void *data)
{
	EmbeddableGrid *embeddable_grid;

	embeddable_grid = embeddable_grid_new_anon ();

	if (embeddable_grid == NULL)
		return NULL;

	/*
	 * Populate verb list here.
	 */

	return GNOME_OBJECT (embeddable_grid);
}

GtkType
embeddable_grid_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"IDL:GNOME/Gnumeric/EmbeddableGrid:1.0",
			sizeof (EmbeddableGrid),
			sizeof (EmbeddableGridClass),
			(GtkClassInitFunc) embeddable_grid_class_init,
			(GtkObjectInitFunc) embeddable_grid_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};
		type = gtk_type_unique (gnome_embeddable_get_type (), &info);
	}
	
	return type;
}

void
EmbeddableGridFactory_init (void)
{
	gnome_embeddable_grid_factory = gnome_embeddable_factory_new (
		"GNOME:Gnumeric:GridFactory:1.0",
		embeddable_grid_factory, NULL);
}

/*
 * GridView object
 */
GtkType
grid_view_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"EmbeddableGridView",
			sizeof (GridView),
			sizeof (GridViewClass),
			NULL, NULL, NULL, NULL, NULL
		};
		type = gtk_type_unique (gnome_view_get_type (), &info);
	}
	
	return type;
}

/*
 * Invoked by the "destroy" method.  We use this to keep the
 * EmbeddableGrid list of views up to date.
 */
static void
grid_view_destroy (GridView *grid_view)
{
	EmbeddableGrid *eg = grid_view->embeddable;

	eg->views = g_list_remove (eg->views, grid_view);
}

/*
 * Invoked by the "view_activate" method.
 */
static void
grid_view_activate (GridView *grid_view)
{
	/* FIXME: Do we need to do any more work here? */
	g_warning ("FIXME: plug is now private, can't set_focus");
/*	gtk_signal_emit_by_name (GTK_OBJECT (grid_view->view.plug), "set_focus",
	grid_view->sheet_view->sheet_view);*/
			
	gnome_view_activate_notify(GNOME_VIEW(grid_view), TRUE);
}

GnomeView *
grid_view_new (EmbeddableGrid *eg)
{
	GridView *grid_view = NULL;
	GNOME_View corba_grid_view;
	
	grid_view = gtk_type_new (GRID_VIEW_TYPE);

	corba_grid_view = gnome_view_corba_object_create (GNOME_OBJECT (grid_view));
	if (corba_grid_view == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (corba_grid_view));
		return NULL;
	}

	grid_view->embeddable = eg;
	grid_view->sheet_view = sheet_new_sheet_view (eg->sheet);
	gtk_widget_show (GTK_WIDGET (grid_view->sheet_view));
	g_warning ("FIXME: view_construct API changed");
/*	gnome_view_construct (
		GNOME_VIEW (grid_view), corba_grid_view,
		GTK_WIDGET (grid_view->sheet_view));*/

	eg->views = g_list_prepend (eg->views, grid_view);

	gtk_signal_connect (GTK_OBJECT (grid_view), "destroy",
			    GTK_SIGNAL_FUNC (grid_view_destroy), NULL);
	gtk_signal_connect (GTK_OBJECT (grid_view), "view_activate",
			    GTK_SIGNAL_FUNC (grid_view_activate), NULL);
	
	return GNOME_VIEW (grid_view);
}

void
embeddable_grid_set_range (EmbeddableGrid *eg,
			   int start_col, int start_row,
			   int end_col, int end_row)
{
	g_return_if_fail (eg != NULL);
	g_return_if_fail (IS_EMBEDDABLE_GRID (eg));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);
}
