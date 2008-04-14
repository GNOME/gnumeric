/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-image.c: a wrapper for gdkpixbuf to display images.
 *
 * Author:
 *	Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "sheet-object-image.h"

#include "gnm-pane.h"
#include "wbc-gtk.h"
#include "sheet-object-impl.h"
#include "sheet-control-gui.h"
#include "gui-file.h"
#include "application.h"
#include "xml-io.h"

#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-file.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-pixbuf.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <goffice/gtk/go-pixbuf.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-utils.h>
#include <gtk/gtk.h>

#include <math.h>
#include <string.h>
#define DISABLE_DEBUG
#ifndef DISABLE_DEBUG
#define d(code)	do { code; } while (0)
#else
#define d(code)
#endif

#define CC2XML(s) ((xmlChar const *)(s))
#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

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
			GdkPixbuf *newimage = go_pixbuf_tile (placeholder,
				(guint)width, (guint)height);
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
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_PIXBUF, 0,
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

static SheetObjectClass *gnm_soi_parent_class;

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
gnm_soi_finalize (GObject *object)
{
	SheetObjectImage *soi;

	soi = SHEET_OBJECT_IMAGE (object);
	g_free (soi->bytes.data);
	g_free (soi->type);
	soi->bytes.data = NULL;

	G_OBJECT_CLASS (gnm_soi_parent_class)->finalize (object);
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
gnm_soi_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	FooCanvasItem *item = NULL;
	GdkPixbuf *pixbuf, *placeholder = NULL;

	pixbuf = soi_get_pixbuf (soi, 1.);

	if (pixbuf == NULL) {
		placeholder = gtk_icon_theme_load_icon (
			gtk_icon_theme_get_default (),
			"unknown_image", 100, 0, NULL);
		pixbuf = gdk_pixbuf_copy (placeholder);
	}

	item = foo_canvas_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_image_foo_view_get_type (),
		"pixbuf", pixbuf,
		"visible", FALSE,
		NULL);
	g_object_unref (G_OBJECT (pixbuf));

	if (placeholder)
		g_object_set_data (G_OBJECT (item), "tile", placeholder);

	return gnm_pane_object_register (so, item, TRUE);
}

static gboolean
soi_gdk_pixbuf_save (gchar const *buf,
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

static GOImageFormat const standard_formats[] = {
	GO_IMAGE_FORMAT_PNG,
	GO_IMAGE_FORMAT_JPG,
	GO_IMAGE_FORMAT_SVG,
	GO_IMAGE_FORMAT_EMF,
	GO_IMAGE_FORMAT_WMF
};

static GtkTargetList *
gnm_soi_get_target_list (SheetObject const *so)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	char *mime_str = go_image_format_to_mime (soi->type);
	GSList *mimes, *ptr;
	char *mime;
	GdkPixbuf *pixbuf;

	mimes = go_strsplit_to_slist (mime_str, ',');
	for (ptr = mimes; ptr != NULL; ptr = ptr->next) {
		mime = (char *) ptr->data;

		if (mime != NULL && *mime != '\0')
			gtk_target_list_add (tl, gdk_atom_intern (mime, FALSE),
					     0, 0);
	}
	g_free (mime_str);
	go_slist_free_custom (mimes, g_free);
	/* No need to eliminate duplicates. */
	if ((pixbuf = soi_get_pixbuf (soi, 1.0)) != NULL) {
		gtk_target_list_add_image_targets (tl, 0, TRUE);
		g_object_unref (pixbuf);
	}

	return tl;
}

static void
gnm_soi_write_image (SheetObject const *so, char const *format, double resolution,
		     GsfOutput *output, GError **err)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	gboolean res = FALSE;
	GdkPixbuf *pixbuf = soi_get_pixbuf (soi, 1.0);

	if (strcmp (format, soi->type) == 0)
		res = gsf_output_write (output,
					soi->bytes.len, soi->bytes.data);
	else if (pixbuf)
		res = gdk_pixbuf_save_to_callback (pixbuf,
						   soi_gdk_pixbuf_save, output,
						   format,
						   err, NULL);
	if (pixbuf)
		g_object_unref (pixbuf);
	if (!res && err && *err == NULL)
		*err = g_error_new (gsf_output_error_id (), 0,
				   _("Unknown failure while saving image"));
}

