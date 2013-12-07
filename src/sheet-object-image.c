/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-image.c: a wrapper for gdkpixbuf to display images.
 *
 * Author:
 *	Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnm-i18n.h"
#include "gnumeric.h"
#include "sheet-object-image.h"
#include "sheet.h"

#include "gnm-pane.h"
#include "wbc-gtk.h"
#include "sheet-object-impl.h"
#include "sheet-control-gui.h"
#include "gui-file.h"
#include "application.h"
#include "xml-sax.h"

#include <goffice/goffice.h>
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
so_image_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *view = GOC_ITEM (GOC_GROUP (sov)->children->data);
	double scale = goc_canvas_get_pixels_per_unit (view->canvas);

	if (visible) {
		double x, y, width, height;
		double old_x1, old_y1, old_x2, old_y2, old_width, old_height;
		GdkPixbuf *placeholder = g_object_get_data (G_OBJECT (view), "tile");

		x = MIN (coords [0], coords [2]) / scale;
		y = MIN (coords [1], coords [3]) / scale;
		width  = fabs (coords [2] - coords [0]) / scale;
		height = fabs (coords [3] - coords [1]) / scale;

		goc_item_get_bounds (view, &old_x1, &old_y1, &old_x2, &old_y2);
		goc_item_set (view,
			"x", x,			"y", y,
			"width",  width,	"height", height,
			NULL);

		/* regenerate the image if necessary */
		old_width = fabs (old_x1 - old_x2);
		old_height = fabs (old_y1 - old_y2);
		if (placeholder != NULL &&
		    (fabs (width - old_width) > 0.5 || fabs (height - old_height) > 0.5)) {
			GdkPixbuf *newimage = go_gdk_pixbuf_tile (placeholder,
				(guint)width, (guint)height);
			goc_item_set (view, "pixbuf", newimage, NULL);
			g_object_unref (newimage);
		}

		goc_item_show (view);
	} else
		goc_item_hide (view);
}

static void
so_image_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_image_view_set_bounds;
}
typedef SheetObjectView	SOImageGocView;
typedef SheetObjectViewClass	SOImageGocViewClass;
static GSF_CLASS (SOImageGocView, so_image_goc_view,
	so_image_goc_view_class_init, NULL,
	SHEET_OBJECT_VIEW_TYPE)

/****************************************************************************/
struct _SheetObjectImage {
	SheetObject  sheet_object;

	GOImage      *image;
	char         *type;
	char	     *name;
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
 * @soi: #SheetObjectImage
 * @type:
 * @data:
 * @data_len
 * @copy_data:
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

	soi->type       = (type && *type)? g_strdup (type): NULL;
	soi->bytes.len  = data_len;
	soi->bytes.data = copy_data ? g_memdup (data, data_len) : data;
	soi->image = go_image_new_from_data (soi->type, soi->bytes.data, soi->bytes.len,
	                                     ((soi->type == NULL)? &soi->type: NULL), NULL);
	if (soi->sheet_object.sheet != NULL) {
		GOImage *image = go_doc_add_image (GO_DOC (soi->sheet_object.sheet->workbook), NULL, soi->image);
		if (image != soi->image) {
			g_object_unref (soi->image);
			soi->image = g_object_ref (image);
		}
	}
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
	g_free (soi->name);
	soi->bytes.data = NULL;
	if (soi->image)
		g_object_unref (soi->image);

	G_OBJECT_CLASS (gnm_soi_parent_class)->finalize (object);
}

static SheetObjectView *
gnm_soi_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GocItem *item = NULL;

	item = goc_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_image_goc_view_get_type (),
		NULL);
	if (soi->image) {
		goc_item_hide (goc_item_new (GOC_GROUP (item),
			GOC_TYPE_IMAGE,
			"image", soi->image,
		        "crop-bottom", soi->crop_bottom,
		        "crop-left", soi->crop_left,
		        "crop-right", soi->crop_right,
		        "crop-top", soi->crop_top,
			NULL));

	} else {
		GdkPixbuf *placeholder = go_gdk_pixbuf_load_from_file
			("res:gnm:pixmaps/unknown_image.png");
		GdkPixbuf *pixbuf = gdk_pixbuf_copy (placeholder);

		goc_item_hide (goc_item_new (GOC_GROUP (item),
			GOC_TYPE_PIXBUF,
			"pixbuf", pixbuf,
			NULL));
		g_object_unref (pixbuf);
		g_object_set_data (G_OBJECT (item), "tile", placeholder);
	}

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
	GO_IMAGE_FORMAT_WMF,
	GO_IMAGE_FORMAT_EPS
};

