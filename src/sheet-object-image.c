/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-image.c: a wrapper for gdkpixbuf to display images.
 *
 * Author:
 * 	Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "sheet-object-image.h"
#include "sheet-object-impl.h"
#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "gui-file.h"
#include "application.h"

#include <goffice/utils/go-libxml-extras.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-utils.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-pixbuf.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <goffice/utils/go-file.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkstock.h>

#include <math.h>
#include <string.h>
#define DISABLE_DEBUG
#ifndef DISABLE_DEBUG
#define d(code)	do { code; } while (0)
#else
#define d(code)
#endif

#define CC2XML(s) ((const xmlChar *)(s))

static void
so_image_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_image_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);
	if (visible) {
		double x, y, width, height;
		double old_x1, old_y1, old_x2, old_y2, old_width, old_height;
		GdkPixbuf *placeholder = g_object_get_data (G_OBJECT (view), "tile");

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

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_image_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_image_view_destroy;
	sov_iface->set_bounds	= so_image_view_set_bounds;
}
typedef FooCanvasPixbuf		SOImageFooView;
typedef FooCanvasPixbufClass	SOImageFooViewClass;
static GSF_CLASS_FULL (SOImageFooView, so_image_foo_view,
	NULL, NULL,
	FOO_TYPE_CANVAS_PIXBUF, 0,
	GSF_INTERFACE (so_image_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

/****************************************************************************/
struct _SheetObjectImage {
	SheetObject  sheet_object;

	char         *type;
	GByteArray   bytes;

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

enum {
	PROP_0,
	PROP_IMAGE_TYPE,
	PROP_IMAGE_DATA,
	PROP_PIXBUF
};

/**
 * sheet_object_image_set_image :
 * @so : #SheetObjectImage
 * @type :
 * @data :
 * @data_len
 * @copy_data :
 *
 * Assign raw data and type to @so assuming that it has not been initialized
 * yet.
 **/
void
sheet_object_image_set_image (SheetObjectImage *soi,
			      char const   *type,
			guint8       *data,
			      unsigned      data_len,
			gboolean      copy_data)
{
	g_return_if_fail (IS_SHEET_OBJECT_IMAGE (soi));
	g_return_if_fail (soi->bytes.data == NULL && soi->bytes.len == 0);

	soi->type       = g_strdup (type);
	soi->bytes.len  = data_len;
	soi->bytes.data = copy_data ? g_memdup (data, data_len) : data;
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
	g_free (soi->bytes.data);
	g_free (soi->type);
	soi->bytes.data = NULL;

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

static void
soi_info_cb (GdkPixbufLoader *loader, 
	     int              width,
	     int              height,
	     gpointer         data)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (data);
	GdkPixbufFormat *format = gdk_pixbuf_loader_get_format (loader);
	char *name = gdk_pixbuf_format_get_name (format);
	
	if (soi->type)
		g_free (soi->type);
	soi->type = name;
}

/**
 * be sure to unref the result if it is non-NULL
 *
 * TODO : this is really overkill for now.
 * only wmf/emf will require regenerating the pixbuf for different scale
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

	data     = soi->bytes.data;
	data_len = soi->bytes.len;
	if (data == NULL || data_len == 0)
		return pixbuf;

	if (soi->type != NULL && !strcmp (soi->type, "wmf"))
		loader = gdk_pixbuf_loader_new_with_type (soi->type, &err);
	else
		loader = gdk_pixbuf_loader_new ();

	if (soi->type == NULL || strlen (soi->type) == 0)
		g_signal_connect (loader, "size-prepared", 
				  G_CALLBACK (soi_info_cb), soi);
		
	if (loader) {
		ret = gdk_pixbuf_loader_write (loader,
					       soi->bytes.data, soi->bytes.len,
					       &err);
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
			GsfOutput *file = gsf_output_stdio_new (filename, NULL);
			if (file) {
				gsf_output_write (GSF_OUTPUT (file),
						  soi->bytes.len,
						  soi->bytes.data);
				gsf_output_close (GSF_OUTPUT (file));
				g_object_unref (file);
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

static SheetObjectView *
sheet_object_image_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	FooCanvasItem *item = NULL;
	GdkPixbuf *pixbuf, *placeholder = NULL;

	pixbuf = soi_get_pixbuf (soi, 1.);

	if (pixbuf == NULL) {
		placeholder = gnm_app_get_pixbuf ("unknown_image");
		pixbuf = gdk_pixbuf_copy (placeholder);
	}

	item = foo_canvas_item_new (gcanvas->object_views,
		so_image_foo_view_get_type (),
		"pixbuf", pixbuf,
		NULL);
	g_object_unref (G_OBJECT (pixbuf));

	if (placeholder)
		g_object_set_data (G_OBJECT (item), "tile", placeholder);

	return gnm_pane_object_register (so, item, TRUE);
}

static gboolean
soi_gdk_pixbuf_save (const gchar *buf,
		     gsize count,
		     GError **error,
		     gpointer data)
{
	GsfOutput *output = GSF_OUTPUT (data);
	gboolean ok = gsf_output_write (output, count, buf);

	if (!ok && error)
		*error = g_error_copy (gsf_output_error (output));

	return ok;
}

static GOImageType const std_fmts[] = {
	{(char *) "png",  (char *) N_("PNG (raster graphics)"),   
	 (char *) "png", TRUE},
	{(char *) "jpeg", (char *) N_("JPEG (photograph)"),      
	 (char *) "jpg", TRUE},
	{(char *) "svg",  (char *) N_("SVG (vector graphics)"),  
	 (char *) "svg", FALSE},
	{(char *) "emf",  (char *) N_("EMF (extended metafile)"),
	 (char *) "emf", FALSE},
	{(char *) "wmf",  (char *) N_("WMF (windows metafile)"), 
	 (char *) "wmf", FALSE}
};

static GOImageType *
soi_get_image_fmt (SheetObjectImage *soi)
{
	GOImageType *ret = g_new0 (GOImageType, 1);
	GSList *l, *pixbuf_fmts;
	guint i;
	
	ret->name = g_strdup (soi->type);
	for (i = 0; i < G_N_ELEMENTS (std_fmts); i++) {
		GOImageType const *fmt = &std_fmts[i];
		
		if (strcmp (soi->type, fmt->name) == 0) {
			ret->desc = g_strdup (fmt->desc);
			ret->ext  = g_strdup (fmt->ext);
			ret->has_pixbuf_saver = fmt->has_pixbuf_saver;
			return ret;
		}
	}

	ret->desc = g_ascii_strup (ret->name, -1);
	ret->has_pixbuf_saver = FALSE;

	/* Not found in standard formats - look in gdk-pixbuf */
	pixbuf_fmts = gdk_pixbuf_get_formats ();
	for (l = pixbuf_fmts; l != NULL; l = l->next) {
		GdkPixbufFormat *fmt = (GdkPixbufFormat *)l->data;
		gchar *name = gdk_pixbuf_format_get_name (fmt);
		int cmp = strcmp (soi->type, name);
		
		g_free (name);
		if (cmp == 0) {
			gchar **exts;
			
			exts = gdk_pixbuf_format_get_extensions (fmt);
			ret->ext = g_strdup (exts[0]);
			g_strfreev (exts);
			break;
		}
	}
	g_slist_free (pixbuf_fmts);

	if (ret->ext == NULL)
		ret->ext = g_strdup (ret->name);

	return ret;
}

static void
soi_free_image_fmt (gpointer data)
{
	GOImageType *fmt = (GOImageType *) data;

	g_free (fmt->name);
	g_free (fmt->desc);
	g_free (fmt->ext);
	g_free (fmt);
}

static void
soi_cb_save_as (SheetObject *so, SheetControl *sc)
{
	WorkbookControlGUI *wbcg;
	char *uri;
	GsfOutput *output;
	GSList *l = NULL;
	GOImageType const *orig_fmt, *sel_fmt;
	GOImageType *fmt;
	GdkPixbuf *pixbuf = NULL;
	guint i;
	GError *err = NULL;
	gboolean res;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	g_return_if_fail (soi != NULL);

	/* Put original format of image first in menu. */
	orig_fmt = soi_get_image_fmt (soi);
	sel_fmt  = orig_fmt;
	l = g_slist_prepend (l, (gpointer)orig_fmt);

	/* Put jpeg and png in menu if we were able to render */
	if ((pixbuf = soi_get_pixbuf (soi, 1.0)) != NULL) {
		for (i = 0; i < G_N_ELEMENTS (std_fmts); i++) {
			if (strcmp (soi->type, std_fmts[i].name) != 0 &&
			    std_fmts[i].has_pixbuf_saver) {
				fmt = g_new0 (GOImageType, 1);
				fmt->name = g_strdup (std_fmts[i].name); 
				fmt->desc = g_strdup (std_fmts[i].desc);
				fmt->ext  = g_strdup (std_fmts[i].ext);
				fmt->has_pixbuf_saver = TRUE; 
				l = g_slist_prepend (l, fmt);
			}
		}
		l = g_slist_reverse (l);
	}		

	wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));

	uri = gui_get_image_save_info (wbcg_toplevel (wbcg), l, &sel_fmt);
	if (!uri)
		goto out;

	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	if (sel_fmt == orig_fmt)
		res = gsf_output_write (output, 
					soi->bytes.len, soi->bytes.data);
	else
		res = gdk_pixbuf_save_to_callback (pixbuf,
						   soi_gdk_pixbuf_save, output,
						   sel_fmt->name,
						   &err, NULL);

	gsf_output_close (output);
	g_object_unref (output);

	if (!res && err == NULL)
		err = g_error_new (gsf_output_error_id (), 0,
				   _("Unknown failure while saving image"));
	if (!res)
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

out:
	if (pixbuf)
		g_object_unref (pixbuf);
	g_free (uri);
	go_slist_free_custom (l, soi_free_image_fmt);
}

