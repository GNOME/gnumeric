/*
 * style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998-2004 Miguel de Icaza
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <style.h>
#include <style-font.h>

#include <gnm-format.h>
#include <style-color.h>
#include <application.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>

#include <gui-util.h>
#include <mathfunc.h>
#include <gnumeric-conf.h>

#include <pango/pangocairo.h>
#include <gdk/gdk.h>
#include <string.h>
#include <goffice/goffice.h>

#undef DEBUG_REF_COUNT
#undef DEBUG_FONTS

static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;

double gnm_font_default_width;
static char *gnumeric_default_font_name;
static double gnumeric_default_font_size;

/* This is very ad hoc - throw it away when something better comes along */
/* See also wine/dlls/winex11.drv/xfont.c */

static struct FontInfo {
	const char *font_name;
	const char *font_substitute_name;
	int override_codepage;
} font_info[] = {
        { "Times New Roman",        "Times",          -1 },
        { "Times New Roman CYR",    "Times",          1251 },
        { "Times New Roman Greek",  "Times",          1253 },
        { "Times New Roman Tur",    "Times",          1254 },
        { "Times New Roman Baltic", "Times",          1257 },
        { "Tms Rmn",                "Times",          -1 },
        { "Arial",                  "Sans",           -1 },
        { "Arial CYR",              "Sans",           1251 },
        { "Arial Greek",            "Sans",           1253 },
        { "Arial Tur",              "Sans",           1254 },
        { "Arial Baltic",           "Sans",           1257 },
        { "Albany",                 "Sans",           -1 },
        { "Helvetica",              "Sans",           -1 },
        { "Courier New",            "Courier",        -1 },
        { "Courier New CYR",        "Courier",        1251 },
        { "Courier New Greek",      "Courier",        1253 },
        { "Courier New Tur",        "Courier",        1254 },
        { "Courier New Baltic",     "Courier",        1257 },
        { "£Í£Ó £Ð¥´¥·¥Ã¥¯",        "Kochi Gothic",   -1 },
        { "£Í£Ó ¥´¥·¥Ã¥¯",          "Kochi Gothic",   -1 },
        { "¥´¥·¥Ã¥¯",               "Kochi Gothic",   -1 },
        { "MS UI Gothic",           "Kochi Gothic",   -1 },
        { "£Í£Ó £ÐÌÀÄ«",            "Kochi Mincho",   -1 },
        { "£Í£Ó ÌÀÄ«",              "Kochi Mincho",   -1 },
        { "ÌÀÄ«",                   "Kochi Mincho",   -1 },
	{ "GulimChe",               NULL,             949 }
};

static struct FontInfo *
find_font (const char *font_name)
{
	unsigned ui;

	if (!font_name)
		return NULL;

	for (ui = 0; ui < G_N_ELEMENTS (font_info); ui++) {
		if (!g_ascii_strcasecmp (font_info[ui].font_name, font_name))
			return font_info + ui;
	}
	return NULL;
}

/**
 * gnm_font_override_codepage:
 * @font_name: The win32 font name
 *
 * Returns a codepage for the named Win32 font, or -1 if no such codepage
 * is known.
 */
int
gnm_font_override_codepage (gchar const *font_name)
{
	struct FontInfo *fi = find_font (font_name);
	return fi ? fi->override_codepage : -1;
}


/*
 * get_substitute_font:
 * @font_name    The font name
 *
 * Tries to find a gnome font which matches the Excel font.
 * Returns the name of the substitute font if found. Otherwise returns %NULL
 */
static gchar const *
get_substitute_font (gchar const *font_name)
{
	struct FontInfo *fi = find_font (font_name);
	return fi ? fi->font_substitute_name : NULL;
}

