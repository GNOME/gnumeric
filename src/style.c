/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "format.h"
#include "color.h"
#include "gnumeric-util.h"

#undef DEBUG_FONTS

#define DEFAULT_FONT "-adobe-helvetica-medium-r-normal--*-*-*-*-*-*-iso8859-*"
#define DEFAULT_FONT_SIZE 12

static GHashTable *style_format_hash;
static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;
static GHashTable *style_border_hash;
static GHashTable *style_color_hash;

StyleFont *gnumeric_default_font;
StyleFont *gnumeric_default_bold_font;
StyleFont *gnumeric_default_italic_font;

static StyleFont *standard_fonts[2][2];  /* [bold-p][italic-p] */
static char *standard_font_names[2][2];  /* [bold-p][italic-p] */

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

static void
font_compute_hints (StyleFont *font)
{
	const char *p = font->font_name;
	int hyphens = 0;

	font->hint_is_bold = 0;
	font->hint_is_italic = 0;

	for (;*p; p++){
		if (*p == '-'){
			hyphens++;

			if (hyphens == 3 && (strncmp (p+1, "bold", 4) == 0))
				font->hint_is_bold = 1;

			if (hyphens == 4){
				if (*(p+1) == 'o' || *(p+1) == 'i')
					font->hint_is_italic = 1;
			}

			if (hyphens > 5)
				break;
		}
	}
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
style_font_new_simple (const char *font_name, int units)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (units > 0, NULL);

	/* This cast does not mean we will change the name.  */
	key.font_name = (char *)font_name;
	key.units = units;

	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		GdkFont *gdk_font;
		char *font_name_copy, *font_name_with_size;
		char sizetxt[4 * sizeof (int)];

		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font_name_copy = g_strdup (font_name);
		sprintf (sizetxt, "%d", units);
		font_name_with_size =
			font_change_component (font_name, 6, sizetxt);
		gdk_font = gdk_font_load (font_name_copy);
#ifdef DEBUG_FONTS
		printf ("Font \"%s\"\n", font_name_with_size);
#endif
		g_free (font_name_with_size);

		font = g_new0 (StyleFont, 1);
		font->font_name = font_name_copy;
		font->units    = units;
		font->font     = gdk_font;

		if (!gdk_font) {
			g_hash_table_insert (style_font_negative_hash,
					     font, font);
#ifdef DEBUG_FONTS
			printf ("was not resolved.\n\n");
#endif
			return NULL;
		}

#ifdef DEBUG_FONTS
		{
			char *fname = my_gdk_actual_font_name (gdk_font);
			printf ("was resolved as \"%s\".\n\n", fname ? fname : "(null)");
			g_free (fname);
		}
#endif

		font_compute_hints (font);

		g_hash_table_insert (style_font_hash, font, font);
	}

	font->ref_count++;
	return font;
}

StyleFont *
style_font_new (const char *font_name, int units)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (units != 0, NULL);

	font = style_font_new_simple (font_name, units);
	if (!font){
		font = gnumeric_default_font;
		style_font_ref (font);
	}

	return font;
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
	style->font        = style_font_new (DEFAULT_FONT, DEFAULT_FONT_SIZE);
	style->border      = style_border_new_plain ();
	style->fore_color  = style_color_new (0, 0, 0);
	style->back_color  = style_color_new (0xffff, 0xffff, 0xffff);
	style->halign      = HALIGN_GENERAL;
	style->valign      = VALIGN_CENTER;
	style->orientation = ORIENT_HORIZ;

	return style;
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

	if (k1->units != k2->units)
		return 0;

	return !strcmp (k1->font_name, k2->font_name);
}

static guint
font_hash (gconstpointer v)
{
	const StyleFont *k = (const StyleFont *) v;

	return k->units + g_str_hash (k->font_name);
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
	int boldp, italicp;

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

	gnumeric_default_font = standard_fonts[0][0];
	gnumeric_default_bold_font = standard_fonts[1][0];
	gnumeric_default_italic_font = standard_fonts[0][1];
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
	int boldp, italicp;

	for (boldp = 0; boldp <= 1; boldp++) {
		for (italicp = 0; italicp <= 1; italicp++) {
			g_free (standard_font_names [boldp][italicp]);
			standard_font_names [boldp][italicp] = NULL;

			style_font_unref (standard_fonts [boldp][italicp]);
			standard_fonts [boldp][italicp] = NULL;
		}
	}

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