static GtkTargetList *
gnm_soi_get_target_list (SheetObject const *so)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	char *mime_str;
	GSList *mimes, *ptr;
	GdkPixbuf *pixbuf = NULL;

	if (soi->type == NULL && soi->image != NULL)
		pixbuf = go_image_get_pixbuf (soi->image);
	mime_str = go_image_format_to_mime (soi->type);
	if (mime_str) {
		mimes = go_strsplit_to_slist (mime_str, ',');
		for (ptr = mimes; ptr != NULL; ptr = ptr->next) {
			const char *mime = ptr->data;

			if (mime != NULL && *mime != '\0')
				gtk_target_list_add (tl, gdk_atom_intern (mime, FALSE),
						     0, 0);
		}
		g_free (mime_str);
		g_slist_free_full (mimes, g_free);
	}
	/* No need to eliminate duplicates. */
	if (soi->image != NULL || pixbuf != NULL) {
		gtk_target_list_add_image_targets (tl, 0, TRUE);
		if (pixbuf != NULL)
			g_object_unref (pixbuf);
	}

	return tl;
}

static void
gnm_soi_write_image (SheetObject const *so, char const *format, 
		     G_GNUC_UNUSED double resolution,
		     GsfOutput *output, GError **err)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	gboolean res = FALSE;
	GdkPixbuf *pixbuf = go_image_get_pixbuf (soi->image);

	if (!soi->type || strcmp (format, soi->type) == 0) {
		if (soi->bytes.len == 0) {
			gsize length;
			guint8 const *data = go_image_get_data (soi->image, &length);
			res = gsf_output_write (output,	length, data);
		} else
			res = gsf_output_write (output,
						soi->bytes.len, soi->bytes.data);
	} else if (pixbuf)
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
	if ((pixbuf = go_image_get_pixbuf (soi->image)) != NULL)
		l = go_image_get_formats_with_pixbuf_saver ();
	/* Move original format first in menu */
	if (sel_fmt != GO_IMAGE_FORMAT_UNKNOWN) {
		l = g_slist_remove (l, GUINT_TO_POINTER (sel_fmt));
		l = g_slist_prepend (l, GUINT_TO_POINTER (sel_fmt));
	}

	wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));

	uri = go_gui_get_image_save_info (wbcg_toplevel (wbcg), l, &sel_fmt, NULL);
	if (!uri)
		goto out;

	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	format_info = go_image_get_format_info (sel_fmt);
	sheet_object_write_image (so, (format_info? format_info->name: NULL), -1.0, output, &err);
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

static void
content_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	char const *image_type = NULL, *image_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "image-type"))
			image_type = CXML2C (attrs[1]);
	else if (attr_eq (attrs[0], "name"))
			image_name = CXML2C (attrs[1]);

	soi->type = g_strdup (image_type);
	if (image_name)
		soi->name = g_strdup (image_name);
}
static void
content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	SheetObject *so = gnm_xml_in_cur_obj (xin);
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	GString *data = xin->content;

	if (data->len >= 4) {
		size_t len = gsf_base64_decode_simple (data->str, data->len);
		soi->bytes.len = len;
		soi->bytes.data = g_memdup (data->str, len);
		soi->image = go_image_new_from_data (soi->type,
						     soi->bytes.data,
						     len, NULL, NULL);
	}
}

static void
gnm_soi_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
			 xmlChar const **attrs,
			 G_GNUC_UNUSED GnmConventions const *convs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (CONTENT, CONTENT, -1, "Content", GSF_XML_CONTENT, &content_start, &content_end),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	if (NULL == doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_double (attrs, "crop-top", &soi->crop_top)) ;
		else if (gnm_xml_attr_double (attrs, "crop-bottom", &soi->crop_bottom))
			/* Nothing */ ;
		else if (gnm_xml_attr_double (attrs, "crop-left", &soi->crop_left))
			/* Nothing */ ;
		else if (gnm_xml_attr_double (attrs, "crop-right", &soi->crop_right))
			/* Nothing */ ;
	}
}

