/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998-2002 Miguel de Icaza
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "style.h"

#include "format.h"
#include "style-color.h"
#include "application.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"

#include "gui-util.h"
#include "mathfunc.h"

#include <string.h>

#undef DEBUG_REF_COUNT
#undef DEBUG_FONTS

static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;

double gnumeric_default_font_width;
static char *gnumeric_default_font_name;
static double gnumeric_default_font_size;

/**
 * get_substitute_font
 * @fontname    The font name
 *
 * Tries to find a gnome font which matches the Excel font.
 * Returns the name of the substitute font if found. Otherwise returns NULL
 */
/* This is very ad hoc - throw it away when something better comes along */
static gchar const *
get_substitute_font (gchar const *fontname)
{
	int i;

	static char const *map[][2] = {
		{ "Times New Roman", "Times"},
		{ "Arial",           "Helvetica"},
		{ "Courier New",     "Courier"},
		{ "£Í£Ó £Ð¥´¥·¥Ã¥¯", "Kochi Gothic"},
		{ "£Í£Ó ¥´¥·¥Ã¥¯",   "Kochi Gothic"},
		{ "¥´¥·¥Ã¥¯",        "Kochi Gothic"},
		{ "MS UI Gothic",    "Kochi Gothic"},
		{ "£Í£Ó £ÐÌÀÄ«",     "Kochi Mincho"},
		{ "£Í£Ó ÌÀÄ«",       "Kochi Mincho"},
		{ "ÌÀÄ«",            "Kochi Mincho"},
		{ NULL }
	};
	for (i = 0; map[i][0]; i++)
		if (strcmp (map[i][0], fontname) == 0)
			return map[i][1];

	return NULL;
}

static int
style_font_string_width (StyleFont const *font, char const *str)
{
	int w;
	pango_layout_set_text (font->pango.layout, str, -1);
	pango_layout_get_pixel_size (font->pango.layout, &w, NULL);
	return w;
}

static double
calc_font_width (StyleFont const *font, char const *teststr)
{
	char const *p1, *p2;
	int w = 0, w1, w2, dw;
	char buf[3];

	for (p1 = teststr; *p1; p1++) {
		buf[0] = *p1;
		buf[1] = 0;
		w1 = style_font_string_width (font, buf);
		for (p2 = teststr; *p2; p2++) {
			buf[1] = *p2;
			buf[2] = 0;
			w2 = style_font_string_width (font, buf);
			dw = w2 - w1;
			if (dw > w) {
				w = dw;
#ifdef DEBUG_FONT_WIDTH
				fprintf (stderr, "   %s = %d", buf, w);
#endif
			}
		}
	}

	return w;
}