static void
sheet_object_image_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const soi_action =
		{ GTK_STOCK_SAVE_AS, N_("_Save as image"), NULL, 0, soi_cb_save_as };
	sheet_object_image_parent_class->populate_menu (so, actions);
	go_ptr_array_insert (actions, (gpointer) &soi_action, 1);
}

static gboolean
sheet_object_image_read_xml_dom (SheetObject *so, char const *typename,
				 XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectImage *soi;
	xmlNodePtr child;
	xmlChar    *type, *content;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), TRUE);
	soi = SHEET_OBJECT_IMAGE (so);

	child = e_xml_get_child_by_name (tree, "Content");
	type  = xmlGetProp (child, CC2XML ("image-type"));
	if (type == NULL)
		return FALSE;
	content = xmlNodeGetContent (child);
	if (content == NULL) {
		xmlFree (type);
		return FALSE;
	}
	soi->type       = g_strdup (type);
	soi->bytes.len  = gsf_base64_decode_simple (content, strlen (content));
	soi->bytes.data = g_memdup (content, soi->bytes.len);
	xmlFree (type);
	xmlFree (content);

	return FALSE;
}

static gboolean
sheet_object_image_write_xml_dom (SheetObject const *so,
				  XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectImage *soi;

	g_return_val_if_fail (IS_SHEET_OBJECT_IMAGE (so), TRUE);
	soi = SHEET_OBJECT_IMAGE (so);

	return FALSE;
}

