/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998, 1999 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "format.h"
#include "color.h"
#include "gnumeric-util.h"

#undef DEBUG_FONTS
#define DEFAULT_FONT      "Helvetica"
#define DEFAULT_SIZE 12

static GHashTable *style_format_hash;
static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;
static GHashTable *style_border_hash;
static GHashTable *style_color_hash;

StyleFont *gnumeric_default_font;
StyleFont *gnumeric_default_bold_font;
StyleFont *gnumeric_default_italic_font;

#if 0
#warning "Temporary disabled"
static StyleFont *standard_fonts[2][2];  /* [bold-p][italic-p] */
static char *standard_font_names[2][2];  /* [bold-p][italic-p] */
#endif

StyleFormat *
style_format_new (const char *name)
{
	StyleFormat *format;

	g_return_val_if_fail (name != NULL, NULL);

	format = (StyleFormat *) g_hash_table_lookup (style_format_hash, name);

	if (!format){
		format = g_new0 (StyleFormat, 1);
		format->format = g_strdup (name);
		format_compile (format);
		g_hash_table_insert (style_format_hash, format->format, format);
	}
	format->ref_count++;

	return format;
}

void
style_format_ref (StyleFormat *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
}

void
style_format_unref (StyleFormat *sf)
{
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
	if (sf->ref_count != 0)
		return;

	g_hash_table_remove (style_format_hash, sf->format);

	format_destroy (sf);
	g_free (sf->format);
	g_free (sf);
}

#ifdef DEBUG_FONTS
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "gdk/gdkprivate.h" /* Sorry */

static char *
my_gdk_actual_font_name (GdkFont *font)
{
	GdkFontPrivate *private;
	Atom atom, font_atom;

	private = (GdkFontPrivate *)font;
	font_atom = XInternAtom (private->xdisplay, "FONT", True);
	if (font_atom != None) {
		if (XGetFontProperty (private->xfont, font_atom, &atom) == True) {
			char *xname, *gname;
			xname = XGetAtomName (private->xdisplay, atom);
			gname = g_strdup ((xname && *xname) ? xname : "<unspecified>");
			XFree (xname);
			return gname;
		}
	}
	return NULL;
}
#endif

StyleFont *
style_font_new_simple (const char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	/* This cast does not mean we will change the name.  */
	key.font_name = (char *)font_name;
	key.size      = size;
	key.is_bold   = bold;
	key.is_italic = italic;
	key.scale     = scale;
	
	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		GnomeDisplayFont *display_font;
		GnomeFont *gnome_font;

		display_font = gnome_get_display_font (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size, scale);

		if (!display_font)
			return NULL;

		gnome_font = gnome_font_new_closest (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size);
			
		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->size      = size;
		font->scale     = scale;
		font->dfont     = display_font;
		font->font      = gnome_font;
		font->is_bold   = bold;
		font->is_italic = italic;
		g_hash_table_insert (style_font_hash, font, font);
	}

	font->ref_count++;
	return font;
}

StyleFont *
style_font_new (const char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	font = style_font_new_simple (font_name, size, scale, bold, italic);
	if (!font){
		if (bold)
			font = gnumeric_default_bold_font;
		else if (italic)
			font = gnumeric_default_italic_font;
		else
			font = gnumeric_default_font;
		style_font_ref (font);
	}

	return font;
}

/*
 * Creates a new StyleFont from an existing StyleFont
 * at the scale @scale
 */
StyleFont *
style_font_new_from (StyleFont *sf, double scale)
{
	StyleFont *new_sf;
	
	g_return_val_if_fail (sf != NULL, NULL);
	g_return_val_if_fail (scale != 0.0, NULL);

	new_sf = style_font_new_simple (sf->font_name, sf->size, scale, sf->is_bold, sf->is_italic);
	if (!new_sf){
	        new_sf = gnumeric_default_font;
		style_font_ref (new_sf);
	}
	return new_sf;
}

GdkFont *
style_font_gdk_font (StyleFont *sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	return sf->dfont->gdk_font;
}

GnomeFont *
style_font_gnome_font (StyleFont *sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	return sf->dfont->gnome_font;
}

int
style_font_get_height (StyleFont *sf)
{
	GdkFont *gdk_font;
	GnomeFont *gnome_font;
	static int warning_shown;
	int height;

	g_return_val_if_fail (sf != NULL, 0);

	gdk_font = sf->dfont->gdk_font;

	height = gdk_font->ascent + gdk_font->descent;

	gnome_font = sf->dfont->gnome_font;
	
	if (height < gnome_font->size)
		height = gnome_font->size;

	if (!warning_shown){
		g_warning ("this should use a gnome-print provided method");
		warning_shown = 1;
	}
	return height;
}