StyleFont *
style_font_new_simple (PangoContext *context,
		       char const *font_name, double size_pts, double scale,
		       gboolean bold, gboolean italic)
{
	StyleFont *font;
	StyleFont key;

	if (font_name == NULL) {
		g_warning ("font_name == NULL, using %s", DEFAULT_FONT);
		font_name = DEFAULT_FONT;
	}
	if (size_pts <= 0) {
		g_warning ("font_size <= 0, using %f", DEFAULT_SIZE);
		size_pts = DEFAULT_SIZE;
	}

	/* This cast does not mean we will change the name.  */
	key.font_name = (char *)font_name;
	key.size_pts  = size_pts;
	key.is_bold   = bold;
	key.is_italic = italic;
	key.scale     = scale;

	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (font == NULL) {
		PangoFontDescription *desc;
		double pts_scale;

		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->size_pts  = size_pts;
		font->scale     = scale;
		font->is_bold   = bold;
		font->is_italic = italic;
		/* One reference for the cache, one for the caller. */
		font->ref_count = 2;

		g_object_ref (context);
		font->pango.context = context;
		desc = pango_context_get_font_description (context);
		pango_font_description_set_family (desc, font_name);
		pango_font_description_set_weight (desc,
			bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
		pango_font_description_set_style (desc,
			italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		/* FIXME: set scale separately?  */
		pango_font_description_set_size (desc,
						 size_pts * scale *
						 PANGO_SCALE);

		font->pango.font = pango_context_load_font (context, desc);
		if (font->pango.font == NULL) {
			/* if we fail, try to be smart and map to something similar */
			char const *sub = get_substitute_font (font_name);
			if (sub != NULL) {
				pango_font_description_set_family (desc, font_name);
				font->pango.font = pango_context_load_font (context,
									    desc);
			}

			if (font->pango.font == NULL) {
				g_object_unref (context);
				font->pango.context = NULL;
				font->pango.font_descr = NULL;
				g_hash_table_insert (style_font_negative_hash,
						     font, font);
				return NULL;
			}
		}

		font->pango.font_descr = pango_font_describe (font->pango.font);

		font->pango.layout  = pango_layout_new (context);
		pango_layout_set_font_description (font->pango.layout,
						   font->pango.font_descr);

		font->pango.metrics = pango_font_get_metrics (font->pango.font,
							      gtk_get_default_language ());

		font->gnome_print_font = gnome_font_find_closest_from_weight_slant (font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_REGULAR, italic, size_pts);

		font->approx_width.pixels.digit = calc_font_width (font, "0123456789");
		font->approx_width.pixels.decimal = calc_font_width (font, ".,");
		font->approx_width.pixels.hash = calc_font_width (font, "#");
		font->approx_width.pixels.sign = calc_font_width (font, "-+");
		font->approx_width.pixels.E = calc_font_width (font, "E");
		font->approx_width.pixels.e = calc_font_width (font, "e");

		pts_scale = application_display_dpi_get (TRUE) / 72.0;
		font->approx_width.pts.digit =
			font->approx_width.pixels.digit / pts_scale;
		font->approx_width.pts.decimal =
			font->approx_width.pixels.decimal / pts_scale;
		font->approx_width.pts.sign =
			font->approx_width.pixels.sign / pts_scale;
		font->approx_width.pts.E =
			font->approx_width.pixels.E / pts_scale;
		font->approx_width.pts.e =
			font->approx_width.pixels.e / pts_scale;

		g_hash_table_insert (style_font_hash, font, font);
	} else
		font->ref_count++;

#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 font, font->font_name,
		 font->is_bold ? " bold" : "",
		 font->is_italic ? " italic" : "",
		 font->ref_count);
#endif
	return font;
}

StyleFont *
style_font_new (PangoContext *context,
		char const *font_name, double size_pts, double scale,
		gboolean bold, gboolean italic)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size_pts > 0, NULL);

	font = style_font_new_simple (context, font_name, size_pts,
				      scale, bold, italic);
	if (font) return font;

	font_name = gnumeric_default_font_name;
	font = style_font_new_simple (context, font_name, size_pts,
				      scale, bold, italic);
	if (font) return font;

	size_pts = gnumeric_default_font_size;
	font = style_font_new_simple (context, font_name, size_pts,
				      scale, bold, italic);
	if (font) return font;

	bold = FALSE;
	font = style_font_new_simple (context, font_name, size_pts,
				      scale, bold, italic);
	if (font) return font;

	italic = FALSE;
	font = style_font_new_simple (context, font_name, size_pts,
				      scale, bold, italic);
	if (font) return font;

	/*
	 * This should not be possible to reach as we have reverted all the way
	 * back to the default font.
	 */
	g_assert_not_reached ();
	abort ();
}

void
style_font_ref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
}

void
style_font_unref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
	if (sf->ref_count != 0)
		return;

	if (sf->pango.context != NULL) {
		g_object_unref (G_OBJECT (sf->pango.context));
		sf->pango.context = NULL;
	}
	if (sf->pango.layout != NULL) {
		g_object_unref (G_OBJECT (sf->pango.layout));
		sf->pango.layout = NULL;
	}
	if (sf->pango.font != NULL) {
		g_object_unref (G_OBJECT (sf->pango.font));
		sf->pango.font = NULL;
	}
	if (sf->pango.font_descr != NULL) {
		pango_font_description_free (sf->pango.font_descr);
		sf->pango.font_descr = NULL;
	}
	if (sf->pango.metrics != NULL) {
		pango_font_metrics_unref (sf->pango.metrics);
		sf->pango.metrics = NULL;
	}
	if (sf->gnome_print_font != NULL) {
		gnome_font_unref (sf->gnome_print_font);
		sf->gnome_print_font = NULL;
	}
	g_hash_table_remove (style_font_hash, sf);
	g_free (sf->font_name);
	g_free (sf);
}

/*
 * The routines used to hash and compare the different styles
 */
gint
style_font_equal (gconstpointer v, gconstpointer v2)
{
	StyleFont const *k1 = (StyleFont const *) v;
	StyleFont const *k2 = (StyleFont const *) v2;

	if (k1->size_pts != k2->size_pts)
		return 0;

	if (k1->is_bold != k2->is_bold)
		return 0;
	if (k1->is_italic != k2->is_italic)
		return 0;
	if (k1->scale != k2->scale)
		return 0;

	return !strcmp (k1->font_name, k2->font_name);
}

guint
style_font_hash_func (gconstpointer v)
{
	StyleFont const *k = (StyleFont const *) v;

	return k->size_pts + g_str_hash (k->font_name);
}