static void
sheet_object_image_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetObjectImage *soi;

	g_return_if_fail (IS_SHEET_OBJECT_IMAGE (so));
	soi = SHEET_OBJECT_IMAGE (so);

	gsf_xml_out_add_float (output, "crop-top", soi->crop_top, 3);
	gsf_xml_out_add_float (output, "crop-bottom", soi->crop_bottom, 3);
	gsf_xml_out_add_float (output, "crop-left", soi->crop_left, 3);
	gsf_xml_out_add_float (output, "crop-right", soi->crop_right, 3);
 	gsf_xml_out_start_element (output, "Content");
	if (soi->type != NULL)
		gsf_xml_out_add_cstr (output, "image-type", soi->type);
	gsf_xml_out_add_uint (output, "size-bytes", soi->bytes.len);
	gsf_xml_out_add_base64 (output, NULL, soi->bytes.data, soi->bytes.len);
 	gsf_xml_out_end_element (output);
}

static void
sheet_object_image_copy (SheetObject *dst, SheetObject const *src)
{
	SheetObjectImage const *soi = SHEET_OBJECT_IMAGE (src);
	SheetObjectImage   *new_soi = SHEET_OBJECT_IMAGE (dst);

	new_soi->type		= g_strdup (soi->type);
	new_soi->bytes.len	= soi->bytes.len;
	new_soi->bytes.data	= g_memdup (soi->bytes.data, soi->bytes.len);
	new_soi->crop_top	= soi->crop_top;
	new_soi->crop_bottom	= soi->crop_bottom;
	new_soi->crop_left	= soi->crop_left;
	new_soi->crop_right	= soi->crop_right;
}

