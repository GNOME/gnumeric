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

#define DEFAULT_FONT      "Helvetica"
#define DEFAULT_SIZE 12

static GHashTable *style_format_hash;
static GHashTable *style_font_hash;
static GHashTable *style_border_hash;
static GHashTable *style_color_hash;

StyleFont *gnumeric_default_font;
StyleFont *gnumeric_default_bold_font;
StyleFont *gnumeric_default_italic_font;

StyleFormat *
style_format_new (char *name)
{
	StyleFormat *format;

	g_return_val_if_fail (name != NULL, NULL);
	
	format = (StyleFormat *) g_hash_table_lookup (style_format_hash, name);
	
	if (!format){
		format = g_new0 (StyleFormat, 1);
		format->format = g_strdup (name);
		format_compile (format);
		g_hash_table_insert (style_format_hash, name, format);
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

StyleFont *
style_font_new_simple (char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	key.font_name = font_name;
	key.size      = size;
	key.is_bold   = bold;
	key.is_italic = italic;
	
	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		GnomeDisplayFont *display_font;

		display_font = gnome_get_display_font (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size, scale);
		
		if (!display_font)
			return NULL;
		
		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->size      = size;
		font->scale     = scale;
		font->dfont     = display_font;
		font->is_bold   = bold;
		font->is_italic = italic;
		g_hash_table_insert (style_font_hash, font, font);
	}
	font->ref_count++;

	return font;
}

StyleFont *
style_font_new (char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	font = style_font_new_simple (font_name, size, scale, bold, italic);
	if (!font){
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
	int lp ;

 	memcpy (&key.type, border_type, sizeof(key.type)) ;
 	for (lp = 0; lp < 4; lp++){
		if (border_color [lp])
			key.color [lp] = border_color [lp] ;
		else
 			key.color [lp] = NULL ;
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
 	StyleBorderType style [4] = { BORDER_NONE, BORDER_NONE, BORDER_NONE, BORDER_NONE } ;
 	StyleColor *color [4] = { NULL, NULL, NULL, NULL } ;

	return style_border_new (style, color) ;
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
style_duplicate (Style *original)
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
	StyleFont *k1 = (StyleFont *) v;
	StyleFont *k2 = (StyleFont *) v2;

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
	StyleFont *k = (StyleFont *) v;

	return k->size + g_str_hash (k->font_name);
}

static gint
border_equal (gconstpointer v, gconstpointer v2)
{
	StyleBorder *k1 = (StyleBorder *) v;
	StyleBorder *k2 = (StyleBorder *) v2;
	int lp ;

 	for (lp = 0; lp < 4; lp++)
 	{
 		if (k1->type [lp] != k2->type [lp])
 			return 0 ;
 		if (k1->type [lp] != BORDER_NONE &&
		    k1->color [lp] != k2->color [lp])
			return 0;
	}
	
	return 1;
}

static guint
border_hash (gconstpointer v)
{
	StyleBorder *k = (StyleBorder *) v;

 	return (k->type [STYLE_LEFT] << 12) | (k->type [STYLE_RIGHT] << 8) |
	       (k->type [STYLE_TOP] << 4)   | (k->type [STYLE_BOTTOM]) ;

}

static gint
color_equal (gconstpointer v, gconstpointer v2)
{
	StyleColor *k1 = (StyleColor *) v;
	StyleColor *k2 = (StyleColor *) v2;

	if (k1->color.red   == k2->color.red &&
	    k1->color.green == k2->color.green &&
	    k1->color.blue  == k2->color.blue)
		return 1;
	
	return 0;
}

static guint
color_hash (gconstpointer v)
{
	StyleColor *k = (StyleColor *)v;

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

	if (!gnumeric_default_font)
		g_error ("Could not load the default font");

	g_warning ("Using hard coded 14!\n");
	
	gnumeric_default_bold_font = style_font_new (DEFAULT_FONT, 14, 1.0, TRUE, FALSE);
	gnumeric_default_italic_font = style_font_new (DEFAULT_FONT, 14, 1.0, FALSE, TRUE);

	printf ("Font: %s\n", gnumeric_default_bold_font->dfont->x_font_name);
	{
		static int warning_shown;

		if (!warning_shown){
			g_warning ("Style created with scale is 1.0");
			warning_shown = TRUE;
		}
	}
}

void
style_init (void)
{
	style_format_hash = g_hash_table_new (g_str_hash, g_str_equal);
	style_font_hash   = g_hash_table_new (font_hash, font_equal);
	style_border_hash = g_hash_table_new (border_hash, border_equal);
	style_color_hash  = g_hash_table_new (color_hash, color_equal);

	font_init ();
}