void
style_font_ref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
}

void
style_font_unref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
	if (sf->ref_count != 0)
		return;

	g_hash_table_remove (style_font_hash, sf);
	g_free (sf->font_name);
	g_free (sf);
}

StyleBorder *
style_border_new (StyleBorderType  border_type  [4],
		  StyleColor      *border_color [4])

{
	StyleBorder key, *border;
	int lp;

 	memcpy (&key.type, border_type, sizeof(key.type));
 	for (lp = 0; lp < 4; lp++){
		if (border_color [lp])
			key.color [lp] = border_color [lp];
		else
 			key.color [lp] = NULL;
 	}

	border = (StyleBorder *) g_hash_table_lookup (style_border_hash,
						      &key);
	if (!border){
		border = g_new0 (StyleBorder, 1);
		*border = key;
		g_hash_table_insert (style_border_hash, border, border);
		border->ref_count = 0;
	}
	border->ref_count++;

	return border;
}

void
style_border_ref (StyleBorder *sb)
{
	g_return_if_fail (sb != NULL);

	sb->ref_count++;
}

void
style_border_unref (StyleBorder *sb)
{
	g_return_if_fail (sb != NULL);
	g_return_if_fail (sb->ref_count > 0);

	sb->ref_count--;
	if (sb->ref_count != 0)
		return;

	g_hash_table_remove (style_border_hash, sb);
	g_free (sb);
}

StyleBorder *
style_border_new_plain (void)
{
 	StyleBorderType style [4] = { BORDER_NONE, BORDER_NONE, BORDER_NONE, BORDER_NONE };
 	StyleColor *color [4] = { NULL, NULL, NULL, NULL };

	return style_border_new (style, color);
}

StyleColor *
style_color_new (gushort red, gushort green, gushort blue)
{
	StyleColor *sc;
	StyleColor key;

	key.color.red   = red;
	key.color.green = green;
	key.color.blue  = blue;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc){
		sc = g_new (StyleColor, 1);
		sc->color = key.color;
		sc->color.pixel = color_alloc (red, green, blue);

		g_hash_table_insert (style_color_hash, sc, sc);
		sc->ref_count = 0;
	}
	sc->ref_count++;

	return sc;
}

void
style_color_ref (StyleColor *sc)
{
	g_return_if_fail (sc != NULL);

	sc->ref_count++;
}

void
style_color_unref (StyleColor *sc)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	if (sc->ref_count != 0)
		return;

	/*
	 * There is no need to deallocate colors, as they come from
	 * the GDK Color Context
	 */
	g_hash_table_remove (style_color_hash, sc);
	g_free (sc);
}

Style *
style_new (void)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->valid_flags = STYLE_ALL;

	style->format      = style_format_new ("General");
	style->font        = style_font_new (DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, FALSE);
	style->border      = style_border_new_plain ();
	style->fore_color  = style_color_new (0, 0, 0);
	style->back_color  = style_color_new (0xffff, 0xffff, 0xffff);
	style->halign      = HALIGN_GENERAL;
	style->valign      = VALIGN_CENTER;
	style->orientation = ORIENT_HORIZ;

	{
		static int warning_shown;

		if (!warning_shown){
			g_warning ("Font style created at zoom factor 1.0");
			warning_shown = TRUE;
		}
	}

	return style;
}

guint
style_hash (gconstpointer a)
{
	Style *style = (Style *) a;

	return ((int) style->format) ^ ((int) style->font) ^ ((int) style->border)
		^ ((int) style->fore_color) ^ ((int) style->back_color);
}

gint
style_compare (gconstpointer a, gconstpointer b)
{
	Style *sa, *sb;

	sa = (Style *) a;
	sb = (Style *) b;

	if (sa->format != sb->format)
		return FALSE;
	if (sa->font != sb->font)
		return FALSE;
	if (sa->border != sb->border)
		return FALSE;
	if (sa->fore_color != sb->fore_color)
		return FALSE;
	if (sa->halign != sb->halign)
		return FALSE;
	if (sa->valign != sb->valign)
		return FALSE;
	if (sa->orientation != sb->orientation)
		return FALSE;

	return TRUE;
}

Style *
style_new_empty (void)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->valid_flags = 0;

	return style;
}

