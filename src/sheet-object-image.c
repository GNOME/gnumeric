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

#include <gal/util/e-util.h>
#include <libgnomecanvas/gnome-canvas-pixbuf.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>

#include <math.h>

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

#define SHEET_OBJECT_IMAGE(o)    (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_IMAGE_TYPE, SheetObjectImage))
#define IS_SHEET_OBJECT_IMAGE(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_IMAGE_TYPE))

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
	GdkPixbufLoader *loader;
	GdkPixbuf	*res = NULL;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (soi), NULL);

	data     = soi->data;
	data_len = soi->data_len;

#warning Add optional use of libwmf here to handle wmf
	loader = gdk_pixbuf_loader_new ();

	if (!strcmp (soi->type, "emf") ||
	    !strcmp (soi->type, "wmf") ||
	    !gdk_pixbuf_loader_write (loader, soi->data, soi->data_len, &err)) {

		if (!soi->dumped) {
			static int count = 0;
			char *filename = g_strdup_printf ("unknown%d.%s",  count++, soi->type);
			FILE *file = fopen (filename, "w");
			if (file != NULL) {
				fwrite (soi->data, soi->data_len, 1, file);
				fclose (file);
			}
			g_free (filename);

			if (err != NULL) {
				g_warning (err-> message);
				g_error_free (err);
				err = NULL;
			}
			soi->dumped = TRUE;
		}
	} else {
		res = gdk_pixbuf_loader_get_pixbuf (loader),
		g_object_ref (G_OBJECT (res));
		if (soi->crop_top != 0.0  || soi->crop_bottom != 0.0 ||
		    soi->crop_left != 0.0 || soi->crop_right != 0.0) {
			res = soi_get_cropped_pixbuf (soi, res);
		}
	}

	gdk_pixbuf_loader_close (loader, &err);
	if (err != NULL) {
		g_warning (err->message);
		g_error_free (err);
		err = NULL;
	}
	g_object_unref (G_OBJECT (loader));
	return res;
}

static GObject *
sheet_object_image_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnumericCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GnomeCanvasItem *item = NULL;
	GdkPixbuf	*pixbuf;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);

	gnome_canvas_item_raise_to_top (GNOME_CANVAS_ITEM (gcanvas->sheet_object_group));
	pixbuf = soi_get_pixbuf (soi, 1.);
	if (pixbuf != NULL)
		item = gnome_canvas_item_new (gcanvas->sheet_object_group,
			GNOME_TYPE_CANVAS_PIXBUF,
			"pixbuf", pixbuf,
			NULL);
	else
		item = gnome_canvas_item_new (gcanvas->sheet_object_group,
			GNOME_TYPE_CANVAS_RECT,
			"fill_color",		"white",
			"outline_color",	"black",
			"width_units",		1.,
			NULL);

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_image_update_bounds (SheetObject *so, GObject *view_obj)
{
	double coords [4];
	GnomeCanvasItem   *view = GNOME_CANVAS_ITEM (view_obj);
	SheetControlGUI	  *scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	scg_object_view_position (scg, so, coords);

	/* handle place holders */
	if (GNOME_IS_CANVAS_PIXBUF (view))
		gnome_canvas_item_set (view,
			"x",	  MIN (coords [0], coords [2]),
			"y",	  MIN (coords [1], coords [3]),
			"width",  fabs (coords [2] - coords [0]),
			"height", fabs (coords [3] - coords [1]),
			"width_set",  TRUE,
			"height_set", TRUE,
			NULL);
	else
		gnome_canvas_item_set (view,
			"x1", MIN (coords [0], coords [2]),
			"y1", MIN (coords [1], coords [3]),
			"x2", MAX (coords [0], coords [2]),
			"y2", MAX (coords [1], coords [3]),
			NULL);

	if (so->is_visible)
		gnome_canvas_item_show (view);
	else
		gnome_canvas_item_hide (view);
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
			  double base_x, double base_y)
{
	SheetObjectImage *soi;
	GdkPixbuf	 *pixbuf;
	double coords [4];

	g_return_if_fail (IS_SHEET_OBJECT_IMAGE (so));
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (ctx));
	soi = SHEET_OBJECT_IMAGE (so);

	pixbuf = soi_get_pixbuf (soi, 1.);
	if (pixbuf == NULL)
		return;

	sheet_object_position_pts_get (so, coords);
	gnome_print_gsave (ctx);
	if (gdk_pixbuf_get_has_alpha (pixbuf))
		gnome_print_rgbaimage  (ctx,
					gdk_pixbuf_get_pixels    (pixbuf),
					gdk_pixbuf_get_width     (pixbuf),
					gdk_pixbuf_get_height    (pixbuf),
					gdk_pixbuf_get_rowstride (pixbuf));
	else
		gnome_print_rgbimage  (ctx,
				       gdk_pixbuf_get_pixels    (pixbuf),
				       gdk_pixbuf_get_width     (pixbuf),
				       gdk_pixbuf_get_height    (pixbuf),
				       gdk_pixbuf_get_rowstride (pixbuf));
	g_object_unref (G_OBJECT (pixbuf));
	gnome_print_grestore (ctx);
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

	sheet_object_image_parent_class = gtk_type_class (SHEET_OBJECT_TYPE);

	/* Object class method overrides */
	object_class->finalize = sheet_object_image_finalize;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  = sheet_object_image_new_view;
	sheet_object_class->update_bounds = sheet_object_image_update_bounds;
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

E_MAKE_TYPE (sheet_object_image, "SheetObjectImage", SheetObjectImage,
	     sheet_object_image_class_init, sheet_object_image_init,
	     SHEET_OBJECT_TYPE);
