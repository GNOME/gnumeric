#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

/* Signals emited by the Gnumeric Sheet widget */
enum {
	GNUMERIC_SHEET_LAST_SIGNAL
};

static guint sheet_signals [GNUMERIC_SHEET_LAST_SIGNAL] = { 0 };

static GnomeCanvasClass *sheet_parent_class;

static void
gnumeric_sheet_destroy (GtkObject *object)
{
	GnumericSheet *gsheet;

	/* Add shutdown code here */
	gsheet = GNUMERIC_SHEET (object);
	
	if (GTK_OBJECT_CLASS (sheet_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_parent_class)->destroy)(object);
}

static GnumericSheet *
gnumeric_sheet_create (Sheet *sheet)
{
	GnumericSheet *gsheet;
	GnomeCanvas   *canvas;
	
	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	canvas = GNOME_CANVAS (gsheet);

	canvas->visual = gtk_widget_get_default_visual ();
	canvas->colormap = gtk_widget_get_default_colormap ();
	canvas->cc   = gdk_color_context_new (canvas->visual, canvas->colormap);
	canvas->root = gnome_canvas_group_new (canvas);
	
	gsheet->sheet   = sheet;
	gsheet->top_col = 0;
	gsheet->top_row = 0;

	return gsheet;
}

GtkWidget *
gnumeric_sheet_new (Sheet *sheet)
{
	GnumericSheet *gsheet;
	
	gsheet = gnumeric_sheet_create (sheet);
	gnome_canvas_set_size (GNOME_CANVAS (gsheet), 300, 100);
	
	gsheet->item_grid = ITEM_GRID (gnome_canvas_item_new (
		GNOME_CANVAS (gsheet),
		GNOME_CANVAS_GROUP (GNOME_CANVAS(gsheet)->root),
		item_grid_get_type (),
		"ItemGrid::default_grid_color", "gray",
		NULL));
	return GTK_WIDGET (gsheet);
}

static void
gnumeric_sheet_class_init (GnumericSheetClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	canvas_class = (GnomeCanvasClass *) class;
	
	sheet_parent_class = gtk_type_class (gnome_canvas_get_type());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;
}

static void
gnumeric_sheet_init (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);

	printf ("idel_id=%d\n", canvas->idle_id);
	printf ("pixs=%g\n", canvas->pixels_per_unit);
}

GtkType
gnumeric_sheet_get_type (void)
{
	static GtkType gnumeric_sheet_type = 0;

	if (!gnumeric_sheet_type){
		GtkTypeInfo gnumeric_sheet_info = {
			"GnumericSheet",
			sizeof (GnumericSheet),
			sizeof (GnumericSheetClass),
			(GtkClassInitFunc) gnumeric_sheet_class_init,
			(GtkObjectInitFunc) gnumeric_sheet_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		gnumeric_sheet_type = gtk_type_unique (gnome_canvas_get_type (), &gnumeric_sheet_info);
	}

	return gnumeric_sheet_type;
}