static void
gnm_soi_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
		       G_GNUC_UNUSED GnmConventions const *convs)
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
	if (soi->image && go_image_get_name (soi->image)) {
		gsf_xml_out_add_cstr (output, "name", go_image_get_name (soi->image));
		if (sheet_object_get_sheet (so))
			go_doc_save_image (GO_DOC (sheet_object_get_sheet (so)->workbook), go_image_get_name (soi->image));
		else {
			/* looks that this may happen when pasting from another process, see #687414 */
			gsize length;
			guint8 const *data = go_image_get_data (soi->image, &length);
			gsf_xml_out_add_uint (output, "size-bytes", length);
			gsf_xml_out_add_base64 (output, NULL, data, length);
		}
	} else {
		gsf_xml_out_add_uint (output, "size-bytes", soi->bytes.len);
		gsf_xml_out_add_base64 (output, NULL, soi->bytes.data, soi->bytes.len);
	}
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
	new_soi->image		= g_object_ref (soi->image);
}

static void
gnm_soi_draw_cairo (SheetObject const *so, cairo_t *cr,
		    double width, double height)
{
	GdkPixbuf *pixbuf;
	GOImage *img;
	int w, h;
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	if (soi->image) {
		w = go_image_get_width (soi->image);
		h = go_image_get_height (soi->image);
		w -= soi->crop_left - soi->crop_right;
		h -= soi->crop_top - soi->crop_bottom;
		if (w <= 0 || h <= 0)
			return;
		cairo_save (cr);
		cairo_rectangle (cr, 0, 0, width, height);
		cairo_clip (cr);
		cairo_scale (cr, width / w, height / h);
		cairo_translate (cr, -soi->crop_left, -soi->crop_top);
		go_image_draw (soi->image, cr);
		cairo_restore (cr);
	} else {
		pixbuf = go_image_get_pixbuf (soi->image);
		if (!pixbuf || width == 0. || height == 0.)
			return;
		cairo_save (cr);
		img = go_pixbuf_new_from_pixbuf (pixbuf);

		w = gdk_pixbuf_get_width  (pixbuf);
		h = gdk_pixbuf_get_height (pixbuf);
		cairo_scale (cr, width / w, height / h);
		go_image_draw (img, cr);
		/*
		 * We need to unset the source before we destroy the pattern.
		 * cairo_restore will do that.  See #632439.
		 */
		cairo_restore (cr);
		g_object_unref (img);
		g_object_unref (pixbuf);
	}
}

static void
gnm_soi_default_size (SheetObject const *so, double *w, double *h)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);
	if (soi->image) {
		*w = go_image_get_width (soi->image);
		*h = go_image_get_height (soi->image);
	} else {
		GdkPixbuf *buf = go_image_get_pixbuf (soi->image);

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
}

static gboolean
gnm_soi_assign_to_sheet (SheetObject *so, Sheet *sheet)
{
	SheetObjectImage *soi = SHEET_OBJECT_IMAGE (so);

	if (soi->image/* && !go_image_get_name (soi->image)*/) {
		GODoc *doc = GO_DOC (sheet->workbook);
		GOImage *image = go_doc_add_image (doc, soi->name, soi->image);
		if (soi->image != image) {
			g_object_unref (soi->image);
			soi->image = g_object_ref (image);
		}
	} else if (soi->name) {
		GODoc *doc = GO_DOC (sheet->workbook);
		GType type = go_image_type_for_format (soi->type);
		if (type != 0)
			soi->image = g_object_ref (go_doc_image_fetch (doc, soi->name, type));
	} else {
		/* There is nothing we can do */
	}
	return FALSE;
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
		pixbuf = go_image_get_pixbuf (soi->image);
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
	so_class->write_xml_sax		= gnm_soi_write_xml_sax;
	so_class->prep_sax_parser	= gnm_soi_prep_sax_parser;
	so_class->copy			= gnm_soi_copy;
	so_class->draw_cairo		= gnm_soi_draw_cairo;
	so_class->user_config		= NULL;
	so_class->default_size		= gnm_soi_default_size;
	so_class->assign_to_sheet       = gnm_soi_assign_to_sheet;
	so_class->rubber_band_directly	= TRUE;

	/* The property strings don't need translation */
	g_object_class_install_property (object_class, PROP_IMAGE_TYPE,
		 g_param_spec_string ("image-type",
				      P_("Image type"),
				      P_("Type of image"),
				      NULL,
				      GSF_PARAM_STATIC | G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_IMAGE_DATA,
		 g_param_spec_pointer ("image-data",
				       P_("Image data"),
				       P_("Image data"),
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
	   GSF_INTERFACE (soi_imageable_init, SHEET_OBJECT_IMAGEABLE_TYPE))