static GnmFont *
style_font_new_simple (PangoContext *context,
		       char const *font_name, double size_pts,
		       gboolean bold, gboolean italic)
{
	GnmFont *font;
	GnmFont key;

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
	key.context   = context;

	font = (GnmFont *) g_hash_table_lookup (style_font_hash, &key);
	if (font == NULL) {
		PangoFontDescription *desc;
		PangoFont *pango_font;

		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font = g_new0 (GnmFont, 1);
		font->font_name = g_strdup (font_name);
		font->size_pts  = size_pts;
		font->is_bold   = bold;
		font->is_italic = italic;
		font->context = g_object_ref (context);
		/* One reference for the cache, one for the caller. */
		font->ref_count = 2;

		desc = pango_font_description_new ();

		pango_font_description_set_family (desc, font_name);
		pango_font_description_set_weight (desc,
			bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
		pango_font_description_set_style (desc,
			italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		pango_font_description_set_size (desc, size_pts * PANGO_SCALE);

		pango_font = pango_context_load_font (context, desc);
		if (pango_font == NULL) {
			/* if we fail, try to be smart and map to something similar */
			char const *sub = get_substitute_font (font_name);
			if (sub != NULL) {
				pango_font_description_set_family (desc, font_name);
				pango_font = pango_context_load_font (context,
								      desc);
			}

			if (pango_font == NULL) {
				pango_font_description_free (desc);
				g_hash_table_insert (style_font_negative_hash,
						     font, font);
				return NULL;
			}
		}

		if (pango_font)
			g_object_unref (pango_font);

		font->go.font = go_font_new_by_desc (desc);
		font->go.metrics = go_font_metrics_new (context, font->go.font);
		g_hash_table_insert (style_font_hash, font, font);
	} else
		font->ref_count++;

#ifdef DEBUG_REF_COUNT
	g_message (__FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 font, font->font_name,
		 font->is_bold ? " bold" : "",
		 font->is_italic ? " italic" : "",
		 font->ref_count);
#endif
	return font;
}

GnmFont *
gnm_font_new (PangoContext *context,
	      char const *font_name, double size_pts,
	      gboolean bold, gboolean italic)
{
	GnmFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size_pts > 0, NULL);

	font = style_font_new_simple (context, font_name, size_pts,
				      bold, italic);
	if (font) return font;

	font_name = gnumeric_default_font_name;
	font = style_font_new_simple (context, font_name, size_pts,
				      bold, italic);
	if (font) return font;

	size_pts = gnumeric_default_font_size;
	font = style_font_new_simple (context, font_name, size_pts,
				      bold, italic);
	if (font) return font;

	bold = FALSE;
	font = style_font_new_simple (context, font_name, size_pts,
				      bold, italic);
	if (font) return font;

	italic = FALSE;
	font = style_font_new_simple (context, font_name, size_pts,
				      bold, italic);
	if (font) return font;

	/*
	 * This should not be possible to reach as we have reverted all the way
	 * back to the default font.
	 */
	g_assert_not_reached ();
	abort ();
}

GnmFont *
gnm_font_ref (GnmFont *sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	sf->ref_count++;
#ifdef DEBUG_REF_COUNT
	g_message (__FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif

	return sf;
}

void
gnm_font_unref (GnmFont *sf)
{
	g_return_if_fail (sf != NULL);
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
#ifdef DEBUG_REF_COUNT
	g_message (__FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
	if (sf->ref_count != 0)
		return;

	g_hash_table_remove (style_font_hash, sf);
	/* hash-changing operations after above line.  */

	if (sf->go.font) {
		go_font_unref (sf->go.font);
		sf->go.font = NULL;
	}

	if (sf->go.metrics) {
		go_font_metrics_free (sf->go.metrics);
		sf->go.metrics = NULL;
	}

	g_object_unref (sf->context);
	sf->context = NULL;

	g_free (sf->font_name);
	sf->font_name = NULL;

	g_free (sf);
}

GType
gnm_font_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFont",
			 (GBoxedCopyFunc)gnm_font_ref,
			 (GBoxedFreeFunc)gnm_font_unref);
	}
	return t;
}

