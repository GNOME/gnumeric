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

#include <gal/util/e-util.h>
#include <libgnomecanvas/gnome-canvas-pixbuf.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>

#include <math.h>

typedef struct {
	SheetObject  sheet_object;

	char const   *type;
	guint8       *data;
	guint32	      data_len;

	gboolean dumped;
} SheetObjectImage;

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectImageClass;

#define SHEET_OBJECT_IMAGE(o)    (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_IMAGE_TYPE, SheetObjectImage))
#define IS_SHEET_OBJECT_IMAGE(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_IMAGE_TYPE))

static SheetObjectClass *sheet_object_image_parent_class;

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
	if (copy_data) {
		soi->data = g_malloc (data_len);
		memcpy (soi->data, data, data_len);
	} else
		soi->data = data;

	return SHEET_OBJECT (soi);
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

/**
 * be sure to unref the result if it is non-NULL
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

	if (!gdk_pixbuf_loader_write (loader, soi->data, soi->data_len, &err)) {

		if (!soi->dumped) {
			static int count = 0;
			char *filename = g_strdup_printf ("unknown%d",  count++);
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
sheet_object_image_new_view (SheetObject *so, SheetControlGUI *scg)
{
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GnomeCanvasItem *item = NULL;
	GdkPixbuf	*pixbuf;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), NULL);
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	pixbuf = soi_get_pixbuf (soi, 1.);
	if (pixbuf != NULL)
		item = gnome_canvas_item_new (gcanvas->object_group,
			GNOME_TYPE_CANVAS_PIXBUF,
			"pixbuf", pixbuf,
			NULL);
	else
		item = gnome_canvas_item_new (gcanvas->object_group,
			GNOME_TYPE_CANVAS_RECT,
			"fill_color",		"white",
			"outline_color",	"black",
			"width_units",		1.,
			NULL);

	scg_object_register (so, item);
	return G_OBJECT (item);
}

static void
sheet_object_image_update_bounds (SheetObject *so, GObject *view,
				  SheetControlGUI *scg)
{
	double coords [4];

	scg_object_view_position (scg, so, coords);

	/* handle place holders */
	if (GNOME_IS_CANVAS_PIXBUF (view))
		gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
			"x",	  MIN (coords [0], coords [2]),
			"y",	  MIN (coords [1], coords [3]),
			"width",  fabs (coords [2] - coords [0]),
			"height", fabs (coords [3] - coords [1]),
			"width_set",  TRUE,
			"height_set", TRUE,
			NULL);
	else
		gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
			"x1", MIN (coords [0], coords [2]),
			"y1", MIN (coords [1], coords [3]),
			"x2", MAX (coords [0], coords [2]),
			"y2", MAX (coords [1], coords [3]),
			NULL);

	if (so->is_visible)
		gnome_canvas_item_show (GNOME_CANVAS_ITEM (view));
	else
		gnome_canvas_item_hide (GNOME_CANVAS_ITEM (view));
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

	sheet_object_position_pts_get (so, coords);

	gnome_print_gsave (ctx);

	pixbuf = soi_get_pixbuf (soi, 1.);
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
}

static void
sheet_object_image_init (GObject *obj)
{
	SheetObjectImage *soi;
	SheetObject *so;

	soi = SHEET_OBJECT_IMAGE (obj);
	soi->dumped = FALSE;

	so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_NONE_MASK;
}

E_MAKE_TYPE (sheet_object_image, "SheetObjectImage", SheetObjectImage,
	     sheet_object_image_class_init, sheet_object_image_init,
	     SHEET_OBJECT_TYPE);
