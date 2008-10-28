/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998-2004 Miguel de Icaza
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "style.h"
#include "style-font.h"

#include "gnm-format.h"
#include "style-color.h"
#include "application.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"

#include "gui-util.h"
#include "mathfunc.h"
#include "gnumeric-gconf.h"

#include <pango/pangoft2.h>
#include <gdk/gdkpango.h>
#include <gtk/gtkmain.h>
#include <string.h>
#include <goffice/utils/go-font.h>

#undef DEBUG_REF_COUNT
#undef DEBUG_FONTS

static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;

double gnm_font_default_width;
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

	static char const * const map[][2] = {
		{ "Times New Roman", "Times"},
		{ "Tms Rmn",	     "Times"},
		{ "Arial",           "Sans"},
		{ "Albany",          "Sans"},
		{ "Helvetica",       "Sans"},
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
		if (g_ascii_strcasecmp (map[i][0], fontname) == 0)
			return map[i][1];

	return NULL;
}

static GnmFont *
style_font_new_simple (PangoContext *context,
		       char const *font_name, double size_pts, double scale,
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
	key.scale     = scale;

	font = (GnmFont *) g_hash_table_lookup (style_font_hash, &key);
	if (font == NULL) {
		PangoFontDescription *desc;

		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font = g_new0 (GnmFont, 1);
		font->font_name = g_strdup (font_name);
		font->size_pts  = size_pts;
		font->scale     = scale;
		font->is_bold   = bold;
		font->is_italic = italic;
		/* One reference for the cache, one for the caller. */
		font->ref_count = 2;

		desc = pango_font_description_copy (pango_context_get_font_description (context));

		pango_font_description_set_family (desc, font_name);
		pango_font_description_set_weight (desc,
			bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
		pango_font_description_set_style (desc,
			italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		pango_font_description_set_size (desc, size_pts * PANGO_SCALE);

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
				pango_font_description_free (desc);
				g_hash_table_insert (style_font_negative_hash,
						     font, font);
				return NULL;
			}
		}

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
	      char const *font_name, double size_pts, double scale,
	      gboolean bold, gboolean italic)
{
	GnmFont *font;

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
gnm_font_ref (GnmFont *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
#ifdef DEBUG_REF_COUNT
	g_message (__FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
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

	if (sf->pango.font != NULL) {
		g_object_unref (G_OBJECT (sf->pango.font));
		sf->pango.font = NULL;
	}
	if (sf->go.font) {
		go_font_unref (sf->go.font);
		sf->go.font = NULL;
	}
	if (sf->go.metrics) {
		go_font_metrics_free (sf->go.metrics);
		sf->go.metrics = NULL;
	}
	g_hash_table_remove (style_font_hash, sf);
	g_free (sf->font_name);
	g_free (sf);
}

gint
gnm_font_equal (gconstpointer v, gconstpointer v2)
{
	GnmFont const *k1 = (GnmFont const *) v;
	GnmFont const *k2 = (GnmFont const *) v2;

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
gnm_font_hash (gconstpointer v)
{
	GnmFont const *k = (GnmFont const *) v;

	return k->size_pts + g_str_hash (k->font_name);
}

static PangoFontMap *fontmap;

/**
 * gnm_pango_context_get :
 *
 * Simple wrapper to handle windowless operation
 **/
PangoContext *
gnm_pango_context_get (void)
{
	PangoContext *context;
	GdkScreen *screen = gdk_screen_get_default ();

	if (screen != NULL) {
		context = gdk_pango_context_get_for_screen (screen);
	} else {
		if (!fontmap)
			fontmap = pango_ft2_font_map_new ();
		pango_ft2_font_map_set_resolution (PANGO_FT2_FONT_MAP (fontmap), 96, 96);
		context = pango_ft2_font_map_create_context (PANGO_FT2_FONT_MAP (fontmap));
	}
	pango_context_set_language (context, gtk_get_default_language ());
	pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);

	return context;
}

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

	gnumeric_default_font_name = g_strdup (gnm_app_prefs->default_font.name);
	gnumeric_default_font_size = gnm_app_prefs->default_font.size;

	context = gnm_pango_context_get ();
	if (gnumeric_default_font_name && gnumeric_default_font_size >= 1)
		gnumeric_default_font = style_font_new_simple (context,
			gnumeric_default_font_name, gnumeric_default_font_size,
			1., FALSE, FALSE);
	if (gnumeric_default_font == NULL) {
		g_warning ("Configured default font '%s %f' not available, trying fallback...",
			   gnumeric_default_font_name, gnumeric_default_font_size);
		gnumeric_default_font = style_font_new_simple (context,
			DEFAULT_FONT, DEFAULT_SIZE, 1., FALSE, FALSE);
		if (gnumeric_default_font != NULL) {
			g_free (gnumeric_default_font_name);
			gnumeric_default_font_name = g_strdup (DEFAULT_FONT);
			gnumeric_default_font_size = DEFAULT_SIZE;
		} else {
			g_warning ("Fallback font '%s %f' not available, trying 'fixed'...",
				   DEFAULT_FONT, DEFAULT_SIZE);
			gnumeric_default_font = style_font_new_simple (context,
				"fixed", 10, 1., FALSE, FALSE);
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
	g_object_unref (G_OBJECT (context));
}

static void
delete_neg_font (GnmFont *sf, gpointer value, gpointer user_data)
{
	g_free (sf->font_name);
	g_free (sf);
}

static void
list_cached_fonts (GnmFont *font, gpointer value, GSList **lp)
{
	*lp = g_slist_prepend (*lp, font);
}

/**
 * gnm_font_shutdown:
 *
 * Release all resources allocated by gnm_font_init.
 **/
void
gnm_font_shutdown (void)
{
	GSList *fonts = NULL, *tmp;

	g_free (gnumeric_default_font_name);
	gnumeric_default_font_name = NULL;

	/* Make a list of the fonts, then unref them.  */
	g_hash_table_foreach (style_font_hash, (GHFunc) list_cached_fonts, &fonts);
	for (tmp = fonts; tmp; tmp = tmp->next) {
		GnmFont *sf = tmp->data;
		if (sf->ref_count != 1)
			g_warning ("Font %s has %d references instead of the expected single.",
				   sf->font_name, sf->ref_count);
		gnm_font_unref (sf);
	}
	g_slist_free (fonts);

	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;

	g_hash_table_foreach (style_font_negative_hash, (GHFunc) delete_neg_font, NULL);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;

	if (fontmap) {
		/*
		 * Workaround for bug #143542 (PangoFT2Fontmap leak).
		 * See also bug #148997 (Text layer rendering leaks font file
		 * descriptor).
		 */
		pango_ft2_font_map_substitute_changed (PANGO_FT2_FONT_MAP (fontmap));

		/* Do this late -- see bugs 558100 and 558254.  */
		g_object_unref (fontmap);
		fontmap = NULL;
	}
}

/**
 * gnm_style_required_spanflags
 * @style: the style
 *
 * What changes are required after applying the supplied style.
 **/
GnmSpanCalcFlags
gnm_style_required_spanflags (GnmStyle const *style)
{
	GnmSpanCalcFlags res = GNM_SPANCALC_SIMPLE;

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
	return res;
}

/**
 * gnm_style_default_halign :
 * @style :
 * @cell  :
 *
 * Select the appropriate horizontal alignment depending on the style and cell
 * value.
 */
GnmHAlign
gnm_style_default_halign (GnmStyle const *mstyle, GnmCell const *c)
{
	GnmHAlign align = gnm_style_get_align_h (mstyle);
	GnmValue *v;

	if (align != HALIGN_GENERAL)
		return align;
	g_return_val_if_fail (c != NULL, HALIGN_RIGHT);

	if (c->base.sheet && c->base.sheet->display_formulas &&
	    gnm_cell_has_expr (c))
		return HALIGN_LEFT;

	for (v = c->value; v != NULL ; )
		switch (v->type) {
		case VALUE_BOOLEAN:
		case VALUE_ERROR:
			return HALIGN_CENTER;

		case VALUE_FLOAT: {
			double a = gnm_style_get_rotation (mstyle);
			if (a > 0 && a < 180)
				return HALIGN_LEFT;
			return HALIGN_RIGHT;
		}

		case VALUE_ARRAY:
			/* Tail recurse into the array */
			if (v->v_array.x > 0 && v->v_array.y > 0) {
				v = v->v_array.vals [0][0];
				continue;
			}

		default:
			if (gnm_style_get_rotation (mstyle) > 180)
				return HALIGN_RIGHT;
			return HALIGN_LEFT;
		}
	return HALIGN_RIGHT;
}