gint
gnm_font_equal (gconstpointer v, gconstpointer v2)
{
	GnmFont const *k1 = (GnmFont const *) v;
	GnmFont const *k2 = (GnmFont const *) v2;

	return (k1->size_pts == k2->size_pts &&
		k1->is_bold == k2->is_bold &&
		k1->is_italic == k2->is_italic &&
		k1->context == k2->context &&
		strcmp (k1->font_name, k2->font_name) == 0);
}

guint
gnm_font_hash (gconstpointer v)
{
	GnmFont const *k = (GnmFont const *) v;
	return (guint)k->size_pts ^
		g_str_hash (k->font_name) ^
		(k->is_bold ? 0x33333333 : 0) ^
		(k->is_italic ? 0xcccccccc : 0) ^
		GPOINTER_TO_UINT (k->context);
}

GType
gnm_align_h_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{GNM_HALIGN_GENERAL, "GNM_HALIGN_GENERAL", "general"},
			{GNM_HALIGN_LEFT, "GNM_HALIGN_LEFT", "left"},
			{GNM_HALIGN_RIGHT, "GNM_HALIGN_RIGHT", "right"},
			{GNM_HALIGN_CENTER, "GNM_HALIGN_CENTER", "center"},
			{GNM_HALIGN_FILL, "GNM_HALIGN_FILL", "fill"},
			{GNM_HALIGN_JUSTIFY, "GNM_HALIGN_JUSTIFY", "justify"},
			{GNM_HALIGN_CENTER_ACROSS_SELECTION,
			 "GNM_HALIGN_CENTER_ACROSS_SELECTION",
			 "across-selection"},
			{GNM_HALIGN_DISTRIBUTED,
			 "GNM_HALIGN_DISTRIBUTED", "distributed"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmHAlign",
						values);
	}
	return etype;
}

GType
gnm_align_v_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{GNM_VALIGN_TOP, "GNM_VALIGN_TOP", "top"},
			{GNM_VALIGN_BOTTOM, "GNM_VALIGN_BOTTOM", "bottom"},
			{GNM_VALIGN_CENTER, "GNM_VALIGN_CENTER", "center"},
			{GNM_VALIGN_JUSTIFY, "GNM_VALIGN_JUSTIFY", "justify"},
			{GNM_VALIGN_DISTRIBUTED,
			 "GNM_VALIGN_DISTRIBUTED", "distributed"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmVAlign",
						values);
	}
	return etype;
}



static PangoFontMap *fontmap;
static PangoContext *context;

/**
 * gnm_pango_context_get:
 *
 * Simple wrapper to handle windowless operation
 * Returns: (transfer full):
 **/
PangoContext *
gnm_pango_context_get (void)
{
	if (!context) {
		GdkScreen *screen = gdk_screen_get_default ();

		if (screen != NULL) {
			context = gdk_pango_context_get_for_screen (screen);
		} else {
			if (!fontmap)
				fontmap = pango_cairo_font_map_new ();
			pango_cairo_font_map_set_resolution (PANGO_CAIRO_FONT_MAP (fontmap), 96);
			context = pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
		}
		pango_context_set_language (context, gtk_get_default_language ());
		pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);
	}

	return g_object_ref (context);
}

/**
 * gnm_font_init: (skip)
 */