void
style_destroy (Style *style)
{
	g_return_if_fail (style != NULL);

	if (style->valid_flags & STYLE_FORMAT)
		style_format_unref (style->format);

	if (style->valid_flags & STYLE_FONT)
		style_font_unref (style->font);

	if (style->valid_flags & STYLE_BORDER)
		style_border_unref (style->border);

	if (style->valid_flags & STYLE_FORE_COLOR)
		if (style->fore_color)
			style_color_unref (style->fore_color);

	if (style->valid_flags & STYLE_BACK_COLOR)
		if (style->back_color)
			style_color_unref (style->back_color);

	g_free (style);
}


Style *
style_duplicate (const Style *original)
{
	Style *style;

	style = g_new0 (Style, 1);

	/* Bit copy */
	*style = *original;

	/* Do the rest of the copy */
	if (style->valid_flags & STYLE_FORMAT)
		style_format_ref (style->format);
	else
		style->format = NULL;

	if (style->valid_flags & STYLE_FONT)
		style_font_ref (style->font);
	else
		style->font = NULL;

	if (style->valid_flags & STYLE_BORDER)
		style_border_ref (style->border);
	else
		style->border = NULL;

	if (style->valid_flags & STYLE_FORE_COLOR)
		style_color_ref (style->fore_color);
	else
		style->fore_color = NULL;

	if (style->valid_flags & STYLE_BACK_COLOR)
		style_color_ref (style->back_color);
	else
		style->back_color = NULL;

	return style;
}

/*
 * The routines used to hash and compare the different styles
 */
static gint
font_equal (gconstpointer v, gconstpointer v2)
{
	const StyleFont *k1 = (const StyleFont *) v;
	const StyleFont *k2 = (const StyleFont *) v2;

	if (k1->size != k2->size)
		return 0;

	if (k1->is_bold != k2->is_bold)
		return 0;
	if (k1->is_italic != k2->is_italic)
		return 0;
	if (k1->scale != k2->scale)
		return 0;

	return !strcmp (k1->font_name, k2->font_name);
}

static guint
font_hash (gconstpointer v)
{
	const StyleFont *k = (const StyleFont *) v;

	return k->size + g_str_hash (k->font_name);
}

static gint
border_equal (gconstpointer v, gconstpointer v2)
{
	const StyleBorder *k1 = (const StyleBorder *) v;
	const StyleBorder *k2 = (const StyleBorder *) v2;
	int lp;

 	for (lp = 0; lp < 4; lp++)
 	{
 		if (k1->type [lp] != k2->type [lp])
 			return 0;
 		if (k1->type [lp] != BORDER_NONE &&
		    k1->color [lp] != k2->color [lp])
			return 0;
	}

	return 1;
}

static guint
border_hash (gconstpointer v)
{
	const StyleBorder *k = (const StyleBorder *) v;

 	return (k->type [STYLE_LEFT] << 12) | (k->type [STYLE_RIGHT] << 8) |
	       (k->type [STYLE_TOP] << 4)   | (k->type [STYLE_BOTTOM]);

}

static gint
color_equal (gconstpointer v, gconstpointer v2)
{
	const StyleColor *k1 = (const StyleColor *) v;
	const StyleColor *k2 = (const StyleColor *) v2;

	if (k1->color.red   == k2->color.red &&
	    k1->color.green == k2->color.green &&
	    k1->color.blue  == k2->color.blue)
		return 1;

	return 0;
}

static guint
color_hash (gconstpointer v)
{
	const StyleColor *k = (const StyleColor *)v;

	return (k->color.red << 16) | (k->color.green << 8) | (k->color.blue);
}

void
style_merge_to (Style *target, Style *source)
{
	if (!(target->valid_flags & STYLE_FORMAT))
		if (source->valid_flags & STYLE_FORMAT){
			target->valid_flags |= STYLE_FORMAT;
			target->format = source->format;
			style_format_ref (target->format);
		}

	if (!(target->valid_flags & STYLE_FONT))
		if (source->valid_flags & STYLE_FONT){
			target->valid_flags |= STYLE_FONT;
			target->font = source->font;
			style_font_ref (target->font);
		}

	if (!(target->valid_flags & STYLE_BORDER))
		if (source->valid_flags & STYLE_BORDER){
			target->valid_flags |= STYLE_BORDER;
			target->border = source->border;
			style_border_ref (target->border);
		}

	if (!(target->valid_flags & STYLE_ALIGN))
		if (source->valid_flags & STYLE_ALIGN){
			target->valid_flags |= STYLE_ALIGN;
			target->halign      = source->halign;
			target->valign      = source->valign;
			target->orientation = source->orientation;
		}

	if (!(target->valid_flags & STYLE_FORE_COLOR))
		if (source->valid_flags & STYLE_FORE_COLOR){
			target->valid_flags |= STYLE_FORE_COLOR;
			target->fore_color = source->fore_color;
			if (target->fore_color)
				style_color_ref (target->fore_color);
		}

	if (!(target->valid_flags & STYLE_BACK_COLOR))
		if (source->valid_flags & STYLE_BACK_COLOR){
			target->valid_flags |= STYLE_BACK_COLOR;
			target->back_color = source->back_color;
			if (target->back_color)
				style_color_ref (target->back_color);
		}

	if (!(target->valid_flags & STYLE_PATTERN))
		if (source->valid_flags & STYLE_PATTERN){
			target->valid_flags |= STYLE_PATTERN;
			target->pattern = source->pattern;
		}
}

