/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-image.c: a wrapper for gdkpixbuf to display images.
 *
 * Author:
 * 	Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-image.h"
#include "sheet-object-impl.h"
#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "application.h"

#include <gsf/gsf-impl-utils.h>
#include <libfoocanvas/foo-canvas-pixbuf.h>
#include <libfoocanvas/foo-canvas-rect-ellipse.h>

#include <math.h>
#include <string.h>
#define DISABLE_DEBUG
#ifndef DISABLE_DEBUG
#define d(code)	do { code; } while (0)
#else
#define d(code)
#endif

struct _SheetObjectImage {
	SheetObject  sheet_object;

	char const   *type;
	guint8       *data;
	guint32	      data_len;

	gboolean dumped;
	double   crop_top;
	double   crop_bottom;
	double   crop_left;
	double   crop_right;
};

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectImageClass;

static SheetObjectClass *sheet_object_image_parent_class;

/**
 * sheet_object_image_new :
 * @type :
 * @data :
 * @data_len
 * @copy_data :
 */
SheetObject *
sheet_object_image_new (char const   *type,
			guint8       *data,
			guint32	      data_len,
			gboolean      copy_data)
{
	SheetObjectImage *soi;

	soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
	soi->type     = type;
	soi->data_len = data_len;
	soi->crop_top = soi->crop_bottom = soi->crop_left = soi->crop_right
		= 0.0;
	if (copy_data) {
		soi->data = g_malloc (data_len);
		memcpy (soi->data, data, data_len);
	} else
		soi->data = data;

	soi->sheet_object.anchor.direction = SO_DIR_DOWN_RIGHT;
	return SHEET_OBJECT (soi);
}

void
sheet_object_image_set_crop (SheetObjectImage *soi,
			     double crop_left,  double crop_top,
			     double crop_right, double crop_bottom)
{
	g_return_if_fail (IS_SHEET_OBJECT_IMAGE (soi));

	soi->crop_left   = crop_left;
	soi->crop_top    = crop_top;
	soi->crop_right  = crop_right;
	soi->crop_bottom = crop_bottom;
}

static void
sheet_object_image_finalize (GObject *object)
{
	SheetObjectImage *soi;

	soi = SHEET_OBJECT_IMAGE (object);
	g_free (soi->data);
	soi->data = NULL;

	G_OBJECT_CLASS (sheet_object_image_parent_class)->finalize (object);
}

static GdkPixbuf *
soi_get_cropped_pixbuf (SheetObjectImage *soi, GdkPixbuf *pixbuf)
{
	int width  = gdk_pixbuf_get_width (pixbuf);
	int height = gdk_pixbuf_get_height (pixbuf);
	int sub_x = rint (soi->crop_left * width);
	int sub_y = rint (soi->crop_top * height);
	int sub_width  = rint (width *
			       (1. - soi->crop_left - soi->crop_right));
	int sub_height = rint (height *
			       (1. - soi->crop_top - soi->crop_bottom));
	GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf (pixbuf, sub_x, sub_y,
						   sub_width, sub_height);
	if (sub) {
		g_object_unref (G_OBJECT (pixbuf));
		pixbuf = sub;
	}
	return pixbuf;
}

/**
 * be sure to unref the result if it is non-NULL
 *
 * TODO : this is really overkill for now.
 * only wmf/emf will require regenerating the pixbug for different scale
 * factors.  And even then we should cache them.
 */
static GdkPixbuf *
soi_get_pixbuf (SheetObjectImage *soi, double scale)
{
	GError *err = NULL;
	guint8 *data;
	guint32 data_len;
	GdkPixbufLoader *loader = NULL;
	GdkPixbuf	*pixbuf = NULL;
	gboolean ret;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (soi), NULL);

	data     = soi->data;
	data_len = soi->data_len;

	if (soi->type != NULL && !strcmp (soi->type, "wmf"))
		loader = gdk_pixbuf_loader_new_with_type (soi->type, &err);
	else
		loader = gdk_pixbuf_loader_new ();

	if (loader) {
		ret = gdk_pixbuf_loader_write (loader,
					       soi->data, soi->data_len, &err);
		/* Close in any case. But don't let error during closing
		 * shadow error from loader_write.  */
		gdk_pixbuf_loader_close (loader, ret ? &err : NULL);
		if (ret)
			pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
		if (pixbuf) {
			g_object_ref (G_OBJECT (pixbuf));
			d (printf ("pixbuf width=%d, height=%d\n",
				   gdk_pixbuf_get_width (pixbuf),
				   gdk_pixbuf_get_height (pixbuf)));
			if (soi->crop_top != 0.0  || soi->crop_bottom != 0.0 ||
			    soi->crop_left != 0.0 || soi->crop_right != 0.0) {
				d (printf ("crop rect top=%g, bottom=%g, "
					   "left=%g, right=%g\n",
					   soi->crop_top, soi->crop_bottom,
					   soi->crop_left, soi->crop_right));
				pixbuf = soi_get_cropped_pixbuf (soi, pixbuf);
			}
		}
		g_object_unref (G_OBJECT (loader));
	}
	if (!pixbuf) {
		if (!soi->dumped) {
			static int count = 0;
			char *filename = g_strdup_printf ("unknown%d.%s",
							  count++, soi->type);
#if 0
			FILE *file = fopen (filename, "w");
			if (file != NULL) {
				fwrite (soi->data, soi->data_len, 1, file);
				fclose (file);
			}
#endif
			g_free (filename);
			soi->dumped = TRUE;
		}

		if (err != NULL) {
			g_warning (err-> message);
			g_error_free (err);
			err = NULL;
		} else {
			g_warning ("Unable to display image");
		}
	}

	return pixbuf;
}