static void
sheet_object_image_print (SheetObject const *so, GnomePrintContext *ctx,
			  double width, double height)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GdkPixbuf *pixbuf = soi_get_pixbuf (soi, 1.);

	if (pixbuf != NULL) {
		const guchar *raw_image = gdk_pixbuf_get_pixels (pixbuf);
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
sheet_object_image_get_property (GObject     *object,
				 guint        property_id,
				 GValue      *value,
				 GParamSpec  *pspec)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (object);
	GdkPixbuf *pixbuf;

	switch (property_id) {
	case PROP_IMAGE_TYPE:
		g_value_set_string (value, soi->type);
		break;
	case PROP_IMAGE_DATA:
		g_value_set_pointer (value, &soi->bytes);
		break;
	case PROP_PIXBUF:
		pixbuf = soi_get_pixbuf (soi, 1.0);
		g_value_set_object (value, pixbuf);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
sheet_object_image_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	sheet_object_image_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize = sheet_object_image_finalize;
	object_class->get_property = sheet_object_image_get_property;

	/* SheetObject class method overrides */
	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->new_view	  	= sheet_object_image_new_view;
	sheet_object_class->populate_menu	= sheet_object_image_populate_menu;
	sheet_object_class->read_xml_dom	= sheet_object_image_read_xml_dom;
	sheet_object_class->write_xml_dom	= sheet_object_image_write_xml_dom;
	sheet_object_class->write_xml_sax	= sheet_object_image_write_xml_sax;
	sheet_object_class->copy		= sheet_object_image_copy;
	sheet_object_class->user_config		= NULL;
	sheet_object_class->print		= sheet_object_image_print;
	sheet_object_class->default_size	= sheet_object_image_default_size;
	sheet_object_class->rubber_band_directly = TRUE;

	/* The property strings don't need translation */
	g_object_class_install_property 
		(object_class,
		 PROP_IMAGE_TYPE,
		 g_param_spec_string ("image-type", "Image type",
				      "Type of image",
				      NULL,
				      G_PARAM_READABLE));
	g_object_class_install_property 
		(object_class,
		 PROP_IMAGE_DATA,
		 g_param_spec_pointer ("image-data", "Image data",
				       "Image data",
				       G_PARAM_READABLE));
	g_object_class_install_property 
		(object_class,
		 PROP_PIXBUF,
		 g_param_spec_object ("pixbuf", "Pixbuf",
				       "Pixbuf",
				       GDK_TYPE_PIXBUF,
				       G_PARAM_READABLE));
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
	so->anchor.direction = SO_DIR_DOWN_RIGHT;
}

GSF_CLASS (SheetObjectImage, sheet_object_image,
	   sheet_object_image_class_init, sheet_object_image_init,
	   SHEET_OBJECT_TYPE);