void
gnm_font_init (void)
{
	PangoContext *context;
	GnmFont *gnumeric_default_font = NULL;
	double pts_scale = 72. / gnm_app_display_dpi_get (TRUE);

	style_font_hash		 = g_hash_table_new (
		gnm_font_hash, gnm_font_equal);
	style_font_negative_hash = g_hash_table_new (
		gnm_font_hash, gnm_font_equal);

	gnumeric_default_font_name = g_strdup (gnm_conf_get_core_defaultfont_name ());
	gnumeric_default_font_size = gnm_conf_get_core_defaultfont_size ();

	context = gnm_pango_context_get ();
	if (gnumeric_default_font_name && gnumeric_default_font_size >= 1)
		gnumeric_default_font = style_font_new_simple (context,
			gnumeric_default_font_name, gnumeric_default_font_size,
			FALSE, FALSE);
	if (gnumeric_default_font == NULL) {
		g_warning ("Configured default font '%s %f' not available, trying fallback...",
			   gnumeric_default_font_name, gnumeric_default_font_size);
		gnumeric_default_font = style_font_new_simple (context,
			DEFAULT_FONT, DEFAULT_SIZE, FALSE, FALSE);
		if (gnumeric_default_font != NULL) {
			g_free (gnumeric_default_font_name);
			gnumeric_default_font_name = g_strdup (DEFAULT_FONT);
			gnumeric_default_font_size = DEFAULT_SIZE;
		} else {
			g_warning ("Fallback font '%s %f' not available, trying 'fixed'...",
				   DEFAULT_FONT, DEFAULT_SIZE);
			gnumeric_default_font = style_font_new_simple (context,
				"fixed", 10, FALSE, FALSE);
			if (gnumeric_default_font != NULL) {
				g_free (gnumeric_default_font_name);
				gnumeric_default_font_name = g_strdup ("fixed");
				gnumeric_default_font_size = 10;
			} else {
				g_warning ("Even 'fixed 10' failed ??  We're going to exit now,"
					   "there is something wrong with your font configuration");
				exit (1);
			}
		}
	}

	gnm_font_default_width = pts_scale *
		PANGO_PIXELS (gnumeric_default_font->go.metrics->avg_digit_width);
	gnm_font_unref (gnumeric_default_font);
	g_object_unref (context);
}

/**
 * gnm_font_shutdown: (skip)
 *
 * Release all resources allocated by gnm_font_init.
 **/
void
gnm_font_shutdown (void)
{
	GList *fonts = NULL, *tmp;

	g_free (gnumeric_default_font_name);
	gnumeric_default_font_name = NULL;

	// Make a list of the fonts, then unref them.
	fonts = g_hash_table_get_keys (style_font_hash);
	for (tmp = fonts; tmp; tmp = tmp->next) {
		GnmFont *sf = tmp->data;
		if (sf->ref_count != 1)
			g_warning ("Font %s has %d references instead of the expected single.",
				   sf->font_name, sf->ref_count);
		gnm_font_unref (sf);
	}
	g_list_free (fonts);
	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;

	fonts = g_hash_table_get_keys (style_font_negative_hash);
	for (tmp = fonts; tmp; tmp = tmp->next) {
		GnmFont *sf = tmp->data;
		g_object_unref (sf->context);
		g_free (sf->font_name);
		g_free (sf);
	}
	g_list_free (fonts);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;

	if (context) {
		g_object_unref (context);
		context = NULL;
	}

	if (fontmap) {
		/* Do this late -- see bugs 558100 and 558254.  */
		/* and not at all on win32, where the life cycle is different */
#ifndef	G_OS_WIN32
		g_object_unref (fontmap);
#endif
		fontmap = NULL;
	}

}

/**
 * gnm_style_required_spanflags:
 * @style: the style
 *
 * What changes are required after applying the supplied style.
 **/
GnmSpanCalcFlags
gnm_style_required_spanflags (GnmStyle const *style)
{
	GnmSpanCalcFlags res = GNM_SPANCALC_SIMPLE;

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS))
		/* Note that style->cond_styles may not be set yet */
		/* More importantly, even if the conditions are empty we */
		/* have to rerender everything since we do not know what changed. */
		res |= GNM_SPANCALC_RE_RENDER | GNM_SPANCALC_RESIZE | GNM_SPANCALC_ROW_HEIGHT;
	else {
		gboolean const row_height =
			gnm_style_is_element_set (style, MSTYLE_FONT_SIZE) ||
			gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT) ||
			gnm_style_is_element_set (style, MSTYLE_ROTATION) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT);
		gboolean const size_change = row_height ||
			gnm_style_is_element_set (style, MSTYLE_FONT_NAME) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_BOLD) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC);
		gboolean const format_change =
			gnm_style_is_element_set (style, MSTYLE_FORMAT) ||
			gnm_style_is_element_set (style, MSTYLE_INDENT) ||
			gnm_style_is_element_set (style, MSTYLE_ALIGN_H) ||
			gnm_style_is_element_set (style, MSTYLE_ALIGN_V) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE) ||
			gnm_style_is_element_set (style, MSTYLE_FONT_COLOR);

		if (row_height)
			res |= GNM_SPANCALC_ROW_HEIGHT;
		if (format_change || size_change)
			res |= GNM_SPANCALC_RE_RENDER | GNM_SPANCALC_RESIZE;
	}
	return res;
}

