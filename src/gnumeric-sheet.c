#ifdef HAVE_CONFIG_H
#   include <config.h>
#endif

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

	if (GTK_OBJECT_CLASS (sheet_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_parent_class)->destroy)(object);
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
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		gnumeric_sheet_type = gtk_type_unique (gtk_object_get_type (), &gnumeric_sheet_info);
	}

	return gnumeric_sheet_type;
}

static void
gnumeric_sheet_class_init (GnumericSheetClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;

	sheet_parent_class = gtk_type_class (gnome_canvas_get_type());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;
}