static void
soi_cb_save_as (SheetObject *so, SheetControl *sc)
{
	WBCGtk *wbcg;
	char *uri;
	GsfOutput *output;
	GSList *l = NULL;
	GOImageFormat sel_fmt;
	GOImageFormatInfo const *format_info;
	GdkPixbuf *pixbuf = NULL;
	GError *err = NULL;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	g_return_if_fail (soi != NULL);

	sel_fmt  = go_image_get_format_from_name (soi->type);
	if ((pixbuf = soi_get_pixbuf (soi, 1.0)) != NULL)
		l = go_image_get_formats_with_pixbuf_saver ();
	/* Move original format first in menu */
	l = g_slist_remove (l, GUINT_TO_POINTER (sel_fmt));
	l = g_slist_prepend (l, GUINT_TO_POINTER (sel_fmt));

	wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));

	uri = go_gui_get_image_save_info (wbcg_toplevel (wbcg), l, &sel_fmt, NULL);
	if (!uri)
		goto out;

	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	format_info = go_image_get_format_info (sel_fmt);
	sheet_object_write_image (so, format_info->name, -1.0, output, &err);
	gsf_output_close (output);
	g_object_unref (output);

	if (err != NULL)
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

out:
	if (pixbuf)
		g_object_unref (pixbuf);
	g_free (uri);
	g_slist_free (l);
}

static void
gnm_soi_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const soi_action =
		{ GTK_STOCK_SAVE_AS, N_("_Save as image"), NULL, 0, soi_cb_save_as };
	gnm_soi_parent_class->populate_menu (so, actions);
	go_ptr_array_insert (actions, (gpointer) &soi_action, 1);
}

static gboolean
gnm_soi_read_xml_dom (SheetObject *so, char const *typename,
				 XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	xmlNodePtr child;
	xmlChar    *type, *content;

	child = e_xml_get_child_by_name (tree, "Content");
	type  = xmlGetProp (child, CC2XML ("image-type"));
	if (type == NULL)
		return FALSE;
	content = xmlNodeGetContent (child);
	if (content == NULL) {
		xmlFree (type);
		return FALSE;
	}
	soi->type       = g_strdup (CXML2C (type));
	soi->bytes.len  = gsf_base64_decode_simple (content, strlen (CXML2C (content)));
	soi->bytes.data = g_memdup (content, soi->bytes.len);
	xmlFree (type);
	xmlFree (content);

	return FALSE;
}

static void
content_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	char const *image_type = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "image-type"))
			image_type = CXML2C (attrs[1]);

	soi->type = g_strdup (image_type);
}
static void
content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	soi->bytes.len  = gsf_base64_decode_simple (
		xin->content->str, xin->content->len);
	soi->bytes.data = g_memdup (xin->content->str, soi->bytes.len);
}

static void
gnm_soi_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (CONTENT, CONTENT, -1, "Content",	GSF_XML_CONTENT, &content_start, &content_end),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	if (NULL == doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "crop-top", &soi->crop_top)) ;
		else if (gnm_xml_attr_double (attrs, "crop-bottom", &soi->crop_bottom)) ;
		else if (gnm_xml_attr_double (attrs, "crop-left", &soi->crop_left)) ;
		else if (gnm_xml_attr_double (attrs, "crop-right", &soi->crop_right)) ;
	}
}