/**
 * gnm_style_default_halign:
 * @style:
 * @c:
 *
 * Select the appropriate horizontal alignment depending on the style and cell
 * value.
 */
GnmHAlign
gnm_style_default_halign (GnmStyle const *style, GnmCell const *c)
{
	GnmHAlign align = gnm_style_get_align_h (style);
	GnmValue *v;

	if (align != GNM_HALIGN_GENERAL)
		return align;
	g_return_val_if_fail (c != NULL, GNM_HALIGN_RIGHT);

	if (c->base.sheet && c->base.sheet->display_formulas &&
	    gnm_cell_has_expr (c))
		return GNM_HALIGN_LEFT;

	for (v = c->value; v != NULL ; )
		switch (v->v_any.type) {
		case VALUE_BOOLEAN:
		case VALUE_ERROR:
			return GNM_HALIGN_CENTER;

		case VALUE_FLOAT: {
			double a = gnm_style_get_rotation (style);
			if (a > 0 && a < 180)
				return GNM_HALIGN_LEFT;
			return GNM_HALIGN_RIGHT;
		}

		case VALUE_ARRAY:
			/* Tail recurse into the array */
			if (v->v_array.x > 0 && v->v_array.y > 0) {
				v = v->v_array.vals [0][0];
				continue;
			}

		default:
			if (gnm_style_get_rotation (style) > 180)
				return GNM_HALIGN_RIGHT;
			return GNM_HALIGN_LEFT;
		}
	return GNM_HALIGN_RIGHT;
}

PangoUnderline
gnm_translate_underline_to_pango (GnmUnderline ul)
{
	g_return_val_if_fail (ul >= UNDERLINE_NONE, PANGO_UNDERLINE_NONE);
	g_return_val_if_fail (ul <= UNDERLINE_DOUBLE_LOW, PANGO_UNDERLINE_NONE);

	switch (ul) {
	case UNDERLINE_SINGLE:
		return PANGO_UNDERLINE_SINGLE;
	case UNDERLINE_DOUBLE:
	case UNDERLINE_DOUBLE_LOW:
		return PANGO_UNDERLINE_DOUBLE;
	case UNDERLINE_SINGLE_LOW:
		return PANGO_UNDERLINE_LOW;
	case UNDERLINE_NONE:
	default:
		return PANGO_UNDERLINE_NONE;
	}
}

GnmUnderline
gnm_translate_underline_from_pango (PangoUnderline pul)
{
	g_return_val_if_fail (pul >= PANGO_UNDERLINE_NONE, UNDERLINE_NONE);
	g_return_val_if_fail (pul <= PANGO_UNDERLINE_ERROR, UNDERLINE_NONE);

	switch (pul) {
	case PANGO_UNDERLINE_SINGLE:
		return UNDERLINE_SINGLE;
	case PANGO_UNDERLINE_DOUBLE:
		return UNDERLINE_DOUBLE;
	case PANGO_UNDERLINE_LOW:
		return UNDERLINE_SINGLE_LOW;
	case PANGO_UNDERLINE_ERROR:
		/* What?  */
	case PANGO_UNDERLINE_NONE:
	default:
		return UNDERLINE_NONE;
	}

}
