/* vim: set sw=8: */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gnumeric-simple-canvas.h"

#include "sheet-control-gui-priv.h"
#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>

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
	parent = g_type_class_peek_parent (klass);
	klass->key_press_event	 = gnm_simple_canvas_key_press;
	klass->key_release_event = gnm_simple_canvas_key_release;
}

GSF_CLASS (GnmSimpleCanvas, gnm_simple_canvas,
	   gnm_simple_canvas_class_init, NULL,
	   GOC_TYPE_CANVAS)

GocCanvas *
gnm_simple_canvas_new (SheetControlGUI *scg)
{
	GnmSimpleCanvas *gcanvas = g_object_new (GNM_SIMPLE_CANVAS_TYPE, NULL);
	gcanvas->scg  = scg;

	return GOC_CANVAS (gcanvas);
}

void
gnm_simple_canvas_ungrab (GocItem *item, guint32 etime)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS(item->canvas);

	g_return_if_fail (gcanvas != NULL);

	gcanvas->scg->grab_stack--;
	goc_item_ungrab (item);
}

/* FIXME this funtion has been simplified a lot, if it really works like that
 we should remove unused args and not return anything except void */
int
gnm_simple_canvas_grab (GocItem *item, unsigned int event_mask,
			GdkCursor *cursor, guint32 etime)
{
	GnmSimpleCanvas *gcanvas = GNM_SIMPLE_CANVAS(item->canvas);
	int res = 0;

	g_return_val_if_fail (gcanvas != NULL, TRUE);

	gcanvas->scg->grab_stack++;
	goc_item_grab (item);

	return res;
}