static GObject *
sheet_object_image_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	FooCanvasItem *item = NULL;
	GdkPixbuf *pixbuf, *placeholder = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);

	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));
	pixbuf = soi_get_pixbuf (soi, 1.);

	if (pixbuf == NULL) {
		placeholder = application_get_pixbuf ("unknown_image");
		pixbuf = gdk_pixbuf_copy (placeholder);
	}

	item = foo_canvas_item_new (gcanvas->sheet_object_group,
				    FOO_TYPE_CANVAS_PIXBUF,
				    "pixbuf", pixbuf,
				    NULL);
	g_object_unref (G_OBJECT (pixbuf));

	if (placeholder)
		g_object_set_data (G_OBJECT (item), "tile", placeholder);

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_image_update_bounds (SheetObject *so, GObject *view_obj)
{
	double coords[4];
	double x, y, width, height;
	double old_x1, old_y1, old_x2, old_y2, old_width, old_height;
	FooCanvasItem *view = FOO_CANVAS_ITEM (view_obj);
	SheetControlGUI *scg =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));
	GdkPixbuf *placeholder = g_object_get_data (G_OBJECT (view), "tile");

	scg_object_view_position (scg, so, coords);

	x = MIN (coords [0], coords [2]);
	y = MIN (coords [1], coords [3]);
	width  = fabs (coords [2] - coords [0]);
	height = fabs (coords [3] - coords [1]);

	foo_canvas_item_get_bounds (view, &old_x1, &old_y1, &old_x2, &old_y2);
	foo_canvas_item_set (view,
		"x", x,			"y", y,
		"width",  width,	"width_set",  (width > 0.),
		"height", height,	"height_set", (height > 0.),
		NULL);

	/* regenerate the image if necessary */
	old_width = fabs (old_x1 - old_x2);
	old_height = fabs (old_y1 - old_y2);
	if (placeholder != NULL &&
	    (fabs (width - old_width) > 0.5 || fabs (height - old_height) > 0.5)) {
		GdkPixbuf *newimage = gnm_pixbuf_tile (placeholder,
			(int)width, (int)height);
		foo_canvas_item_set (view, "pixbuf", newimage, NULL);
		g_object_unref (newimage);
	}

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static gboolean
sheet_object_image_read_xml (SheetObject *so,
			       XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectImage *soi;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), TRUE);
	soi = SHEET_OBJECT_IMAGE (so);

	return FALSE;
}

static gboolean
sheet_object_image_write_xml (SheetObject const *so,
				XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectImage *soi;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), TRUE);
	soi = SHEET_OBJECT_IMAGE (so);

	return FALSE;
}

static SheetObject *
sheet_object_image_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectImage *soi;
	SheetObjectImage *new_soi;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), NULL);
	soi = SHEET_OBJECT_IMAGE (so);

	new_soi = g_object_new (GTK_OBJECT_TYPE (soi), NULL);

	return SHEET_OBJECT (new_soi);
}

static void
sheet_object_image_print (SheetObject const *so, GnomePrintContext *ctx,
			  double width, double height)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GdkPixbuf *pixbuf = soi_get_pixbuf (soi, 1.);

	if (pixbuf != NULL) {
		char *raw_image = (char *)gdk_pixbuf_get_pixels (pixbuf);
		gint rowstride	= gdk_pixbuf_get_rowstride (pixbuf);
		gint w	= gdk_pixbuf_get_width  (pixbuf);
		gint h	= gdk_pixbuf_get_height (pixbuf);

		gnome_print_gsave (ctx);
		gnome_print_translate (ctx, 0, -height);
		gnome_print_scale (ctx, width, height);

		if (gdk_pixbuf_get_has_alpha (pixbuf))
			gnome_print_rgbaimage (ctx, raw_image, w, h, rowstride);
		else
			gnome_print_rgbimage (ctx, raw_image, w, h, rowstride);

		g_object_unref (G_OBJECT (pixbuf));
		gnome_print_grestore (ctx);
	}
}

static void
sheet_object_image_default_size (SheetObject const *so, double *w, double *h)
{
	GdkPixbuf *buf = soi_get_pixbuf (SHEET_OBJECT_IMAGE (so), 1.);
	*w = gdk_pixbuf_get_width  (buf);
	*h = gdk_pixbuf_get_height (buf);
	g_object_unref (buf);
}

static void
sheet_object_image_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_image_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize = sheet_object_image_finalize;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_image_new_view;
	sheet_object_class->update_view_bounds = sheet_object_image_update_bounds;
	sheet_object_class->read_xml	  = sheet_object_image_read_xml;
	sheet_object_class->write_xml	  = sheet_object_image_write_xml;
	sheet_object_class->clone         = sheet_object_image_clone;
	sheet_object_class->user_config   = NULL;
	sheet_object_class->print         = sheet_object_image_print;
	sheet_object_class->rubber_band_directly = TRUE;
	sheet_object_class->default_size  = sheet_object_image_default_size;
}

static void
sheet_object_image_init (GObject *obj)
{
	SheetObjectImage *soi;
	SheetObject *so;

	soi = SHEET_OBJECT_IMAGE (obj);
	soi->dumped = FALSE;
	soi->crop_top = soi->crop_bottom = soi->crop_left = soi->crop_right
		= 0.0;

	so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_NONE_MASK;
}

GSF_CLASS (SheetObjectImage, sheet_object_image,
	   sheet_object_image_class_init, sheet_object_image_init,
	   SHEET_OBJECT_TYPE);