static void
gnm_soi_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
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
gnm_soi_copy (SheetObject *dst, SheetObject const *src)
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
gnm_soi_draw_cairo (SheetObject const *so, cairo_t *cr,
		    double width, double height)
{
	GdkPixbuf *pixbuf;
	GOImage *img;
	cairo_pattern_t *cr_pattern;
	int w, h;
	cairo_matrix_t cr_matrix;

	pixbuf = soi_get_pixbuf (SHEET_OBJECT_IMAGE (so), 1.);
	if (!pixbuf)
		return;
	img = go_image_new_from_pixbuf (pixbuf);
	cr_pattern = go_image_create_cairo_pattern (img);

	w = gdk_pixbuf_get_width  (pixbuf);
	h = gdk_pixbuf_get_height (pixbuf);
	cairo_matrix_init_scale (&cr_matrix,
				 w / width,
				 h / height);
	cairo_pattern_set_matrix (cr_pattern, &cr_matrix);
	cairo_rectangle (cr, 0., 0., width, height);
	cairo_set_source (cr, cr_pattern);
	cairo_fill (cr);
	cairo_pattern_destroy (cr_pattern);
	g_object_unref (img);
	g_object_unref (pixbuf);
}

static void
gnm_soi_default_size (SheetObject const *so, double *w, double *h)
{
	GdkPixbuf *buf = soi_get_pixbuf (SHEET_OBJECT_IMAGE (so), 1.);

	if (!buf) {
		*w = *h = 5;
		return;
	}

	*w = gdk_pixbuf_get_width  (buf);
	*h = gdk_pixbuf_get_height (buf);

	/* In case buf is invalid with size 0,0 or if the image is just too
	 * small to be useful default to something slightly larger
	 * http://bugzilla.gnome.org/show_bug.cgi?id=462787
	 **/
	if ((*w * *h) < 25.) {
		if (*w < 5) *w = 25;
		if (*h < 5) *h = 25;
	}
	g_object_unref (buf);
}

static void
gnm_soi_get_property (GObject     *object,
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
gnm_soi_class_init (GObjectClass *object_class)
{
	SheetObjectClass *so_class;

	gnm_soi_parent_class = g_type_class_peek_parent (object_class);

	/* Object class method overrides */
	object_class->finalize	   = gnm_soi_finalize;
	object_class->get_property = gnm_soi_get_property;

	/* SheetObject class method overrides */
	so_class = SHEET_OBJECT_CLASS (object_class);
	so_class->new_view		= gnm_soi_new_view;
	so_class->populate_menu		= gnm_soi_populate_menu;
	so_class->read_xml_dom		= gnm_soi_read_xml_dom;
	so_class->write_xml_sax		= gnm_soi_write_xml_sax;
	so_class->prep_sax_parser	= gnm_soi_prep_sax_parser;
	so_class->copy			= gnm_soi_copy;
	so_class->draw_cairo		= gnm_soi_draw_cairo;
	so_class->user_config		= NULL;
	so_class->default_size		= gnm_soi_default_size;
	so_class->rubber_band_directly	= TRUE;

	/* The property strings don't need translation */
	g_object_class_install_property (object_class, PROP_IMAGE_TYPE,
		 g_param_spec_string ("image-type", "Image type",
				      "Type of image",
				      NULL,
				      GSF_PARAM_STATIC | G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_IMAGE_DATA,
		 g_param_spec_pointer ("image-data", "Image data",
				       "Image data",
				       GSF_PARAM_STATIC | G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_PIXBUF,
		 g_param_spec_object ("pixbuf", "Pixbuf",
				       "Pixbuf",
				       GDK_TYPE_PIXBUF,
				       GSF_PARAM_STATIC | G_PARAM_READABLE));
}

static void
gnm_soi_init (GObject *obj)
{
	SheetObjectImage *soi;
	SheetObject *so;

	soi = SHEET_OBJECT_IMAGE (obj);
	soi->dumped = FALSE;
	soi->crop_top = soi->crop_bottom = soi->crop_left = soi->crop_right
		= 0.0;

	so = SHEET_OBJECT (obj);
	so->anchor.base.direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
}

static void
soi_imageable_init (SheetObjectImageableIface *soi_iface)
{
	soi_iface->get_target_list = gnm_soi_get_target_list;
	soi_iface->write_image	   = gnm_soi_write_image;
}

GSF_CLASS_FULL (SheetObjectImage, sheet_object_image,
	   NULL, NULL, gnm_soi_class_init, NULL,
	   gnm_soi_init, SHEET_OBJECT_TYPE, 0,
	   GSF_INTERFACE (soi_imageable_init, SHEET_OBJECT_IMAGEABLE_TYPE));
