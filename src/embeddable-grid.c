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
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "embeddable-grid.h"

#include "sheet.h"
#include "workbook.h"
#include "sheet-private.h"
#include <bonobo/bonobo-generic-factory.h>

static BonoboGenericFactory *bonobo_embeddable_grid_factory;

static inline EmbeddableGrid *
embeddable_grid_from_servant (PortableServer_Servant servant)
{
	return EMBEDDABLE_GRID (bonobo_object_from_servant (servant));
}

static GNOME_Gnumeric_Sheet
Grid_get_sheet (PortableServer_Servant servant, CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	return CORBA_Object_duplicate (eg->sheet->priv->corba_server, ev);
}

static void
Grid_set_header_visibility (PortableServer_Servant servant,
			    const CORBA_boolean col_headers_visible,
			    const CORBA_boolean row_headers_visible,
			    CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	g_return_if_fail (IS_EMBEDDABLE_GRID (eg));

	eg->sheet->hide_col_header = !col_headers_visible;
	eg->sheet->hide_row_header = !row_headers_visible;

	sheet_adjust_preferences (eg->sheet, TRUE, FALSE);
}

static void
Grid_get_header_visibility (PortableServer_Servant servant,
			    CORBA_boolean *col_headers_visible,
			    CORBA_boolean *row_headers_visible,
			    CORBA_Environment *ev)
{
	EmbeddableGrid *eg = embeddable_grid_from_servant (servant);

	*col_headers_visible = !eg->sheet->hide_col_header;
	*row_headers_visible = !eg->sheet->hide_row_header;
}

static BonoboView *
embeddable_grid_view_factory (BonoboEmbeddable *embeddable,
			      const Bonobo_ViewFrame view_frame,
			      void *data)
{
	BonoboView *view;
	EmbeddableGrid *eg = EMBEDDABLE_GRID (embeddable);

	view = grid_view_new (eg);

	return BONOBO_VIEW (view);
}

static void
embeddable_grid_class_init (EmbeddableGridClass *klass)
{
	POA_GNOME_Gnumeric_Grid__epv *epv = &klass->epv;

	epv->get_sheet = Grid_get_sheet;
	epv->set_header_visibility = Grid_set_header_visibility;
	epv->get_header_visibility = Grid_get_header_visibility;
}

static void
embeddable_grid_init_anon (EmbeddableGrid *eg)
{
	eg->workbook = workbook_new_with_sheets (1);
	eg->sheet = workbook_sheet_by_index (eg->workbook, 0);
}

EmbeddableGrid *
embeddable_grid_new_anon (void)
{
	EmbeddableGrid *embeddable_grid;

	embeddable_grid = gtk_type_new (EMBEDDABLE_GRID_TYPE);
	embeddable_grid_init_anon (embeddable_grid);

	bonobo_embeddable_construct (
		BONOBO_EMBEDDABLE (embeddable_grid),
		embeddable_grid_view_factory, NULL);

	return embeddable_grid;
}

EmbeddableGrid *
embeddable_grid_new (Workbook *wb, Sheet *sheet)
{
	EmbeddableGrid *embeddable_grid;

	embeddable_grid = gtk_type_new (EMBEDDABLE_GRID_TYPE);

	embeddable_grid->workbook = wb;
	embeddable_grid->sheet = sheet;

	/*
	 * We keep a handle to the Workbook
	 */
	bonobo_object_ref (BONOBO_OBJECT (embeddable_grid->workbook->priv));

	bonobo_embeddable_construct (
		BONOBO_EMBEDDABLE (embeddable_grid),
		embeddable_grid_view_factory, NULL);

	return embeddable_grid;
}

static BonoboObject *
embeddable_grid_factory (BonoboGenericFactory *This, void *data)
{
	EmbeddableGrid *embeddable_grid;

	embeddable_grid = embeddable_grid_new_anon ();

	if (embeddable_grid == NULL) {
		g_warning ("Failed to create new embeddable grid");
		return NULL;
	}

	/*
	 * Populate verb list here.
	 */

	return BONOBO_OBJECT (embeddable_grid);
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
			NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};
		type = bonobo_x_type_unique (
			bonobo_embeddable_get_type (),
			POA_GNOME_Gnumeric_Grid__init, NULL,
			GTK_STRUCT_OFFSET (EmbeddableGridClass, epv),
			&info);
	}

	return type;
}

gboolean
EmbeddableGridFactory_init (void)
{
	bonobo_embeddable_grid_factory = bonobo_generic_factory_new (
		"OAFIID:GNOME_Gnumeric_GridFactory",
		embeddable_grid_factory, NULL);
	return TRUE;
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
		type = bonobo_x_type_unique (
			bonobo_view_get_type (),
			NULL, NULL, 0, &info);
	}

	return type;
}

/*
 * Invoked by the "view_activate" method.
 */
static void
grid_view_activate (GridView *grid_view, gboolean state)
{
	if (state) {
		g_return_if_fail (grid_view != NULL);

		scg_take_focus (grid_view->scg);
	}

	bonobo_view_activate_notify (BONOBO_VIEW (grid_view), state);
}

BonoboView *
grid_view_new (EmbeddableGrid *eg)
{
	GridView *grid_view = NULL;

	grid_view = gtk_type_new (GRID_VIEW_TYPE);

	grid_view->embeddable = eg;
	grid_view->scg = sheet_control_gui_new (eg->sheet);
	gtk_widget_show (scg_toplevel (grid_view->scg));
	gtk_widget_set_usize (scg_toplevel (grid_view->scg), 320, 200);

	grid_view = GRID_VIEW (
		bonobo_view_construct (	BONOBO_VIEW (grid_view),
		scg_toplevel (grid_view->scg)));
	if (!grid_view)
		return NULL;

	gtk_signal_connect (GTK_OBJECT (grid_view), "activate",
			    GTK_SIGNAL_FUNC (grid_view_activate), NULL);

	return BONOBO_VIEW (grid_view);
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
