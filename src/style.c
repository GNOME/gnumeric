/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include "style.h"

static GHashTable *style_format_hash;
static GHashTable *style_font_hash;
static GHashTable *style_border_hash;

StyleFormat *
style_format_new (char *name)
{
	StyleFormat *format;

	g_return_val_if_fail (name != NULL, NULL);
	
	format = (StyleFormat *) g_hash_table_lookup (style_format_hash, name);
	
	if (!format){
		format = g_new0 (StyleFormat, 1);
		format->format = g_strdup (name);
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
	g_free (sf->format);
	g_free (sf);
}

StyleFont *
style_font_new (char *font_name, int units)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (units != 0, NULL);
	
	key.font_name = font_name;
	key.units    = units;
	
	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->units    = units;
		g_hash_table_insert (style_font_hash, font, font);
	}
	font->ref_count++;

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
style_border_new (StyleBorderType left, StyleBorderType right,
		  StyleBorderType top,  StyleBorderType bottom,
		  GdkColor *left_color,  GdkColor *right_color,
		  GdkColor *top_color,   GdkColor *bottom_color)
{
	StyleBorder key, *border;

	key.left    	 = left;
	key.right   	 = right;
	key.top     	 = top;
	key.bottom  	 = bottom;
	if (left_color)
		key.left_color   = *left_color;
	if (right_color)
		key.right_color  = *right_color;
	if (top_color)
		key.top_color    = *top_color;
	if (bottom_color)
		key.bottom_color = *bottom_color;

	border = (StyleBorder *) g_hash_table_lookup (style_border_hash, &key);
	if (!border){
		border = g_new0 (StyleBorder, 1);
		*border = key;
		g_hash_table_insert (style_border_hash, border, border);
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
	return style_border_new (BORDER_NONE, BORDER_NONE,
				 BORDER_NONE, BORDER_NONE,
				 NULL, NULL, NULL, NULL);
}

Style *
style_new (void)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->format  = style_format_new ("#");
	style->font    = style_font_new ("Times", 14);
	style->border  = style_border_new_plain ();

	style->halign = HALIGN_LEFT;
	style->valign = VALIGN_CENTER;
	style->orientation = ORIENT_HORIZ;
	
	return style;
}

Style *
style_duplicate (Style *original)
{
	Style *style;

	style = g_new0 (Style, 1);

	*style = *original;
	
	style_format_ref (original->format);
	style_font_ref   (original->font);
	style_border_ref (original->border);

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

	if (k1->units != k2->units)
		return 0;
	
	return !strcmp (k1->font_name, k2->font_name);
}

static guint
font_hash (gconstpointer v)
{
	StyleFont *k = (StyleFont *) v;

	return k->units + g_str_hash (k->font_name);
}

static gint
border_equal (gconstpointer v, gconstpointer v2)
{
	StyleBorder *k1 = (StyleBorder *) v;
	StyleBorder *k2 = (StyleBorder *) v2;

	if (k1->left != k2->left)
		return 0;
	if (k1->right != k2->right)
		return 0;
	if (k1->top != k2->top)
		return 0;
	if (k1->bottom != k2->bottom)
		return 0;
	if (k1->left != BORDER_NONE)
		if (!gdk_color_equal (&k1->left_color, &k2->left_color))
			return 0;
	if (k1->right != BORDER_NONE)
		if (!gdk_color_equal (&k1->right_color, &k2->right_color))
			return 0;
	if (k1->top != BORDER_NONE)
		if (gdk_color_equal (&k1->top_color, &k2->top_color))
			return 0;
	if (k1->bottom != BORDER_NONE)
		if (gdk_color_equal (&k1->bottom_color, &k2->bottom_color))
			return 0;
	return 1;
}

static guint
border_hash (gconstpointer v)
{
	StyleBorder *k = (StyleBorder *) v;

	return (k->left << 12) | (k->right << 8) | (k->top << 4) | (k->bottom);
}

void
style_init (void)
{
	style_format_hash = g_hash_table_new (g_str_hash, g_str_equal);
	style_font_hash   = g_hash_table_new (font_hash, font_equal);
	style_border_hash = g_hash_table_new (border_hash, border_equal);
}