static void
font_init (void)
{
	gnumeric_default_font = style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, FALSE);

#warning "Some Morten code here looks useful, but needs to be redone for gnome-print"
#if 0
	for (boldp = 0; boldp <= 1; boldp++) {
		for (italicp = 0; italicp <= 1; italicp++) {
			char *name;
			int size = DEFAULT_FONT_SIZE;

			name = g_strdup (DEFAULT_FONT);

			if (boldp) {
				char *tmp;
				tmp = font_get_bold_name (name, size);
				g_free (name);
				name = tmp;
			}

			if (italicp) {
				char *tmp;
				tmp = font_get_italic_name (name, size);
				g_free (name);
				name = tmp;
			}

			standard_fonts[boldp][italicp] =
				style_font_new_simple (name, size);
			if (!standard_fonts[boldp][italicp]) {
				fprintf (stderr,
					 "Gnumeric failed to find a suitable default font.\n"
					 "Please file a proper bug report (see http://bugs.gnome.org)\n"
					 "including the following extra items:\n"
					 "\n"
					 "1. Values of LC_ALL and LANG environment variables.\n"
					 "2. A list of the fonts on your system (from the xlsfonts program).\n"
					 "3. The values here: boldp=%d, italicp=%d\n"
					 "\n"
					 "Thanks -- the Gnumeric Team\n",
					 boldp, italicp);
				exit (1);
			}

			standard_font_names[boldp][italicp] = name;
		}
	}
#endif
	
	if (!gnumeric_default_font)
		g_error ("Could not load the default font");

	/*
	 * Load bold font
	 */
	gnumeric_default_bold_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, 1.0, TRUE, FALSE);
	if (gnumeric_default_bold_font == NULL){
	    gnumeric_default_bold_font = gnumeric_default_font;
	    style_font_ref (gnumeric_default_bold_font);
	}

	/*
	 * Load italic font
	 */
	gnumeric_default_italic_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, TRUE);
	if (gnumeric_default_italic_font == NULL){
		gnumeric_default_italic_font = gnumeric_default_font;
		style_font_ref (gnumeric_default_italic_font);
	}
}

void
style_init (void)
{
	style_format_hash = g_hash_table_new (g_str_hash, g_str_equal);
	style_font_hash   = g_hash_table_new (font_hash, font_equal);
	style_border_hash = g_hash_table_new (border_hash, border_equal);
	style_color_hash  = g_hash_table_new (color_hash, color_equal);

	style_font_negative_hash = g_hash_table_new (font_hash, font_equal);

	font_init ();
}

static void
delete_neg_font (gpointer key, gpointer value, gpointer user_data)
{
	StyleFont *font = key;

	g_free (font->font_name);
	g_free (font);
}

/*
 * Release all resources allocated by style_init.
 */
void
style_shutdown (void)
{
#warning Temporary disabled
#if 0
	int boldp, italicp;

	for (boldp = 0; boldp <= 1; boldp++) {
		for (italicp = 0; italicp <= 1; italicp++) {
			g_free (standard_font_names [boldp][italicp]);
			standard_font_names [boldp][italicp] = NULL;

			style_font_unref (standard_fonts [boldp][italicp]);
			standard_fonts [boldp][italicp] = NULL;
		}
	}
#endif
	
	gnumeric_default_font = NULL;
	gnumeric_default_bold_font = NULL;
	gnumeric_default_italic_font = NULL;

	g_hash_table_destroy (style_format_hash);
	style_format_hash = NULL;
	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;
	g_hash_table_destroy (style_border_hash);
	style_border_hash = NULL;
	g_hash_table_destroy (style_color_hash);
	style_color_hash = NULL;

	g_hash_table_foreach (style_font_negative_hash, delete_neg_font, NULL);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;
}
