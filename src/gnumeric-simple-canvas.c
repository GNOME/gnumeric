/* vim: set sw=8: */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gnumeric-simple-canvas.h"

#include "sheet-control-gui-priv.h"
#include <gal/util/e-util.h>

static GtkWidgetClass const *parent;
static gint
gnm_simple_canvas_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS (widget);

	if (gcanvas->scg->grab_stack > 0)
		return TRUE;
	return parent->key_press_event (widget, event);
}

static gint
gnm_simple_canvas_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS (widget);

	if (gcanvas->scg->grab_stack > 0)
		return TRUE;
	return parent->key_release_event (widget, event);
}

static void
gnm_simple_canvas_class_init (GtkWidgetClass *klass)
{
	parent = gtk_type_class (gnome_canvas_get_type ());

	klass->key_press_event	 = gnm_simple_canvas_key_press;
	klass->key_release_event = gnm_simple_canvas_key_release;
}

E_MAKE_TYPE (gnm_simple_canvas, "GnmSimpleCanvas", GnmSimpleCanvas,
	     gnm_simple_canvas_class_init, NULL,
	     GNOME_TYPE_CANVAS);

GnomeCanvas *
gnm_simple_canvas_new (SheetControlGUI *scg)
{
	GnmSimpleCanvas *gcanvas = gtk_type_new (gnm_simple_canvas_get_type ());
	gcanvas->scg = scg;
	return GNOME_CANVAS (gcanvas);
}

void
gnm_simple_canvas_ungrab (GnomeCanvasItem *item, guint32 etime)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS(item->canvas);

	g_return_if_fail (gcanvas != NULL);

	gcanvas->scg->grab_stack--;
	gnome_canvas_item_ungrab (item, etime);

	/* We flush after the ungrab, to have the ungrab take effect
	 * immediately operations might take a while, and we
	 * do not want the mouse to be grabbed the entire time.
	 */
	gdk_flush ();
}

int
gnm_simple_canvas_grab (GnomeCanvasItem *item, unsigned int event_mask,
			GdkCursor *cursor, guint32 etime)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS(item->canvas);
	int res;

	g_return_val_if_fail (gcanvas != NULL, TRUE);

	gcanvas->scg->grab_stack++;
	res = gnome_canvas_item_grab (item, event_mask, cursor, etime);

	/* Be extra paranoid.  Ensure that the grab is registered */
	gdk_flush ();

	return res;
}