static void
font_init (void)
{
	GConfClient *client = application_get_gconf_client ();
	GdkScreen *screen;
	PangoContext *context;
	StyleFont *gnumeric_default_font = NULL;

	gnumeric_default_font_name =
		gconf_client_get_string (client,
					 GCONF_DEFAULT_FONT,
					 NULL);
	gnumeric_default_font_size =
		gconf_client_get_float (client,
					GCONF_DEFAULT_SIZE,
					NULL);

	screen = gdk_screen_get_default ();
	context = gdk_pango_context_get_for_screen (screen);
	pango_context_set_language (context, gtk_get_default_language ());
	gdk_pango_context_set_colormap (context,
					gdk_screen_get_default_colormap (screen));

	if (gnumeric_default_font_name && gnumeric_default_font_size >= 1)
		gnumeric_default_font =
			style_font_new_simple (context,
					       gnumeric_default_font_name,
					       gnumeric_default_font_size,
					       1., FALSE, FALSE);
	if (!gnumeric_default_font) {
		g_warning ("Configured default font not available, trying fallback...");
		gnumeric_default_font =
			style_font_new_simple (context,
					       DEFAULT_FONT, DEFAULT_SIZE,
					       1., FALSE, FALSE);
		g_free (gnumeric_default_font_name);
		gnumeric_default_font_name = g_strdup (DEFAULT_FONT);
		gnumeric_default_font_size = DEFAULT_SIZE;
	}

	g_object_unref (context);

	if (!gnumeric_default_font)
		exit (1);

	gnumeric_default_font_width = gnumeric_default_font->approx_width.pts.digit;
	style_font_unref (gnumeric_default_font);
}

void
style_init (void)
{
	number_format_init ();
	style_font_hash = g_hash_table_new (style_font_hash_func,
					    style_font_equal);
	style_font_negative_hash = g_hash_table_new (style_font_hash_func,
						     style_font_equal);

	font_init ();
}

static void
delete_neg_font (gpointer key, gpointer value, gpointer user_data)
{
	StyleFont *sf = key;

	g_free (sf->font_name);
	g_free (sf);
}

static void
list_cached_fonts (gpointer key, gpointer value, gpointer user_data)
{
	StyleFont *font = key;
	GSList **lp = (GSList **)user_data;

	*lp = g_slist_prepend (*lp, font);
}

/*
 * Release all resources allocated by style_init.
 */
void
style_shutdown (void)
{
	g_free (gnumeric_default_font_name);
	gnumeric_default_font_name = NULL;

	number_format_shutdown ();
	{
		/* Make a list of the fonts, then unref them.  */
		GSList *fonts = NULL, *tmp;
		g_hash_table_foreach (style_font_hash, list_cached_fonts, &fonts);
		for (tmp = fonts; tmp; tmp = tmp->next) {
			StyleFont *sf = tmp->data;
			if (sf->ref_count != 1)
				g_warning ("Font %s has %d references instead of the expected single.",
					   sf->font_name, sf->ref_count);
			style_font_unref (sf);
		}
		g_slist_free (fonts);
	}
	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;

	g_hash_table_foreach (style_font_negative_hash, delete_neg_font, NULL);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;
}

/**
 * required_updates_for_style
 * @style: the style
 *
 * What changes are required after applying the supplied style.
 */
SpanCalcFlags
required_updates_for_style (MStyle *style)
{
	gboolean const size_change =
	    (mstyle_is_element_set  (style, MSTYLE_FONT_NAME) ||
	     mstyle_is_element_set  (style, MSTYLE_FONT_BOLD) ||
	     mstyle_is_element_set  (style, MSTYLE_FONT_ITALIC) ||
	     mstyle_is_element_set  (style, MSTYLE_FONT_SIZE) ||
	     mstyle_is_element_set  (style, MSTYLE_WRAP_TEXT));
	gboolean const format_change =
	    (mstyle_is_element_set (style, MSTYLE_FORMAT) ||
	     mstyle_is_element_set (style, MSTYLE_INDENT));

	return format_change
	    ? SPANCALC_RE_RENDER|SPANCALC_RESIZE
	    : size_change ? SPANCALC_RESIZE
			  : SPANCALC_SIMPLE;
}

/**
 * style_default_halign :
 * @style :
 * @cell  :
 *
 * Select the appropriate horizontal alignment depending on the style and cell
 * value.
 */
StyleHAlignFlags
style_default_halign (MStyle const *mstyle, Cell const *c)
{
	StyleHAlignFlags align = mstyle_get_align_h (mstyle);

	if (align == HALIGN_GENERAL) {
		Value *v;

		g_return_val_if_fail (c != NULL, HALIGN_RIGHT);

		if (c->base.sheet && c->base.sheet->display_formulas &&
		    cell_has_expr (c))
			return HALIGN_LEFT;

		for (v = c->value; v != NULL ; )
			switch (v->type) {
			case VALUE_BOOLEAN :
				return HALIGN_CENTER;

			case VALUE_INTEGER :
			case VALUE_FLOAT :
				return HALIGN_RIGHT;

			case VALUE_ARRAY :
				/* Tail recurse into the array */
				if (v->v_array.x > 0 && v->v_array.y > 0) {
					v = v->v_array.vals [0][0];
					break;
				}

			default :
				return HALIGN_LEFT;
			}
	}

	return align;
}
